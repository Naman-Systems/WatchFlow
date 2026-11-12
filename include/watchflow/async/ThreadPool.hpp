#pragma once

#include "watchflow/async/TaskExecutor.hpp"
#include "watchflow/async/TaskQueue.hpp"

#include <atomic>
#include <cstddef>
#include <memory>
#include <thread>
#include <vector>

namespace watchflow {

class ThreadPool {
public:
    /**
     * @brief Creates a ThreadPool with a given number of worker threads.
     * @param queue Shared TaskQueue to pull tasks from.
     * @param executor Shared TaskExecutor to execute tasks.
     * @param workerCount Number of worker threads (default: hardware concurrency or minimum 2).
     */
    ThreadPool(
        std::shared_ptr<TaskQueue> queue,
        std::shared_ptr<TaskExecutor> executor,
        size_t workerCount = 0
    );

    ~ThreadPool();

    // Prevent copy/move
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    /**
     * @brief Initiates graceful shutdown: shuts down the task queue and joins all workers.
     * @param drain If true, workers will finish all pending tasks in the queue before exiting.
     */
    void shutdown(bool drain = true);

    /**
     * @brief Blocks until all tasks currently in the queue are completed.
     */
    void drain();

    /**
     * @brief Returns the configured number of worker threads.
     */
    size_t workerCount() const;

    /**
     * @brief Returns the number of currently active workers (currently executing a task).
     */
    size_t activeWorkers() const;

    /**
     * @brief Returns whether the pool has been shut down.
     */
    bool isShutdown() const;

    /**
     * @brief Returns task counts processed by each worker.
     */
    std::vector<size_t> workerTaskCounts() const;

private:
    void workerLoop(size_t workerId);

    std::shared_ptr<TaskQueue> queue_;
    std::shared_ptr<TaskExecutor> executor_;
    const size_t workerCount_;

    std::vector<std::thread> workers_;
    std::vector<std::atomic<size_t>> workerTaskCounts_;

    std::atomic<size_t> activeWorkers_{0};
    std::atomic<bool> shutdown_{false};
    std::mutex shutdownMutex_;

    // For drain notification
    std::mutex drainMutex_;
    std::condition_variable cvDrain_;
};

} // namespace watchflow
