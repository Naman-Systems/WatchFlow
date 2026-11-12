#pragma once

#include "watchflow/action/CommandFactory.hpp"
#include "watchflow/async/Task.hpp"
#include "watchflow/logging/ConsoleLogger.hpp"
#include "watchflow/logging/FileLogger.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace watchflow {

class TaskExecutor {
public:
    using TaskCallback = std::function<void(const Task&, bool success)>;

    explicit TaskExecutor(
        ConsoleLogger* consoleLogger = nullptr,
        FileLogger* fileLogger = nullptr,
        TaskCallback onComplete = nullptr
    );

    ~TaskExecutor() = default;

    // Prevent copy/move
    TaskExecutor(const TaskExecutor&) = delete;
    TaskExecutor& operator=(const TaskExecutor&) = delete;
    TaskExecutor(TaskExecutor&&) = delete;
    TaskExecutor& operator=(TaskExecutor&&) = delete;

    /**
     * @brief Executes a single task.
     * Handles per-file synchronization, command execution, exception catching,
     * transient retries, logging, and status tracking.
     * @param task The task to execute.
     * @return true if task executed successfully, false otherwise.
     */
    bool execute(Task& task);

    // Callbacks & Loggers
    void setConsoleLogger(ConsoleLogger* logger);
    void setFileLogger(FileLogger* logger);
    void setOnCompleteCallback(TaskCallback callback);

    // Metrics
    size_t totalExecuted() const;
    size_t totalSucceeded() const;
    size_t totalFailed() const;
    size_t totalRetried() const;
    int64_t totalExecutionTimeMs() const;
    double averageExecutionTimeMs() const;

private:
    std::shared_ptr<std::mutex> getFileLock(const std::filesystem::path& path);

    ConsoleLogger* consoleLogger_{nullptr};
    FileLogger* fileLogger_{nullptr};
    TaskCallback onComplete_{nullptr};

    // Per-file lock management
    std::mutex fileLocksMutex_;
    std::unordered_map<std::string, std::shared_ptr<std::mutex>> fileLocks_;

    // Metrics (atomic for lock-free updates)
    std::atomic<size_t> totalExecuted_{0};
    std::atomic<size_t> totalSucceeded_{0};
    std::atomic<size_t> totalFailed_{0};
    std::atomic<size_t> totalRetried_{0};
    std::atomic<int64_t> totalExecutionTimeMs_{0};
};

} // namespace watchflow
