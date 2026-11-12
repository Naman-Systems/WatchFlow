#include "watchflow/async/Task.hpp"
#include "watchflow/rule/Rule.hpp"

#include <atomic>

namespace watchflow {

namespace {
    std::atomic<uint64_t> gTaskIdSequence{1};
}

const char* toString(TaskStatus status) {
    switch (status) {
        case TaskStatus::Pending:
            return "Pending";
        case TaskStatus::Running:
            return "Running";
        case TaskStatus::Completed:
            return "Completed";
        case TaskStatus::Failed:
            return "Failed";
        case TaskStatus::Retrying:
            return "Retrying";
    }
    return "Unknown";
}

int64_t Task::queueWaitTimeMs() const {
    const auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - queuedAt).count();
}

Task createTask(const Rule& rule, const FileEvent& event) {
    Task task;
    task.id = gTaskIdSequence.fetch_add(1, std::memory_order_relaxed);
    task.filePath = event.path;
    task.eventType = event.type;
    task.ruleName = rule.name();
    task.action = rule.action();
    task.status = TaskStatus::Pending;
    task.createdAt = std::chrono::system_clock::now();
    task.queuedAt = std::chrono::steady_clock::now();
    task.retryCount = 0;
    task.maxRetries = 2;

    return task;
}

} // namespace watchflow
