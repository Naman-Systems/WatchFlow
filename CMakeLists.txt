cmake_minimum_required(VERSION 3.20)
project(WatchFlow VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

set(WATCHFLOW_COMMON_SOURCES
    src/core/FileEvent.cpp
    src/watcher/InotifyFileWatcher.cpp
    src/matcher/RegexMatcher.cpp
    src/matcher/FileSizeMatcher.cpp
    src/matcher/ExtensionMatcher.cpp
    src/action/CopyAction.cpp
    src/action/FileActionUtils.cpp
    src/action/MoveAction.cpp
    src/action/ExecuteAction.cpp
    src/action/CompressAction.cpp
    src/action/NotificationAction.cpp
    src/rule/Rule.cpp
    src/rule/RuleEngine.cpp
    src/config/ConfigManager.cpp
    src/logging/ConsoleLogger.cpp
    src/logging/FileLogger.cpp
    src/async/Task.cpp
    src/async/TaskQueue.cpp
    src/async/TaskExecutor.cpp
    src/async/ThreadPool.cpp
)

add_library(watchflow_core STATIC
    ${WATCHFLOW_COMMON_SOURCES}
)

target_include_directories(watchflow_core PUBLIC include)
target_link_libraries(watchflow_core PUBLIC Threads::Threads)
target_compile_options(watchflow_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(watchflow
    src/main.cpp
)

target_link_libraries(watchflow PRIVATE watchflow_core Threads::Threads)
target_compile_options(watchflow PRIVATE -Wall -Wextra -Wpedantic)

# Alias for target if needed
add_library(WatchFlow ALIAS watchflow_core)

# Testing
enable_testing()
add_subdirectory(tests)
