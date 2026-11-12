#include "watchflow/async/TaskExecutor.hpp"

#include <iostream>
#include <sstream>
#include <thread>

namespace watchflow {

TaskExecutor::TaskExecutor(
    ConsoleLogger* consoleLogger,
    FileLogger* fileLogger,
    TaskCallback onComplete
) : consoleLogger_(consoleLogger),
    fileLogger_(fileLogger),
    onComplete_(std::move(onComplete)) {
}

std::shared_ptr<std::mutex> TaskExecutor::getFileLock(const std::filesystem::path& path) {
    const std::string key = path.string();
    std::lock_guard<std::mutex> lock(fileLocksMutex_);

    auto it = fileLocks_.find(key);
    if (it != fileLocks_.end()) {
        return it->second;
    }

    auto newLock = std::make_shared<std::mutex>();
    fileLocks_[key] = newLock;
    return newLock;
}

bool TaskExecutor::execute(Task& task) {
    task.status = TaskStatus::Running;
    const auto startTime = std::chrono::steady_clock::now();
    const auto queueWaitMs = task.queueWaitTimeMs();

    // Log start of task execution
    std::ostringstream startMsg;
    startMsg << "[TASK #" << task.id << "] Running rule: \"" << task.ruleName
             << "\" on " << task.filePath << " (queue wait: " << queueWaitMs << "ms)";
    
    if (consoleLogger_) {
        consoleLogger_->logMessage(startMsg.str());
    }
    if (fileLogger_) {
        fileLogger_->logMessage(startMsg.str());
    }

    // Acquire file-specific lock to prevent conflicting concurrent file ops
    auto fileLock = getFileLock(task.filePath);
    std::lock_guard<std::mutex> fileGuard(*fileLock);

    const FileEvent event{task.eventType, task.filePath};
    bool success = false;
    std::string errorDetails;

    // Retry loop for transient failures
    while (true) {
        try {
            if (!task.action) {
                errorDetails = "No action associated with task";
                success = false;
            } else {
                success = task.action->execute(event);
                if (!success) {
                    errorDetails = "Action execute() returned false";
                }
            }
        } catch (const std::exception& ex) {
            success = false;
            errorDetails = ex.what();
        } catch (...) {
            success = false;
            errorDetails = "Unknown exception";
        }

        if (success) {
            break;
        }

        // Check if we can retry
        if (task.retryCount < task.maxRetries) {
            ++task.retryCount;
            ++totalRetried_;
            task.status = TaskStatus::Retrying;

            std::ostringstream retryMsg;
            retryMsg << "[TASK #" << task.id << "] Failed: " << errorDetails
                     << ". Retrying (" << task.retryCount << "/" << task.maxRetries << ")...";
            if (consoleLogger_) consoleLogger_->logMessage(retryMsg.str());
            if (fileLogger_) fileLogger_->logMessage(retryMsg.str());

            // Brief backoff before transient retry
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        } else {
            break;
        }
    }

    const auto endTime = std::chrono::steady_clock::now();
    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        endTime - startTime).count();

    totalExecutionTimeMs_.fetch_add(durationMs, std::memory_order_relaxed);
    totalExecuted_.fetch_add(1, std::memory_order_relaxed);

    if (success) {
        task.status = TaskStatus::Completed;
        totalSucceeded_.fetch_add(1, std::memory_order_relaxed);

        std::ostringstream successMsg;
        successMsg << "[TASK #" << task.id << "] [SUCCESS] rule: \"" << task.ruleName
                   << "\" completed in " << durationMs << "ms";
        if (consoleLogger_) consoleLogger_->logMessage(successMsg.str());
        if (fileLogger_) {
            fileLogger_->logRuleResult(task.ruleName, true);
            fileLogger_->logMessage(successMsg.str());
        }
    } else {
        task.status = TaskStatus::Failed;
        task.errorMessage = errorDetails;
        totalFailed_.fetch_add(1, std::memory_order_relaxed);

        std::ostringstream failMsg;
        failMsg << "[TASK #" << task.id << "] [FAILED] rule: \"" << task.ruleName
                << "\" failed after " << durationMs << "ms: " << errorDetails;
        if (consoleLogger_) consoleLogger_->logMessage(failMsg.str());
        if (fileLogger_) {
            fileLogger_->logRuleResult(task.ruleName, false);
            fileLogger_->logMessage(failMsg.str());
        }
    }

    if (onComplete_) {
        onComplete_(task, success);
    }

    return success;
}

void TaskExecutor::setConsoleLogger(ConsoleLogger* logger) {
    consoleLogger_ = logger;
}

void TaskExecutor::setFileLogger(FileLogger* logger) {
    fileLogger_ = logger;
}

void TaskExecutor::setOnCompleteCallback(TaskCallback callback) {
    onComplete_ = std::move(callback);
}

size_t TaskExecutor::totalExecuted() const {
    return totalExecuted_.load(std::memory_order_relaxed);
}

size_t TaskExecutor::totalSucceeded() const {
    return totalSucceeded_.load(std::memory_order_relaxed);
}

size_t TaskExecutor::totalFailed() const {
    return totalFailed_.load(std::memory_order_relaxed);
}

size_t TaskExecutor::totalRetried() const {
    return totalRetried_.load(std::memory_order_relaxed);
}

int64_t TaskExecutor::totalExecutionTimeMs() const {
    return totalExecutionTimeMs_.load(std::memory_order_relaxed);
}

double TaskExecutor::averageExecutionTimeMs() const {
    const size_t executed = totalExecuted();
    if (executed == 0) return 0.0;
    return static_cast<double>(totalExecutionTimeMs()) / static_cast<double>(executed);
}

} // namespace watchflow
