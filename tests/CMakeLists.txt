add_executable(test_task_queue test_task_queue.cpp)
target_link_libraries(test_task_queue PRIVATE watchflow_core Threads::Threads)
add_test(NAME TaskQueueTest COMMAND test_task_queue)

add_executable(test_thread_pool test_thread_pool.cpp)
target_link_libraries(test_thread_pool PRIVATE watchflow_core Threads::Threads)
add_test(NAME ThreadPoolTest COMMAND test_thread_pool)

add_executable(test_task_executor test_task_executor.cpp)
target_link_libraries(test_task_executor PRIVATE watchflow_core Threads::Threads)
add_test(NAME TaskExecutorTest COMMAND test_task_executor)

add_executable(test_watcher test_watcher.cpp)
target_link_libraries(test_watcher PRIVATE watchflow_core Threads::Threads)
add_test(NAME WatcherTest COMMAND test_watcher)

add_executable(test_async_execution test_async_execution.cpp)
target_link_libraries(test_async_execution PRIVATE watchflow_core Threads::Threads)
add_test(NAME AsyncExecutionTest COMMAND test_async_execution)

add_executable(benchmark_sync_vs_async benchmark.cpp)
target_link_libraries(benchmark_sync_vs_async PRIVATE watchflow_core Threads::Threads)
