#pragma once

#include "watchflow/core/FileEvent.hpp"
#include <mutex>
#include <string>

namespace watchflow {

class ConsoleLogger {
public:
    void log(const FileEvent& event) const;
    void logMessage(const std::string& message) const;

private:
    static inline std::mutex consoleMutex_;
};

} // namespace watchflow
