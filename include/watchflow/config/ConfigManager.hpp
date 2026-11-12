#pragma once

#include "watchflow/rule/Rule.hpp"
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace watchflow {

enum class DuplicateMode {
    Overwrite,
    Rename,
    Skip
};

struct FileStabilityOptions {
    bool enabled = false;
    std::chrono::milliseconds stableFor{1000};
    std::chrono::milliseconds timeout{60000};
};

struct ActionOptions {
    DuplicateMode duplicateMode = DuplicateMode::Overwrite;
    FileStabilityOptions fileStability;
};

struct Config {
    std::filesystem::path watchDirectory;
    std::filesystem::path logFile;
    ActionOptions actionOptions;
    std::vector<Rule> rules;
    size_t workers = 0;
    size_t queueCapacity = 1000;
};

class ConfigManager {
public:
    Config load(const std::filesystem::path& file) const;
};

} // namespace watchflow
