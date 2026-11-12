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

void testWatcherDeduplication() {
    std::cout << "[TEST] Running testWatcherDeduplication...\n";
    const std::filesystem::path watchDir = "/tmp/watchflow_test_dedupe";
    std::filesystem::remove_all(watchDir);
    std::filesystem::create_directories(watchDir);

    InotifyFileWatcher watcher(watchDir, std::chrono::milliseconds(150));
    std::atomic<int> eventCount{0};

    watcher.start([&eventCount](const FileEvent&) {
        eventCount.fetch_add(1);
    });

    // Give watcher thread a moment to start poll
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Rapid writes to the same file in rapid succession
    const std::filesystem::path testFile = watchDir / "rapid.txt";
    for (int i = 0; i < 5; ++i) {
        std::ofstream out(testFile, std::ios::app);
        out << "chunk " << i << "\n";
        out.close();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Wait past debounce window
    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    watcher.stop();
    std::filesystem::remove_all(watchDir);

    // Without deduplication, 5 writes would generate 10+ inotify events (CREATE/MODIFY/CLOSE_WRITE)
    // With 150ms deduplication, it should be debounced to very few events (e.g. 1-2).
    std::cout << "  Total debounced events received: " << eventCount.load() << "\n";
    assert(eventCount.load() >= 1 && eventCount.load() < 5);
    std::cout << "  -> PASSED (rapid events successfully debounced)\n";
}

void testWatcherRescanRecovery() {
    std::cout << "[TEST] Running testWatcherRescanRecovery...\n";
    const std::filesystem::path watchDir = "/tmp/watchflow_test_rescan";
    std::filesystem::remove_all(watchDir);
    std::filesystem::create_directories(watchDir);

    // Create 3 files prior to rescan
    for (int i = 1; i <= 3; ++i) {
        std::ofstream out(watchDir / ("file" + std::to_string(i) + ".txt"));
        out << "content " << i;
    }

    InotifyFileWatcher watcher(watchDir);
    std::vector<std::filesystem::path> rescannedFiles;

    watcher.rescanDirectory([&rescannedFiles](const FileEvent& event) {
        rescannedFiles.push_back(event.path);
    });

    assert(rescannedFiles.size() == 3);
    std::filesystem::remove_all(watchDir);
    std::cout << "  -> PASSED (directory rescan recovered all " << rescannedFiles.size() << " files)\n";
}

int main() {
    std::cout << "=== Running InotifyFileWatcher Unit Tests ===\n";
    testWatcherDeduplication();
    testWatcherRescanRecovery();
    std::cout << "All InotifyFileWatcher tests PASSED!\n\n";
    return 0;
}
