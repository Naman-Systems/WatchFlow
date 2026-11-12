#include "watchflow/config/ConfigManager.hpp"

#include "watchflow/action/CompressAction.hpp"
#include "watchflow/action/CopyAction.hpp"
#include "watchflow/action/ExecuteAction.hpp"
#include "watchflow/action/MoveAction.hpp"
#include "watchflow/action/NotificationAction.hpp"
#include "watchflow/matcher/ExtensionMatcher.hpp"
#include "watchflow/matcher/FileSizeMatcher.hpp"
#include "watchflow/matcher/RegexMatcher.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

namespace watchflow {

namespace {

std::vector<std::string> split(const std::string& line, char delimiter) {
    std::vector<std::string> parts;
    std::stringstream ss(line);
    std::string item;
    while (std::getline(ss, item, delimiter)) parts.push_back(item);
    return parts;
}

std::string trim(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(),
        [](unsigned char ch) { return std::isspace(ch); });
    const auto last = std::find_if_not(value.rbegin(), value.rend(),
        [](unsigned char ch) { return std::isspace(ch); }).base();

    if (first >= last) return {};
    return std::string(first, last);
}

std::string unquote(std::string value) {
    value = trim(std::move(value));
    if (value.size() >= 2 &&
        ((value.front() == '"' && value.back() == '"') ||
         (value.front() == '\'' && value.back() == '\''))) {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

std::string upper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    return value;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::vector<std::string> parseList(std::string value) {
    value = trim(std::move(value));
    if (value.empty()) return {};

    if (value.front() == '[' && value.back() == ']') {
        value = value.substr(1, value.size() - 2);
    }

    std::vector<std::string> values;
    for (auto& item : split(value, ',')) {
        auto cleaned = unquote(item);
        if (!cleaned.empty()) values.push_back(std::move(cleaned));
    }
    return values;
}

EventType parseEvent(const std::string& value) {
    const auto normalized = upper(trim(value));
    if (normalized == "CREATED") return EventType::Created;
    if (normalized == "MODIFIED") return EventType::Modified;
    if (normalized == "DELETED") return EventType::Deleted;
    if (normalized == "MOVED") return EventType::Moved;
    throw std::runtime_error("Unknown event: " + value);
}

DuplicateMode parseDuplicateMode(const std::string& value) {
    const auto normalized = lower(trim(value));
    if (normalized == "overwrite") return DuplicateMode::Overwrite;
    if (normalized == "rename") return DuplicateMode::Rename;
    if (normalized == "skip") return DuplicateMode::Skip;
    throw std::runtime_error("Unknown duplicate mode: " + value);
}

std::chrono::milliseconds parseMilliseconds(const std::string& value) {
    return std::chrono::milliseconds(std::stoll(trim(value)));
}

std::unique_ptr<IMatcher> makeMatcher(const std::string& type, const std::string& value) {
    const auto normalized = upper(trim(type));

    if (normalized == "REGEX") {
        return std::make_unique<RegexMatcher>(value);
    }

    if (normalized == "EXT" || normalized == "EXTENSION" || normalized == "EXTENSIONS") {
        return std::make_unique<ExtensionMatcher>(parseList(value));
    }

    if (normalized == "SIZE_GT") {
        return std::make_unique<FileSizeMatcher>(
            std::stoull(value), SizeComparison::GreaterThan);
    }

    if (normalized == "SIZE_LT") {
        return std::make_unique<FileSizeMatcher>(
            std::stoull(value), SizeComparison::LessThan);
    }

    if (normalized == "SIZE_EQ") {
        return std::make_unique<FileSizeMatcher>(
            std::stoull(value), SizeComparison::Equal);
    }

    throw std::runtime_error("Unknown matcher: " + type);
}

std::unique_ptr<IAction> makeAction(
    const std::string& type,
    const std::string& value,
    const ActionOptions& options) {
    const auto normalized = upper(trim(type));

    if (normalized == "COPY") {
        return std::make_unique<CopyAction>(value, options);
    }

    if (normalized == "MOVE") {
        return std::make_unique<MoveAction>(value, options);
    }

    if (normalized == "EXECUTE") {
        return std::make_unique<ExecuteAction>(value);
    }

    if (normalized == "COMPRESS") {
        return std::make_unique<CompressAction>(value.empty() ? ".gz" : value);
    }

    if (normalized == "NOTIFY") {
        return std::make_unique<NotificationAction>(value);
    }

    throw std::runtime_error("Unknown action: " + type);
}

Config loadPipeConfig(std::ifstream& input) {
    Config config;
    std::string line;
    int lineNumber = 0;

    while (std::getline(input, line)) {
        ++lineNumber;
        line = trim(std::move(line));

        if (line.empty() || line[0] == '#') continue;

        const auto parts = split(line, '|');

        if (parts[0] == "WATCH") {
            if (parts.size() != 2) throw std::runtime_error("Invalid WATCH at line " + std::to_string(lineNumber));
            config.watchDirectory = trim(parts[1]);
            continue;
        }

        if (parts[0] == "LOG" || parts[0] == "LOG_FILE") {
            if (parts.size() != 2) throw std::runtime_error("Invalid LOG at line " + std::to_string(lineNumber));
            config.logFile = trim(parts[1]);
            continue;
        }

        if (parts[0] == "DUPLICATES") {
            if (parts.size() != 2) throw std::runtime_error("Invalid DUPLICATES at line " + std::to_string(lineNumber));
            config.actionOptions.duplicateMode = parseDuplicateMode(parts[1]);
            continue;
        }

        if (parts[0] == "WAIT_UNTIL_COMPLETE") {
            if (parts.size() != 4) throw std::runtime_error("Invalid WAIT_UNTIL_COMPLETE at line " + std::to_string(lineNumber));
            config.actionOptions.fileStability.enabled = lower(trim(parts[1])) == "true";
            config.actionOptions.fileStability.stableFor = parseMilliseconds(parts[2]);
            config.actionOptions.fileStability.timeout = parseMilliseconds(parts[3]);
            continue;
        }

        if (parts[0] == "RULE" && parts.size() == 7) {
            config.rules.emplace_back(
                trim(parts[1]),
                parseEvent(parts[2]),
                makeMatcher(parts[3], parts[4]),
                makeAction(parts[5], parts[6], config.actionOptions)
            );
            continue;
        }

        throw std::runtime_error("Invalid config line " + std::to_string(lineNumber));
    }

    return config;
}

std::pair<std::string, std::string> parseYamlKeyValue(const std::string& line) {
    const auto separator = line.find(':');
    if (separator == std::string::npos) {
        throw std::runtime_error("Invalid YAML line: " + line);
    }

    return {
        trim(line.substr(0, separator)),
        unquote(line.substr(separator + 1))
    };
}

void addYamlRule(
    Config& config,
    const std::map<std::string, std::string>& rule) {
    if (rule.empty()) return;

    const auto name = rule.contains("name") ? rule.at("name") : "Unnamed rule";
    const auto action = rule.contains("action") ? rule.at("action") : "";
    const auto destination = rule.contains("destination")
        ? rule.at("destination")
        : (rule.contains("value") ? rule.at("value") : "");
    const auto matcher = rule.contains("matcher") ? rule.at("matcher") : "extension";
    const auto matcherValue = rule.contains("extensions")
        ? rule.at("extensions")
        : (rule.contains("pattern") ? rule.at("pattern") : rule.at("value"));

    std::vector<std::string> events;
    if (rule.contains("events")) {
        events = parseList(rule.at("events"));
    } else if (rule.contains("event")) {
        events.push_back(rule.at("event"));
    } else {
        events = {"created", "moved"};
    }

    for (const auto& event : events) {
        config.rules.emplace_back(
            name + " " + lower(event),
            parseEvent(event),
            makeMatcher(matcher, matcherValue),
            makeAction(action, destination, config.actionOptions));
    }
}

Config loadYamlConfig(std::ifstream& input) {
    Config config;
    std::string section;
    std::map<std::string, std::string> currentRule;
    std::string line;
    int lineNumber = 0;

    while (std::getline(input, line)) {
        ++lineNumber;

        const auto comment = line.find('#');
        if (comment != std::string::npos) line = line.substr(0, comment);

        line = trim(std::move(line));
        if (line.empty()) continue;

        if (line == "wait_until_complete:" || line == "rules:") {
            if (section == "rules") {
                addYamlRule(config, currentRule);
                currentRule.clear();
            }

            section = line.substr(0, line.size() - 1);
            continue;
        }

        if (section == "rules" && line.rfind("- ", 0) == 0) {
            addYamlRule(config, currentRule);
            currentRule.clear();
            line = trim(line.substr(2));
        }

        const auto [key, value] = parseYamlKeyValue(line);

        if (section == "wait_until_complete") {
            if (key == "enabled") {
                config.actionOptions.fileStability.enabled = lower(value) == "true";
            } else if (key == "stable_for_ms") {
                config.actionOptions.fileStability.stableFor = parseMilliseconds(value);
            } else if (key == "timeout_ms") {
                config.actionOptions.fileStability.timeout = parseMilliseconds(value);
            } else {
                throw std::runtime_error("Unknown wait_until_complete key at line " + std::to_string(lineNumber));
            }
            continue;
        }

        if (section == "rules") {
            currentRule[key] = value;
            continue;
        }

        if (key == "watch") {
            config.watchDirectory = value;
        } else if (key == "log_file") {
            config.logFile = value;
        } else if (key == "duplicates") {
            config.actionOptions.duplicateMode = parseDuplicateMode(value);
        } else {
            throw std::runtime_error("Unknown YAML key at line " + std::to_string(lineNumber));
        }
    }

    if (section == "rules") {
        addYamlRule(config, currentRule);
    }

    return config;
}

bool isYamlFile(const std::filesystem::path& file) {
    const auto extension = lower(file.extension().string());
    return extension == ".yml" || extension == ".yaml";
}

} // namespace

Config ConfigManager::load(const std::filesystem::path& file) const {
    std::ifstream input(file);
    if (!input) throw std::runtime_error("Cannot open config: " + file.string());

    auto config = isYamlFile(file)
        ? loadYamlConfig(input)
        : loadPipeConfig(input);

    if (config.watchDirectory.empty())
        throw std::runtime_error("WATCH directory is required");

    return config;
}

} // namespace watchflow
