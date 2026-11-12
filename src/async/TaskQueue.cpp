#include "watchflow/async/TaskQueue.hpp"

namespace watchflow {

TaskQueue::TaskQueue(size_t capacity)
    : capacity_(capacity) {
}

TaskQueue::~TaskQueue() {
    shutdown();
}

bool TaskQueue::push(Task task) {
    std::unique_lock<std::mutex> lock(mutex_);

    // If bounded, wait until there is space or shutdown
    if (capacity_ > 0) {
        cvNotFull_.wait(lock, [this]() {
            return shutdown_ || queue_.size() < capacity_;
        });
    }

    if (shutdown_) {
        ++totalDropped_;
        return false;
    }

    task.queuedAt = std::chrono::steady_clock::now();
    queue_.push(std::move(task));
    ++totalPushed_;

    lock.unlock();
    cvNotEmpty_.notify_one();
    return true;
}

bool TaskQueue::push(Task task, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);

    if (capacity_ > 0) {
        const bool hasSpace = cvNotFull_.wait_for(lock, timeout, [this]() {
            return shutdown_ || queue_.size() < capacity_;
        });

        if (!hasSpace || shutdown_) {
            ++totalDropped_;
            return false;
        }
    } else if (shutdown_) {
        ++totalDropped_;
        return false;
    }

    task.queuedAt = std::chrono::steady_clock::now();
    queue_.push(std::move(task));
    ++totalPushed_;

    lock.unlock();
    cvNotEmpty_.notify_one();
    return true;
}

bool TaskQueue::try_push(Task task) {
    std::unique_lock<std::mutex> lock(mutex_);

    if (shutdown_ || (capacity_ > 0 && queue_.size() >= capacity_)) {
        ++totalDropped_;
        return false;
    }

    task.queuedAt = std::chrono::steady_clock::now();
    queue_.push(std::move(task));
    ++totalPushed_;

    lock.unlock();
    cvNotEmpty_.notify_one();
    return true;
}

bool TaskQueue::pop(Task& task) {
    std::unique_lock<std::mutex> lock(mutex_);

    cvNotEmpty_.wait(lock, [this]() {
        return shutdown_ || !queue_.empty();
    });

    if (queue_.empty()) {
        // Shutdown and empty
        return false;
    }

    task = std::move(queue_.front());
    queue_.pop();
    ++totalPopped_;

    lock.unlock();
    cvNotFull_.notify_one();
    return true;
}

bool TaskQueue::try_pop(Task& task) {
    std::unique_lock<std::mutex> lock(mutex_);

    if (queue_.empty()) {
        return false;
    }

    task = std::move(queue_.front());
    queue_.pop();
    ++totalPopped_;

    lock.unlock();
    cvNotFull_.notify_one();
    return true;
}

void TaskQueue::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (shutdown_) {
            return;
        }
        shutdown_ = true;
    }

    // Wake all waiting consumers and producers
    cvNotEmpty_.notify_all();
    cvNotFull_.notify_all();
}

bool TaskQueue::isShutdown() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return shutdown_;
}

size_t TaskQueue::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

bool TaskQueue::empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.empty();
}

size_t TaskQueue::capacity() const {
    return capacity_;
}

void TaskQueue::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!queue_.empty()) {
        queue_.pop();
        ++totalDropped_;
    }
    cvNotFull_.notify_all();
}

size_t TaskQueue::totalPushed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return totalPushed_;
}

size_t TaskQueue::totalPopped() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return totalPopped_;
}

size_t TaskQueue::totalDropped() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return totalDropped_;
}

} // namespace watchflow
