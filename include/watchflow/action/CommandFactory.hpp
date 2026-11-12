#pragma once

#include "watchflow/action/CompressAction.hpp"
#include "watchflow/action/CopyAction.hpp"
#include "watchflow/action/ExecuteAction.hpp"
#include "watchflow/action/IAction.hpp"
#include "watchflow/action/MoveAction.hpp"
#include "watchflow/action/NotificationAction.hpp"
#include "watchflow/config/ConfigManager.hpp"

#include <memory>
#include <string>

namespace watchflow {

// Command Pattern Aliases for Action classes
using ICommand = IAction;
using CopyCommand = CopyAction;
using MoveCommand = MoveAction;
using CompressCommand = CompressAction;
using ExecuteCommand = ExecuteAction;
using NotificationCommand = NotificationAction;

/**
 * @brief Factory class to create Command (IAction) instances,
 * adhering to the Factory Design Pattern.
 */
class CommandFactory {
public:
    static std::shared_ptr<ICommand> createCommand(
        const std::string& type,
        const std::string& value,
        const ActionOptions& options = {})
    {
        std::string normalized = type;
        for (auto& c : normalized) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }

        if (normalized == "COPY") {
            return std::make_shared<CopyCommand>(value, options);
        }
        if (normalized == "MOVE") {
            return std::make_shared<MoveCommand>(value, options);
        }
        if (normalized == "COMPRESS") {
            return std::make_shared<CompressCommand>(value.empty() ? ".gz" : value);
        }
        if (normalized == "EXECUTE") {
            return std::make_shared<ExecuteCommand>(value);
        }
        if (normalized == "NOTIFY") {
            return std::make_shared<NotificationCommand>(value);
        }

        return nullptr;
    }
};

} // namespace watchflow
