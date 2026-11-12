#pragma once

#include "watchflow/action/IAction.hpp"
#include "watchflow/core/FileEvent.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace watchflow {

class Rule;

enum class TaskStatus {
    Pending,
    Running,
    Completed,
    Failed,
    Retrying
};

const char* toString(TaskStatus status);

struct Task {
    uint64_t id{0};
    std::filesystem::path filePath;
    EventType eventType{EventType::Created};
    std::string ruleName;
    std::string actionType;
    std::filesystem::path destinationPath;
    std::chrono::system_clock::time_point createdAt{std::chrono::system_clock::now()};
    std::chrono::steady_clock::time_point queuedAt{std::chrono::steady_clock::now()};
    int retryCount{0};
    int maxRetries{2};
    int priority{0}; // Higher integer = higher priority
    TaskStatus status{TaskStatus::Pending};
    std::shared_ptr<IAction> action;
    std::string errorMessage;

    // Helper to get queue wait time in milliseconds
    int64_t queueWaitTimeMs() const;
};

// Factory helper to construct a Task from a Rule and FileEvent
Task createTask(const Rule& rule, const FileEvent& event);

} // namespace watchflow
