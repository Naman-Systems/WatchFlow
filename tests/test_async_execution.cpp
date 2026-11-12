#include "watchflow/action/CommandFactory.hpp"
#include "watchflow/async/Task.hpp"
#include "watchflow/async/TaskExecutor.hpp"
#include "watchflow/async/TaskQueue.hpp"
#include "watchflow/async/ThreadPool.hpp"
#include "watchflow/matcher/ExtensionMatcher.hpp"
#include "watchflow/rule/Rule.hpp"
#include "watchflow/rule/RuleEngine.hpp"
#include "watchflow/watcher/InotifyFileWatcher.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

using namespace watchflow;

void testConcurrentExecutionSpeedup() {
    std::cout << "[TEST] Running testConcurrentExecutionSpeedup (Artificial Delay Benchmark)...\n";
    
    // We execute 8 tasks, each taking 50ms artificial delay.
    // Sequential execution: 8 * 50ms = 400ms.
    // 4-thread pool: ~100ms - 150ms.
    const int numTasks = 8;
    const int delayMs = 50;

    class DelayedAction : public IAction {
    public:
        explicit DelayedAction(int delay) : delay_(delay) {}
        bool execute(const FileEvent&) override {
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_));
            return true;
        }
    private:
        int delay_;
    };

    auto queue = std::make_shared<TaskQueue>(100);
    auto executor = std::make_shared<TaskExecutor>();
    const size_t numWorkers = 4;
    ThreadPool pool(queue, executor, numWorkers);

    for (int i = 0; i < numTasks; ++i) {
        Task t;
        t.id = i + 1;
        t.ruleName = "DelayRule";
        t.filePath = "/tmp/delay_" + std::to_string(i);
        t.action = std::make_shared<DelayedAction>(delayMs);
        queue->push(t);
    }

    const auto start = std::chrono::steady_clock::now();
    pool.drain();
    const auto end = std::chrono::steady_clock::now();

    const auto totalDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::cout << "  8 tasks with " << delayMs << "ms delay executed in: " << totalDurationMs << "ms across " << numWorkers << " workers\n";
    std::cout << "  (Sequential baseline would be ~" << (numTasks * delayMs) << "ms)\n";

    assert(totalDurationMs < (numTasks * delayMs * 3 / 4)); // Confirms multithreaded speedup
    assert(executor->totalSucceeded() == static_cast<size_t>(numTasks));

    pool.shutdown(false);
    std::cout << "  -> PASSED\n";
}

void testEndToEndAsyncWatcherPipeline() {
    std::cout << "[TEST] Running testEndToEndAsyncWatcherPipeline...\n";

    const std::filesystem::path testDir = "/tmp/watchflow_test_e2e";
    const std::filesystem::path watchDir = testDir / "watch";
    const std::filesystem::path outputDir = testDir / "out";

    std::filesystem::remove_all(testDir);
    std::filesystem::create_directories(watchDir);
    std::filesystem::create_directories(outputDir);

    // Set up RuleEngine
    RuleEngine engine;
    auto copyAction = CommandFactory::createCommand("COPY", outputDir.string());
    Rule rule(
        "CopyTxtFiles",
        EventType::Modified,
        std::make_unique<ExtensionMatcher>(std::vector<std::string>{"txt"}),
        copyAction
    );
    engine.addRule(std::move(rule));

    auto taskQueue = std::make_shared<TaskQueue>(100);
    auto taskExecutor = std::make_shared<TaskExecutor>();
    ThreadPool threadPool(taskQueue, taskExecutor, 4);

    InotifyFileWatcher watcher(watchDir, std::chrono::milliseconds(20));

    watcher.start([&engine, &taskQueue](const FileEvent& event) {
        auto matchedRules = engine.match(event);
        for (const auto* r : matchedRules) {
            Task task = createTask(*r, event);
            taskQueue->push(std::move(task));
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Create 5 simultaneous file events
    const int fileCount = 5;
    for (int i = 0; i < fileCount; ++i) {
        std::ofstream out(watchDir / ("doc" + std::to_string(i) + ".txt"));
        out << "Async pipeline test file content " << i << "\n";
    }

    // Wait for inotify + async task processing
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    threadPool.drain();
    watcher.stop();
    threadPool.shutdown(true);

    // Verify all files were copied to output directory
    for (int i = 0; i < fileCount; ++i) {
        const auto copiedPath = outputDir / ("doc" + std::to_string(i) + ".txt");
        assert(std::filesystem::exists(copiedPath));
    }

    assert(taskExecutor->totalSucceeded() >= static_cast<size_t>(fileCount));
    std::filesystem::remove_all(testDir);
    std::cout << "  -> PASSED (all " << fileCount << " files monitored, matched, queued, and executed asynchronously)\n";
}

int main() {
    std::cout << "=== Running End-to-End Async Execution Tests ===\n";
    testConcurrentExecutionSpeedup();
    testEndToEndAsyncWatcherPipeline();
    std::cout << "All End-to-End Async tests PASSED!\n\n";
    return 0;
}
