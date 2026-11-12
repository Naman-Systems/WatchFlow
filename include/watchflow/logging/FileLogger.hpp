#pragma once

#include "watchflow/core/FileEvent.hpp"
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace watchflow {

class FileLogger {
public:
    explicit FileLogger(std::filesystem::path file);

    void logEvent(const FileEvent& event);
    void logRuleResult(const std::string& ruleName, bool success);
    void logMessage(const std::string& message);

private:
    std::ofstream output_;
    std::mutex mutex_;
};

} // namespace watchflow
