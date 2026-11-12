#include "watchflow/action/CommandFactory.hpp"
#include "watchflow/action/CompressAction.hpp"
#include "watchflow/action/CopyAction.hpp"
#include "watchflow/action/ExecuteAction.hpp"
#include "watchflow/async/Task.hpp"
#include "watchflow/async/TaskExecutor.hpp"
#include "watchflow/async/TaskQueue.hpp"
#include "watchflow/async/ThreadPool.hpp"
#include "watchflow/matcher/ExtensionMatcher.hpp"
#include "watchflow/rule/Rule.hpp"
#include "watchflow/rule/RuleEngine.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <vector>

using namespace watchflow;

struct BenchmarkResults {
    std::string mode;
    size_t filesProcessed{0};
    size_t tasksExecuted{0};
    size_t tasksFailed{0};
    int64_t totalDurationMs{0};
    double throughputTasksPerSec{0.0};
    double avgQueueWaitMs{0.0};
    std::vector<size_t> workerTasks;
};

void printResultsTable(const BenchmarkResults& syncRes, const BenchmarkResults& asyncRes) {
    std::cout << "\n=========================================================================\n";
    std::cout << "                   WATCHFLOW PERFORMANCE BENCHMARK                       \n";
    std::cout << "             Synchronous Baseline vs. Asynchronous Engine                \n";
    std::cout << "=========================================================================\n";
    std::cout << std::left
              << std::setw(32) << "Metric"
              << std::setw(20) << "Synchronous"
              << std::setw(20) << "Asynchronous" << "\n";
    std::cout << "-------------------------------------------------------------------------\n";

    std::cout << std::left
              << std::setw(32) << "Files Processed"
              << std::setw(20) << syncRes.filesProcessed
              << std::setw(20) << asyncRes.filesProcessed << "\n";

    std::cout << std::left
              << std::setw(32) << "Automation Tasks"
              << std::setw(20) << syncRes.tasksExecuted
              << std::setw(20) << asyncRes.tasksExecuted << "\n";

    std::cout << std::left
              << std::setw(32) << "Failed Tasks"
              << std::setw(20) << syncRes.tasksFailed
              << std::setw(20) << asyncRes.tasksFailed << "\n";

    std::cout << std::left
              << std::setw(32) << "Total Execution Time (ms)"
              << std::setw(20) << syncRes.totalDurationMs
              << std::setw(20) << asyncRes.totalDurationMs << "\n";

    std::cout << std::left
              << std::setw(32) << "Throughput (tasks/sec)"
              << std::setw(20) << std::fixed << std::setprecision(2) << syncRes.throughputTasksPerSec
              << std::setw(20) << std::fixed << std::setprecision(2) << asyncRes.throughputTasksPerSec << "\n";

    std::cout << std::left
              << std::setw(32) << "Avg Queue Wait Time (ms)"
              << std::setw(20) << "N/A (no queue)"
              << std::setw(20) << std::fixed << std::setprecision(2) << asyncRes.avgQueueWaitMs << "\n";

    double speedup = (asyncRes.totalDurationMs > 0)
        ? static_cast<double>(syncRes.totalDurationMs) / static_cast<double>(asyncRes.totalDurationMs)
        : 1.0;

    std::cout << std::left
              << std::setw(32) << "Speedup Factor"
              << std::setw(20) << "1.00x (baseline)"
              << std::setw(20) << (std::to_string(speedup).substr(0, 4) + "x") << "\n";

    std::cout << "-------------------------------------------------------------------------\n";
    std::cout << "Worker Distribution (Async):\n";
    for (size_t i = 0; i < asyncRes.workerTasks.size(); ++i) {
        double pct = (asyncRes.tasksExecuted > 0)
            ? (100.0 * asyncRes.workerTasks[i] / asyncRes.tasksExecuted)
            : 0.0;
        std::cout << "  Worker #" << i << ": " << asyncRes.workerTasks[i]
                  << " tasks (" << std::fixed << std::setprecision(1) << pct << "%)\n";
    }
    std::cout << "=========================================================================\n\n";
}

int main(int argc, char* argv[]) {
    const size_t fileCount = (argc > 1) ? std::stoul(argv[1]) : 50;
    const size_t numWorkers = (argc > 2) ? std::stoul(argv[2]) : 4;

    std::cout << "Running WatchFlow Benchmark with " << fileCount
              << " workload files and " << numWorkers << " async workers...\n\n";

    const std::filesystem::path benchmarkDir = "/tmp/watchflow_benchmark";
    const std::filesystem::path syncSrcDir = benchmarkDir / "sync_src";
    const std::filesystem::path syncOutDir = benchmarkDir / "sync_out";
    const std::filesystem::path asyncSrcDir = benchmarkDir / "async_src";
    const std::filesystem::path asyncOutDir = benchmarkDir / "async_out";

    std::filesystem::remove_all(benchmarkDir);
    std::filesystem::create_directories(syncSrcDir);
    std::filesystem::create_directories(syncOutDir);
    std::filesystem::create_directories(asyncSrcDir);
    std::filesystem::create_directories(asyncOutDir);

    // Prepare identical workload files
    std::vector<std::string> filenames;
    for (size_t i = 0; i < fileCount; ++i) {
        std::string fname = "data_" + std::to_string(i) + ".txt";
        filenames.push_back(fname);

        std::string content = "Sample benchmark dataset line for file index " + std::to_string(i) + "\n";
        for (int k = 0; k < 50; ++k) content += "Repeated line for realistic file size payload.\n";

        {
            std::ofstream out(syncSrcDir / fname);
            out << content;
        }
        {
            std::ofstream out(asyncSrcDir / fname);
            out << content;
        }
    }

    // --- Benchmark 1: Synchronous Engine ---
    BenchmarkResults syncRes;
    syncRes.mode = "Synchronous";
    syncRes.filesProcessed = fileCount;

    RuleEngine syncEngine;
    auto syncCopy = std::make_shared<CopyAction>(syncOutDir);
    auto syncCompress = std::make_shared<CompressAction>(".gz");
    Rule r1("CopyTxt", EventType::Created,
            std::make_unique<ExtensionMatcher>(std::vector<std::string>{"txt"}), syncCopy);
    Rule r2("CompressTxt", EventType::Created,
            std::make_unique<ExtensionMatcher>(std::vector<std::string>{"txt"}), syncCompress);
    syncEngine.addRule(std::move(r1));
    syncEngine.addRule(std::move(r2));

    const auto syncStart = std::chrono::steady_clock::now();
    for (const auto& fname : filenames) {
        FileEvent ev{EventType::Created, syncSrcDir / fname};
        auto results = syncEngine.onEvent(ev);
        syncRes.tasksExecuted += results.size();
        for (const auto& [name, ok] : results) {
            if (!ok) ++syncRes.tasksFailed;
        }
    }
    const auto syncEnd = std::chrono::steady_clock::now();
    syncRes.totalDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        syncEnd - syncStart).count();
    syncRes.throughputTasksPerSec = (syncRes.totalDurationMs > 0)
        ? (static_cast<double>(syncRes.tasksExecuted) * 1000.0 / syncRes.totalDurationMs)
        : 0.0;

    // --- Benchmark 2: Asynchronous Engine ---
    BenchmarkResults asyncRes;
    asyncRes.mode = "Asynchronous";
    asyncRes.filesProcessed = fileCount;

    RuleEngine asyncEngine;
    auto asyncCopy = std::make_shared<CopyAction>(asyncOutDir);
    auto asyncCompress = std::make_shared<CompressAction>(".gz");
    Rule ar1("CopyTxt", EventType::Created,
             std::make_unique<ExtensionMatcher>(std::vector<std::string>{"txt"}), asyncCopy);
    Rule ar2("CompressTxt", EventType::Created,
             std::make_unique<ExtensionMatcher>(std::vector<std::string>{"txt"}), asyncCompress);
    asyncEngine.addRule(std::move(ar1));
    asyncEngine.addRule(std::move(ar2));

    auto taskQueue = std::make_shared<TaskQueue>(1000);
    auto taskExecutor = std::make_shared<TaskExecutor>();
    ThreadPool pool(taskQueue, taskExecutor, numWorkers);

    std::vector<int64_t> queueWaitTimes;
    std::mutex queueWaitMutex;

    taskExecutor->setOnCompleteCallback([&queueWaitTimes, &queueWaitMutex](const Task& task, bool) {
        std::lock_guard<std::mutex> lock(queueWaitMutex);
        queueWaitTimes.push_back(task.queueWaitTimeMs());
    });

    const auto asyncStart = std::chrono::steady_clock::now();
    for (const auto& fname : filenames) {
        FileEvent ev{EventType::Created, asyncSrcDir / fname};
        auto matched = asyncEngine.match(ev);
        for (const auto* r : matched) {
            Task task = createTask(*r, ev);
            taskQueue->push(std::move(task));
        }
    }

    pool.drain();
    const auto asyncEnd = std::chrono::steady_clock::now();

    asyncRes.tasksExecuted = taskExecutor->totalExecuted();
    asyncRes.tasksFailed = taskExecutor->totalFailed();
    asyncRes.totalDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        asyncEnd - asyncStart).count();
    asyncRes.throughputTasksPerSec = (asyncRes.totalDurationMs > 0)
        ? (static_cast<double>(asyncRes.tasksExecuted) * 1000.0 / asyncRes.totalDurationMs)
        : 0.0;
    asyncRes.workerTasks = pool.workerTaskCounts();

    if (!queueWaitTimes.empty()) {
        int64_t sumWait = std::accumulate(queueWaitTimes.begin(), queueWaitTimes.end(), 0LL);
        asyncRes.avgQueueWaitMs = static_cast<double>(sumWait) / static_cast<double>(queueWaitTimes.size());
    }

    pool.shutdown(false);
    std::filesystem::remove_all(benchmarkDir);

    printResultsTable(syncRes, asyncRes);
    return 0;
}
