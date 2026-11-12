#include "watchflow/async/Task.hpp"
#include "watchflow/async/TaskExecutor.hpp"
#include "watchflow/async/TaskQueue.hpp"
#include "watchflow/async/ThreadPool.hpp"
#include "watchflow/config/ConfigManager.hpp"
#include "watchflow/logging/ConsoleLogger.hpp"
#include "watchflow/logging/FileLogger.hpp"
#include "watchflow/rule/RuleEngine.hpp"
#include "watchflow/watcher/InotifyFileWatcher.hpp"

#include <csignal>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <unistd.h>

using namespace std;
using namespace watchflow;

namespace {
    volatile sig_atomic_t keepRunning = 1;

    void handleSignal(int) {
        keepRunning = 0;
    }
}

int main(int argc, char* argv[]) {

    // Check command-line arguments
    if (argc != 2) {
        cerr << "Usage: watchflow <config-file>\n";
        return 1;
    }

    try {

        // 1. Load configuration
        ConfigManager configManager;
        auto config = configManager.load(argv[1]);

        // 2. Check watch directory
        if (!filesystem::is_directory(config.watchDirectory)) {
            cerr << "Watch directory does not exist: "
                 << config.watchDirectory << "\n";
            return 1;
        }

        // 3. Create WatchFlow components
        InotifyFileWatcher fileWatcher(config.watchDirectory);
        RuleEngine engine;
        ConsoleLogger logger;

        optional<FileLogger> fileLogger;

        // Create file logger only if configured
        if (!config.logFile.empty()) {
            fileLogger.emplace(config.logFile);
        }

        // 4. Add rules to RuleEngine
        for (auto& rule : config.rules) {
            engine.addRule(move(rule));
        }

        // 5. Initialize Asynchronous Task Processing Components
        auto taskQueue = make_shared<TaskQueue>(config.queueCapacity);
        auto taskExecutor = make_shared<TaskExecutor>(
            &logger,
            fileLogger ? &(*fileLogger) : nullptr
        );
        ThreadPool threadPool(taskQueue, taskExecutor, config.workers);

        // 6. Handle Ctrl+C and termination
        signal(SIGINT, handleSignal);
        signal(SIGTERM, handleSignal);

        // 7. Display startup information
        cout << "WatchFlow started (Asynchronous Engine)\n";
        cout << "Watching: " << config.watchDirectory << "\n";
        cout << "Rules: " << config.rules.size() << "\n";
        cout << "Worker Threads: " << threadPool.workerCount() << "\n";
        cout << "Queue Capacity: " << config.queueCapacity << "\n";

        if (!config.logFile.empty()) {
            cout << "Log file: " << config.logFile << "\n";
        }

        cout << "Press Ctrl+C to stop.\n\n";

        // 8. Start watching files asynchronously
        fileWatcher.start([&](const FileEvent& event) {

            // Print event on console (thread-safe)
            logger.log(event);

            // Write event to file if enabled
            if (fileLogger) {
                fileLogger->logEvent(event);
            }

            // Match rules without blocking the event listener thread
            auto matchedRules = engine.match(event);

            for (const auto* rule : matchedRules) {
                Task task = createTask(*rule, event);
                taskQueue->push(std::move(task));
            }
        });

        // 9. Keep program running until signal
        while (keepRunning) {
            pause();
        }

        cout << "\nStopping WatchFlow gracefully...\n";

        // 10. Stop inotify file watcher (stops receiving new events)
        fileWatcher.stop();

        // 11. Gracefully drain task queue and join all worker threads
        threadPool.shutdown(true);

        // 12. Display execution summary
        cout << "\nWatchFlow shutdown complete.\n";
        cout << "Total Tasks Executed: " << taskExecutor->totalExecuted() << "\n";
        cout << "  Successful: " << taskExecutor->totalSucceeded() << "\n";
        cout << "  Failed:     " << taskExecutor->totalFailed() << "\n";
        cout << "  Retried:    " << taskExecutor->totalRetried() << "\n";
        cout << "Average Task Latency: " << taskExecutor->averageExecutionTimeMs() << " ms\n";

    }
    catch (const exception& ex) {
        cerr << "WatchFlow error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
