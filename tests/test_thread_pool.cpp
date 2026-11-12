#include "watchflow/async/ThreadPool.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace watchflow;

// Custom mock action for testing
class MockAction : public IAction {
public:
    explicit MockAction(std::function<bool(const FileEvent&)> fn)
        : fn_(std::move(fn)) {}

    bool execute(const FileEvent& event) override {
        return fn_(event);
    }

private:
    std::function<bool(const FileEvent&)> fn_;
};

void testThreadPoolWorkerExecution() {
    std::cout << "[TEST] Running testThreadPoolWorkerExecution...\n";
    auto queue = std::make_shared<TaskQueue>(100);
    auto executor = std::make_shared<TaskExecutor>();
    const size_t workerCount = 4;
    ThreadPool pool(queue, executor, workerCount);

    assert(pool.workerCount() == workerCount);

    const int totalTasks = 40;
    std::atomic<int> completedTasks{0};

    for (int i = 0; i < totalTasks; ++i) {
        Task t;
        t.id = i + 1;
        t.ruleName = "TestRule";
        t.filePath = "/tmp/dummy_" + std::to_string(i);
        t.action = std::make_shared<MockAction>([&completedTasks](const FileEvent&) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            completedTasks.fetch_add(1);
            return true;
        });
        queue->push(t);
    }

    pool.drain();

    assert(completedTasks.load() == totalTasks);
    assert(executor->totalExecuted() == static_cast<size_t>(totalTasks));
    assert(executor->totalSucceeded() == static_cast<size_t>(totalTasks));

    auto counts = pool.workerTaskCounts();
    size_t sum = 0;
    for (size_t c : counts) {
        sum += c;
    }
    assert(sum == static_cast<size_t>(totalTasks));

    pool.shutdown(false);
    assert(pool.isShutdown());
    std::cout << "  -> PASSED (" << totalTasks << " tasks distributed across " << workerCount << " workers)\n";
}

void testThreadPoolExceptionHandling() {
    std::cout << "[TEST] Running testThreadPoolExceptionHandling...\n";
    auto queue = std::make_shared<TaskQueue>(50);
    auto executor = std::make_shared<TaskExecutor>();
    ThreadPool pool(queue, executor, 2);

    std::atomic<int> normalSuccesses{0};

    // Push task that throws std::runtime_error
    Task tThrow;
    tThrow.id = 1;
    tThrow.ruleName = "ThrowRule";
    tThrow.filePath = "/tmp/throw.txt";
    tThrow.action = std::make_shared<MockAction>([](const FileEvent&) -> bool {
        throw std::runtime_error("Simulated catastrophic task failure");
    });
    queue->push(tThrow);

    // Push 5 normal tasks after the throwing task
    for (int i = 0; i < 5; ++i) {
        Task t;
        t.id = i + 2;
        t.ruleName = "NormalRule";
        t.filePath = "/tmp/normal_" + std::to_string(i);
        t.action = std::make_shared<MockAction>([&normalSuccesses](const FileEvent&) {
            normalSuccesses.fetch_add(1);
            return true;
        });
        queue->push(t);
    }

    pool.drain();

    // The throwing task should fail, but normal tasks must succeed!
    assert(executor->totalFailed() == 1);
    assert(normalSuccesses.load() == 5);
    assert(executor->totalSucceeded() == 5);

    pool.shutdown(false);
    std::cout << "  -> PASSED (worker thread survived exception)\n";
}

int main() {
    std::cout << "=== Running ThreadPool Unit Tests ===\n";
    testThreadPoolWorkerExecution();
    testThreadPoolExceptionHandling();
    std::cout << "All ThreadPool tests PASSED!\n\n";
    return 0;
}
