#include "watchflow/async/ThreadPool.hpp"

#include <algorithm>

namespace watchflow {

ThreadPool::ThreadPool(
    std::shared_ptr<TaskQueue> queue,
    std::shared_ptr<TaskExecutor> executor,
    size_t workerCount
) : queue_(std::move(queue)),
    executor_(std::move(executor)),
    workerCount_(workerCount > 0 ? workerCount : std::max(2u, std::thread::hardware_concurrency())),
    workerTaskCounts_(workerCount_ > 0 ? workerCount_ : std::max(2u, std::thread::hardware_concurrency())) {

    workers_.reserve(workerCount_);
    for (size_t i = 0; i < workerCount_; ++i) {
        workerTaskCounts_[i].store(0, std::memory_order_relaxed);
        workers_.emplace_back(&ThreadPool::workerLoop, this, i);
    }
}

ThreadPool::~ThreadPool() {
    shutdown(false);
}

void ThreadPool::workerLoop(size_t workerId) {
    while (true) {
        Task task;
        if (!queue_->pop(task)) {
            // Queue is shut down and empty
            break;
        }

        activeWorkers_.fetch_add(1, std::memory_order_relaxed);

        try {
            executor_->execute(task);
        } catch (const std::exception& ex) {
            // TaskExecutor already handles exceptions, but this is a failsafe
            // to ensure no exception ever kills the worker thread.
        } catch (...) {
        }

        workerTaskCounts_[workerId].fetch_add(1, std::memory_order_relaxed);
        activeWorkers_.fetch_sub(1, std::memory_order_relaxed);

        // Check if drained
        if (queue_->empty() && activeWorkers_.load(std::memory_order_relaxed) == 0) {
            std::lock_guard<std::mutex> lock(drainMutex_);
            cvDrain_.notify_all();
        }
    }
}

void ThreadPool::shutdown(bool drain) {
    {
        std::lock_guard<std::mutex> lock(shutdownMutex_);
        if (shutdown_.exchange(true)) {
            return;
        }
    }

    if (drain) {
        // Wait until queue is empty and active workers finish
        this->drain();
    }

    // Signal queue to wake waiting workers
    queue_->shutdown();

    // Join all worker threads safely
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void ThreadPool::drain() {
    std::unique_lock<std::mutex> lock(drainMutex_);
    cvDrain_.wait(lock, [this]() {
        return queue_->empty() && activeWorkers_.load(std::memory_order_relaxed) == 0;
    });
}

size_t ThreadPool::workerCount() const {
    return workerCount_;
}

size_t ThreadPool::activeWorkers() const {
    return activeWorkers_.load(std::memory_order_relaxed);
}

bool ThreadPool::isShutdown() const {
    return shutdown_.load(std::memory_order_relaxed);
}

std::vector<size_t> ThreadPool::workerTaskCounts() const {
    std::vector<size_t> counts(workerCount_);
    for (size_t i = 0; i < workerCount_; ++i) {
        counts[i] = workerTaskCounts_[i].load(std::memory_order_relaxed);
    }
    return counts;
}

} // namespace watchflow
