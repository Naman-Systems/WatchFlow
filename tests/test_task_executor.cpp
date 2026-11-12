#include "watchflow/action/CommandFactory.hpp"
#include "watchflow/async/TaskExecutor.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

using namespace watchflow;

class MockRetryAction : public IAction {
public:
    explicit MockRetryAction(int failTimes)
        : failTimes_(failTimes) {}

    bool execute(const FileEvent&) override {
        if (attempts_++ < failTimes_) {
            return false;
        }
        return true;
    }

    int attempts() const { return attempts_; }

private:
    int failTimes_;
    int attempts_{0};
};

void testTaskExecutorCommandIntegration() {
    std::cout << "[TEST] Running testTaskExecutorCommandIntegration...\n";
    TaskExecutor executor;

    // Create a temporary test file
    const std::filesystem::path testDir = "/tmp/watchflow_test_exec";
    const std::filesystem::path srcFile = testDir / "source.txt";
    const std::filesystem::path destDir = testDir / "dest";

    std::filesystem::remove_all(testDir);
    std::filesystem::create_directories(testDir);

    {
        std::ofstream out(srcFile);
        out << "Hello WatchFlow TaskExecutor!";
    }

    // Test CopyCommand via CommandFactory
    auto copyCmd = CommandFactory::createCommand("COPY", destDir.string());
    assert(copyCmd != nullptr);

    Task task;
    task.id = 1;
    task.ruleName = "CopySource";
    task.filePath = srcFile;
    task.eventType = EventType::Created;
    task.action = copyCmd;

    bool result = executor.execute(task);
    assert(result);
    assert(task.status == TaskStatus::Completed);
    assert(std::filesystem::exists(destDir / "source.txt"));

    std::filesystem::remove_all(testDir);
    std::cout << "  -> PASSED\n";
}

void testPerFileSynchronization() {
    std::cout << "[TEST] Running testPerFileSynchronization...\n";
    TaskExecutor executor;

    const std::filesystem::path sharedFile = "/tmp/shared_file.txt";
    std::atomic<int> concurrentExecutorsOnSameFile{0};
    std::atomic<bool> conflictDetected{false};

    class SynchronizedCheckAction : public IAction {
    public:
        SynchronizedCheckAction(std::atomic<int>& count, std::atomic<bool>& conflict)
            : count_(count), conflict_(conflict) {}

        bool execute(const FileEvent&) override {
            int current = ++count_;
            if (current > 1) {
                // More than 1 thread is operating on the SAME file at the same time!
                conflict_.store(true);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            --count_;
            return true;
        }

    private:
        std::atomic<int>& count_;
        std::atomic<bool>& conflict_;
    };

    const int numTasks = 10;
    std::vector<std::thread> threads;

    for (int i = 0; i < numTasks; ++i) {
        threads.emplace_back([&executor, sharedFile, &concurrentExecutorsOnSameFile, &conflictDetected, i]() {
            Task t;
            t.id = i;
            t.ruleName = "PerFileSyncRule";
            t.filePath = sharedFile; // Same file for all tasks
            t.action = std::make_shared<SynchronizedCheckAction>(
                concurrentExecutorsOnSameFile, conflictDetected);
            executor.execute(t);
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    assert(!conflictDetected.load());
    assert(executor.totalExecuted() == static_cast<size_t>(numTasks));
    std::cout << "  -> PASSED (per-file mutex prevented concurrent conflicting operations)\n";
}

void testTransientRetryLogic() {
    std::cout << "[TEST] Running testTransientRetryLogic...\n";
    TaskExecutor executor;

    // Case 1: Fails once, succeeds on retry 1
    {
        auto retryAction = std::make_shared<MockRetryAction>(1);
        Task t;
        t.id = 1;
        t.ruleName = "TransientRule";
        t.filePath = "/tmp/transient.txt";
        t.action = retryAction;
        t.maxRetries = 2;

        bool success = executor.execute(t);
        assert(success);
        assert(t.status == TaskStatus::Completed);
        assert(t.retryCount == 1);
        assert(retryAction->attempts() == 2);
    }

    // Case 2: Fails permanently (more than maxRetries)
    {
        auto permFailAction = std::make_shared<MockRetryAction>(10);
        Task t;
        t.id = 2;
        t.ruleName = "PermFailRule";
        t.filePath = "/tmp/permfail.txt";
        t.action = permFailAction;
        t.maxRetries = 2;

        bool success = executor.execute(t);
        assert(!success);
        assert(t.status == TaskStatus::Failed);
        assert(t.retryCount == 2);
    }

    std::cout << "  -> PASSED\n";
}

int main() {
    std::cout << "=== Running TaskExecutor Unit Tests ===\n";
    testTaskExecutorCommandIntegration();
    testPerFileSynchronization();
    testTransientRetryLogic();
    std::cout << "All TaskExecutor tests PASSED!\n\n";
    return 0;
}
