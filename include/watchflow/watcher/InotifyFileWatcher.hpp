#pragma once

#include "watchflow/watcher/IFileWatcher.hpp"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace watchflow {

class InotifyFileWatcher final : public IFileWatcher {
public:
    explicit InotifyFileWatcher(
        std::filesystem::path directory,
        std::chrono::milliseconds debounceWindow = std::chrono::milliseconds(50)
    );
    ~InotifyFileWatcher() override;

    void start(EventCallback callback) override;
    void stop() override;

    void setDebounceWindow(std::chrono::milliseconds window);
    void rescanDirectory(const EventCallback& callback);

private:
    void run(EventCallback callback);
    bool shouldDeduplicate(const std::filesystem::path& path, EventType type);

    std::filesystem::path directory_;
    std::chrono::milliseconds debounceWindow_{50};
    std::atomic<bool> running_{false};
    std::thread worker_;
    int stopFd_{-1};

    std::mutex dedupeMutex_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> eventTimestamps_;
};

} // namespace watchflow
