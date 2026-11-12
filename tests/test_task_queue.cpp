#include "watchflow/async/TaskQueue.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

using namespace watchflow;

void testBasicPushPop() {
    std::cout << "[TEST] Running testBasicPushPop...\n";
    TaskQueue queue(10);

    assert(queue.empty());
    assert(queue.size() == 0);
    assert(queue.capacity() == 10);

    Task t1;
    t1.id = 1;
    t1.filePath = "/tmp/test1.txt";

    bool pushed = queue.push(t1);
    assert(pushed);
    assert(!queue.empty());
    assert(queue.size() == 1);

    Task popped;
    bool popSuccess = queue.pop(popped);
    assert(popSuccess);
    assert(popped.id == 1);
    assert(popped.filePath == "/tmp/test1.txt");
    assert(queue.empty());
    std::cout << "  -> PASSED\n";
}

void testMultipleProducersConsumers() {
    std::cout << "[TEST] Running testMultipleProducersConsumers...\n";
    TaskQueue queue(500);

    const int numProducers = 4;
    const int numConsumers = 4;
    const int tasksPerProducer = 250;
    const int totalTasks = numProducers * tasksPerProducer;

    std::atomic<int> tasksConsumed{0};
    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;

    // Launch consumers
    for (int i = 0; i < numConsumers; ++i) {
        consumers.emplace_back([&queue, &tasksConsumed, totalTasks]() {
            while (tasksConsumed.load() < totalTasks) {
                Task t;
                if (queue.pop(t)) {
                    tasksConsumed.fetch_add(1);
                } else {
                    break;
                }
            }
        });
    }

    // Launch producers
    for (int p = 0; p < numProducers; ++p) {
        producers.emplace_back([&queue, p, tasksPerProducer]() {
            for (int i = 0; i < tasksPerProducer; ++i) {
                Task t;
                t.id = static_cast<uint64_t>(p * 1000 + i);
                queue.push(t);
            }
        });
    }

    for (auto& t : producers) {
        t.join();
    }

    // Wait until all are consumed or queue shuts down
    while (tasksConsumed.load() < totalTasks && !queue.empty()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    queue.shutdown();
    for (auto& t : consumers) {
        t.join();
    }

    assert(tasksConsumed.load() == totalTasks);
    assert(queue.totalPushed() == static_cast<size_t>(totalTasks));
    assert(queue.totalPopped() == static_cast<size_t>(totalTasks));
    std::cout << "  -> PASSED (" << totalTasks << " tasks processed concurrently)\n";
}

void testBackpressureAndCapacity() {
    std::cout << "[TEST] Running testBackpressureAndCapacity...\n";
    const size_t capacity = 3;
    TaskQueue queue(capacity);

    for (size_t i = 0; i < capacity; ++i) {
        Task t;
        t.id = i;
        assert(queue.push(t));
    }

    assert(queue.size() == capacity);

    // try_push should fail because queue is at capacity
    Task overflowTask;
    overflowTask.id = 999;
    assert(!queue.try_push(overflowTask));

    // Timed push with short timeout should fail
    assert(!queue.push(overflowTask, std::chrono::milliseconds(50)));

    // Pop one task in background thread after 100ms
    std::thread backgroundPopper([&queue]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        Task t;
        queue.pop(t);
    });

    // Now push should unblock and succeed once space is made
    assert(queue.push(overflowTask, std::chrono::milliseconds(500)));
    backgroundPopper.join();

    std::cout << "  -> PASSED\n";
}

void testGracefulShutdown() {
    std::cout << "[TEST] Running testGracefulShutdown...\n";
    TaskQueue queue(10);

    // Push 2 tasks
    Task t1, t2;
    t1.id = 1;
    t2.id = 2;
    queue.push(t1);
    queue.push(t2);

    std::atomic<bool> workerWoken{false};
    std::atomic<int> poppedCount{0};

    std::thread consumer([&queue, &workerWoken, &poppedCount]() {
        Task t;
        while (queue.pop(t)) {
            poppedCount.fetch_add(1);
        }
        workerWoken.store(true);
    });

    // Give consumer time to pop existing items and wait on empty queue
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(poppedCount.load() == 2);

    // Calling shutdown must wake the waiting consumer
    queue.shutdown();
    consumer.join();

    assert(workerWoken.load());
    assert(queue.isShutdown());

    // Pushing after shutdown should fail
    Task t3;
    t3.id = 3;
    assert(!queue.push(t3));

    std::cout << "  -> PASSED\n";
}

int main() {
    std::cout << "=== Running TaskQueue Unit Tests ===\n";
    testBasicPushPop();
    testMultipleProducersConsumers();
    testBackpressureAndCapacity();
    testGracefulShutdown();
    std::cout << "All TaskQueue tests PASSED!\n\n";
    return 0;
}
