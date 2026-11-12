#pragma once

#include "watchflow/async/Task.hpp"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>

namespace watchflow {

class TaskQueue {
public:
    explicit TaskQueue(size_t capacity = 1000);
    ~TaskQueue();

    // Prevent copy/move
    TaskQueue(const TaskQueue&) = delete;
    TaskQueue& operator=(const TaskQueue&) = delete;
    TaskQueue(TaskQueue&&) = delete;
    TaskQueue& operator=(TaskQueue&&) = delete;

    /**
     * @brief Submits a task to the queue. Blocks if the queue is at capacity
     * until space is available, or until queue is shut down.
     * @param task The task to push.
     * @return true if pushed, false if rejected due to shutdown.
     */
    bool push(Task task);

    /**
     * @brief Submits a task with a timeout if queue is full.
     * @return true if pushed, false if timed out or shutdown.
     */
    bool push(Task task, std::chrono::milliseconds timeout);

    /**
     * @brief Non-blocking push. Fails immediately if queue is full or shut down.
     */
    bool try_push(Task task);

    /**
     * @brief Retrieves the next task from the queue. Blocks if the queue is empty
     * until a task is available or the queue is shut down.
     * @param task Output parameter to store the popped task.
     * @return true if task was popped, false if queue is shut down and empty.
     */
    bool pop(Task& task);

    /**
     * @brief Non-blocking pop. Returns false immediately if queue is empty.
     */
    bool try_pop(Task& task);

    /**
     * @brief Shuts down the queue. No new tasks will be accepted.
     * Wakes all waiting producers and consumers.
     */
    void shutdown();

    /**
     * @brief Checks if queue is shut down.
     */
    bool isShutdown() const;

    /**
     * @brief Returns current number of pending tasks.
     */
    size_t size() const;

    /**
     * @brief Checks if queue is empty.
     */
    bool empty() const;

    /**
     * @brief Returns max queue capacity (0 = unbounded).
     */
    size_t capacity() const;

    /**
     * @brief Clears pending tasks from the queue.
     */
    void clear();

    // Metrics
    size_t totalPushed() const;
    size_t totalPopped() const;
    size_t totalDropped() const;

private:
    const size_t capacity_;
    std::queue<Task> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cvNotEmpty_;
    std::condition_variable cvNotFull_;
    bool shutdown_{false};

    size_t totalPushed_{0};
    size_t totalPopped_{0};
    size_t totalDropped_{0};
};

} // namespace watchflow
