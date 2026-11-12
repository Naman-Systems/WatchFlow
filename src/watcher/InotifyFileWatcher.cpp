#include "watchflow/watcher/InotifyFileWatcher.hpp"

#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace watchflow {

InotifyFileWatcher::InotifyFileWatcher(
    std::filesystem::path directory,
    std::chrono::milliseconds debounceWindow
) : directory_(std::move(directory)),
    debounceWindow_(debounceWindow) {
    stopFd_ = eventfd(0, EFD_NONBLOCK);
    if (stopFd_ < 0) {
        throw std::runtime_error("eventfd creation failed: " + std::string(std::strerror(errno)));
    }
}

InotifyFileWatcher::~InotifyFileWatcher() {
    stop();
    if (stopFd_ >= 0) {
        close(stopFd_);
        stopFd_ = -1;
    }
}

void InotifyFileWatcher::setDebounceWindow(std::chrono::milliseconds window) {
    std::lock_guard<std::mutex> lock(dedupeMutex_);
    debounceWindow_ = window;
}

bool InotifyFileWatcher::shouldDeduplicate(const std::filesystem::path& path, EventType type) {
    if (debounceWindow_.count() <= 0) {
        return false;
    }

    const auto now = std::chrono::steady_clock::now();
    const std::string key = path.string() + ":" + std::string(toString(type));

    std::lock_guard<std::mutex> lock(dedupeMutex_);

    // Periodic cleanup of very old entries (older than 10 seconds)
    if (eventTimestamps_.size() > 500) {
        for (auto it = eventTimestamps_.begin(); it != eventTimestamps_.end();) {
            if (std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count() > 10) {
                it = eventTimestamps_.erase(it);
            } else {
                ++it;
            }
        }
    }

    auto it = eventTimestamps_.find(key);
    if (it != eventTimestamps_.end()) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second);
        if (elapsed < debounceWindow_) {
            // Duplicate event within debounce window: ignore
            return true;
        }
    }

    eventTimestamps_[key] = now;
    return false;
}

void InotifyFileWatcher::rescanDirectory(const EventCallback& callback) {
    std::error_code ec;
    if (!std::filesystem::exists(directory_, ec) || !std::filesystem::is_directory(directory_, ec)) {
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(directory_, ec)) {
        if (ec) break;
        if (entry.is_regular_file(ec)) {
            FileEvent fileEvent{EventType::Modified, entry.path()};
            callback(fileEvent);
        }
    }
}

void InotifyFileWatcher::start(EventCallback callback) {
    if (running_.exchange(true)) {
        return;
    }

    worker_ = std::thread(&InotifyFileWatcher::run, this, std::move(callback));
}

void InotifyFileWatcher::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    // Signal eventfd to wake up poll immediately
    if (stopFd_ >= 0) {
        uint64_t val = 1;
        ssize_t ret = write(stopFd_, &val, sizeof(val));
        (void)ret;
    }

    if (worker_.joinable()) {
        worker_.join();
    }
}

void InotifyFileWatcher::run(EventCallback callback) {
    const int inotifyFd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (inotifyFd < 0) {
        running_ = false;
        throw std::runtime_error("inotify_init1 failed: " + std::string(std::strerror(errno)));
    }

    // Watch for creates, writes, close-after-writes, moves, deletes, and queue overflows
    const uint32_t mask = IN_CREATE | IN_MODIFY | IN_CLOSE_WRITE |
                          IN_DELETE | IN_MOVED_TO | IN_Q_OVERFLOW | IN_DONT_FOLLOW;

    const int wd = inotify_add_watch(inotifyFd, directory_.c_str(), mask);
    if (wd < 0) {
        close(inotifyFd);
        running_ = false;
        throw std::runtime_error("inotify_add_watch failed: " + std::string(std::strerror(errno)));
    }

    std::vector<char> buffer(64 * 1024);

    struct pollfd fds[2];
    fds[0].fd = inotifyFd;
    fds[0].events = POLLIN;
    fds[1].fd = stopFd_;
    fds[1].events = POLLIN;

    while (running_) {
        const int pollRet = poll(fds, 2, -1);

        if (pollRet < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        // Stop signal received
        if (fds[1].revents & POLLIN) {
            break;
        }

        if (!(fds[0].revents & POLLIN)) {
            continue;
        }

        const ssize_t length = read(inotifyFd, buffer.data(), buffer.size());
        if (length <= 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            break;
        }

        ssize_t offset = 0;
        while (offset < length) {
            auto* event = reinterpret_cast<inotify_event*>(buffer.data() + offset);

            // Handle inotify queue overflow
            if (event->mask & IN_Q_OVERFLOW) {
                std::cerr << "[WARNING] inotify queue overflow (IN_Q_OVERFLOW). Recovering via directory rescan...\n";
                rescanDirectory(callback);
                offset += sizeof(inotify_event) + event->len;
                continue;
            }

            // Ignore directories
            if (event->len > 0 && !(event->mask & IN_ISDIR)) {
                EventType type;
                bool valid = true;

                if (event->mask & IN_CREATE) {
                    type = EventType::Created;
                } else if (event->mask & (IN_CLOSE_WRITE | IN_MODIFY)) {
                    // IN_CLOSE_WRITE indicates file write completed
                    type = EventType::Modified;
                } else if (event->mask & IN_DELETE) {
                    type = EventType::Deleted;
                } else if (event->mask & IN_MOVED_TO) {
                    type = EventType::Moved;
                } else {
                    valid = false;
                }

                if (valid) {
                    const std::filesystem::path filePath = directory_ / event->name;

                    // Deduplicate rapid repeat events
                    if (!shouldDeduplicate(filePath, type)) {
                        FileEvent fileEvent{type, filePath};
                        callback(fileEvent);
                    }
                }
            }

            offset += sizeof(inotify_event) + event->len;
        }
    }

    inotify_rm_watch(inotifyFd, wd);
    close(inotifyFd);
}

} // namespace watchflow
