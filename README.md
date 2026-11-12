# WatchFlow

WatchFlow is a lightweight Linux file watcher and automation engine written in
C++20. It watches one directory with `inotify`, turns filesystem changes into
events, matches those events against configurable rules, and runs actions such
as moving downloads, copying files, compressing large files, running commands,
or sending desktop notifications — all executed **asynchronously** via a
configurable thread pool.

```text
Linux inotify
  → InotifyFileWatcher
  → FileEvent
  → RuleEngine.match()
  → Task creation
  → TaskQueue (bounded, thread-safe)
  → ThreadPool (N worker threads)
  → TaskExecutor
  → CopyCommand / MoveCommand / CompressCommand / ExecuteCommand / NotificationCommand
  → ConsoleLogger / FileLogger
```

---

## Features

- **Asynchronous, event-driven** architecture — the inotify thread never blocks on file I/O.
- Configurable **thread pool** with per-task logging, retry logic, and per-file ordering.
- **Bounded task queue** with backpressure to prevent unbounded memory growth.
- **Duplicate event deduplication** via a configurable debounce window.
- `IN_CLOSE_WRITE` handling to avoid processing partially-written files.
- `IN_Q_OVERFLOW` recovery via directory rescan.
- Task lifecycle tracking: `Pending → Running → (Retrying →) Completed / Failed`.
- Per-task execution logging with queue wait time and duration reporting.
- Transient failure retry with configurable `maxRetries`.
- Thread-safe `ConsoleLogger` and `FileLogger`.
- **Graceful shutdown**: drains all pending tasks before joining worker threads.
- Execution summary on exit (tasks executed, succeeded, failed, retried, avg latency).
- YAML-style configuration (`*.yml` / `*.yaml`) with optional `workers` and `queue_capacity` fields.
- Legacy pipe-delimited `.conf` configuration still supported.
- Matchers: `extension`, `regex`, `size_gt`, `size_lt`, `size_eq`.
- Actions: `move`, `copy`, `execute`, `compress`, `notify`.
- Duplicate handling for move/copy: `overwrite`, `rename`, or `skip`.
- Optional `wait_until_complete` stability check before move/copy.

---

## Architecture

### Before (Synchronous)

```
inotify thread
    └─ callback(FileEvent)
          ├─ logger.log(event)
          ├─ engine.onEvent(event)       ← BLOCKS here
          │     └─ rule.execute(event)   ← file I/O inside callback
          └─ fileLogger.logResult()
```

The inotify loop **stopped receiving events** while each copy, move, or gzip ran.
Rapid bursts caused event drops.

### After (Asynchronous)

```
inotify thread                   Worker Threads (N)
    └─ callback(FileEvent)             │
          ├─ logger.log(event)         │
          ├─ engine.match(event)       │
          └─ for matched rule:         │
                createTask()           │
                taskQueue.push()  ──→  taskQueue.pop()
                                       └─ taskExecutor.execute(task)
                                             └─ action->execute(event)
                                                   (file I/O here)
```

The inotify thread **only does rule matching and queue submission**. Workers run file operations in parallel, never blocking the event listener.

### Component Responsibilities

| Component | Responsibility |
|---|---|
| `InotifyFileWatcher` | Receives inotify events, deduplicates, calls callback. Never performs file I/O. |
| `RuleEngine.match()` | Returns matched rules without executing actions. |
| `Task` | Carries all metadata for one automation operation. |
| `TaskQueue` | Thread-safe bounded producer-consumer queue. |
| `ThreadPool` | Fixed pool of worker threads consuming from TaskQueue. |
| `TaskExecutor` | Executes tasks via existing IAction implementations. Handles retries, per-file locking, logging. |
| `CommandFactory` | Factory producing `shared_ptr<IAction>` from config strings. |
| `IAction` / `ICommand` | Strategy interface; implementations unchanged. |
| `ConsoleLogger` / `FileLogger` | Thread-safe loggers. |

---

## Thread Synchronization

### TaskQueue

- `std::mutex` protects all queue state.
- `std::condition_variable cvNotEmpty_` — consumers wait here when empty.
- `std::condition_variable cvNotFull_` — producers wait here when at capacity.
- `shutdown()` sets the flag and calls `notify_all()` on both CVs.
- No busy-waiting anywhere.

### TaskExecutor per-file locking

- A `std::unordered_map<string, shared_ptr<mutex>>` maps file paths to locks.
- Before executing, the executor acquires the per-file mutex.
- Prevents two workers from running conflicting operations (e.g., copy + compress) on the same file simultaneously.
- The map mutex (`fileLocksMutex_`) is held only during lookup/insert, not during file I/O.

### InotifyFileWatcher

- Uses `eventfd` + `poll()` instead of a sleep loop.
- `stop()` writes to the eventfd, immediately waking `poll()`.
- Worker thread joins cleanly with no busy-spin or sleep.

---

## Requirements

- Linux
- GCC or Clang with C++20 support
- CMake 3.20 or newer
- `gzip` for the `compress` action
- `notify-send` from `libnotify-bin` for the `notify` action

```bash
sudo apt update
sudo apt install build-essential cmake gzip libnotify-bin
```

---

## Build

```bash
cmake -S . -B build
cmake --build build
```

The main executable:

```bash
./build/watchflow
```

Test executables under `./build/tests/`.

---

## Run

```bash
./build/watchflow <config-file>
```

Examples:

```bash
./build/watchflow examples/watchflow.conf
./build/watchflow downloads-sort.yml
```

Stop with `Ctrl+C`. WatchFlow drains the task queue before exiting.

---

## Quick Start

```bash
mkdir -p sandbox output/pdfs
./build/watchflow examples/watchflow.conf
```

In another terminal:

```bash
touch sandbox/report.pdf
```

The demo rule copies it to `output/pdfs/report.pdf`.

---

## YAML Configuration

```yaml
watch: /home/naman/Downloads
log_file: /home/naman/Music/WatchFlow/logs/downloads.log
duplicates: rename
workers: 4          # optional, default: hardware_concurrency
queue_capacity: 500 # optional, default: 1000

wait_until_complete:
  enabled: true
  stable_for_ms: 1500
  timeout_ms: 60000

rules:
  - name: Images
    events: [created, moved]
    matcher: extension
    extensions: [jpg, jpeg, png, gif, webp, bmp, svg]
    action: move
    destination: /home/naman/Pictures

  - name: PDFs
    events: [created, moved]
    matcher: extension
    extensions: [pdf]
    action: move
    destination: /home/naman/Documents/PDF
```

### Additional Async Config Fields

| Field | Type | Default | Description |
|---|---|---|---|
| `workers` | int | `hardware_concurrency` (min 2) | Number of worker threads in the pool. |
| `queue_capacity` | int | 1000 | Max pending tasks; producers block when full. |

---

## Legacy `.conf` Configuration

```text
WATCH|./sandbox
LOG|./logs/watchflow.log
DUPLICATES|rename
WORKERS|4
QUEUE_CAPACITY|500
WAIT_UNTIL_COMPLETE|true|1500|60000

RULE|Copy PDFs|CREATED|REGEX|.*\.pdf|COPY|./output/pdfs
RULE|Move PDFs|MOVED|EXT|pdf|MOVE|./output/pdfs
RULE|Compress large files|MODIFIED|SIZE_GT|1048576|COMPRESS|.gz
RULE|Notify PDFs|CREATED|REGEX|.*\.pdf|NOTIFY|New PDF: {file}
```

---

## Testing

Build and run all tests:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Run individual test suites:

```bash
./build/tests/test_task_queue       # TaskQueue: push/pop, concurrent producers/consumers, backpressure, shutdown
./build/tests/test_thread_pool      # ThreadPool: worker distribution, exception isolation
./build/tests/test_task_executor    # TaskExecutor: command integration, per-file sync, retry logic
./build/tests/test_watcher          # InotifyFileWatcher: debounce deduplication, rescan recovery
./build/tests/test_async_execution  # End-to-end: artificial delay speedup, full pipeline
```

### Test Coverage

| Test | What It Verifies |
|---|---|
| `test_task_queue` | Basic push/pop, 4-producer × 4-consumer concurrent correctness, bounded backpressure, timed push, graceful shutdown waking all waiters |
| `test_thread_pool` | 40 tasks distributed across 4 workers, per-worker counts, exception in one task does not kill other workers |
| `test_task_executor` | CopyCommand integration via CommandFactory, per-file mutex prevents concurrent conflicts on same file, transient retry succeeds, permanent failure marks task Failed |
| `test_watcher` | 5 rapid writes debounced to ≤2 events, rescanDirectory() finds all 3 pre-existing files |
| `test_async_execution` | 8 tasks with 50ms delay finish in ~100ms on 4 workers (vs ~400ms sequential); full e2e pipeline: inotify → match → queue → workers → copy files |

### ThreadSanitizer

```bash
cmake -S . -B build-tsan -DCMAKE_CXX_FLAGS="-fsanitize=thread -g"
cmake --build build-tsan
./build-tsan/tests/test_task_queue
./build-tsan/tests/test_thread_pool
./build-tsan/tests/test_task_executor
```

### AddressSanitizer + UBSanitizer

```bash
cmake -S . -B build-asan -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -g"
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

---

## Performance Benchmark

```bash
./build/tests/benchmark_sync_vs_async [file_count] [worker_count]
# Default: 100 files, 4 workers
./build/tests/benchmark_sync_vs_async 200 8
```

### Actual Results (100 files, 2 rules each — Copy + Compress, 4 workers)

| Metric | Synchronous | Asynchronous |
|---|---|---|
| Files Processed | 100 | 100 |
| Automation Tasks | 200 | 200 |
| Failed Tasks | 0 | 0 |
| Total Execution Time | 444 ms | 136 ms |
| Throughput | 450 tasks/sec | 1,471 tasks/sec |
| Avg Queue Wait Time | N/A | 67.72 ms |
| **Speedup Factor** | 1.00× (baseline) | **3.26×** |

**Worker utilization** (balanced distribution):

| Worker | Tasks | Share |
|---|---|---|
| Worker #0 | 54 | 27.0% |
| Worker #1 | 46 | 23.0% |
| Worker #2 | 48 | 24.0% |
| Worker #3 | 52 | 26.0% |

> Results measured on this machine with actual file copy + gzip operations.
> No artificial delays were used in this benchmark.

---

## Project Structure

```text
WatchFlow/
├── CMakeLists.txt
├── README.md
├── downloads-sort.yml
├── downloads-sort.conf
├── docs/
│   └── INSTRUCTIONS.md
├── examples/
│   └── watchflow.conf
├── include/watchflow/
│   ├── Watcher.hpp           ← convenience re-export
│   ├── RuleEngine.hpp        ← convenience re-export
│   ├── Task.hpp              ← convenience re-export
│   ├── TaskQueue.hpp         ← convenience re-export
│   ├── ThreadPool.hpp        ← convenience re-export
│   ├── TaskExecutor.hpp      ← convenience re-export
│   ├── action/
│   │   ├── IAction.hpp
│   │   ├── CommandFactory.hpp   ← NEW: factory for shared_ptr<IAction>
│   │   ├── CopyAction.hpp
│   │   ├── MoveAction.hpp
│   │   ├── CompressAction.hpp
│   │   ├── ExecuteAction.hpp
│   │   ├── NotificationAction.hpp
│   │   └── FileActionUtils.hpp
│   ├── async/
│   │   ├── Task.hpp             ← NEW: task abstraction + createTask()
│   │   ├── TaskQueue.hpp        ← NEW: thread-safe bounded queue
│   │   ├── TaskExecutor.hpp     ← NEW: per-file-locked async executor
│   │   └── ThreadPool.hpp       ← NEW: configurable worker pool
│   ├── config/
│   │   └── ConfigManager.hpp    ← MODIFIED: workers, queue_capacity fields
│   ├── core/
│   │   └── FileEvent.hpp
│   ├── logging/
│   │   ├── ConsoleLogger.hpp    ← MODIFIED: thread-safe, logMessage()
│   │   └── FileLogger.hpp       ← MODIFIED: thread-safe, logMessage()
│   ├── matcher/
│   │   ├── IMatcher.hpp
│   │   ├── ExtensionMatcher.hpp
│   │   ├── RegexMatcher.hpp
│   │   └── FileSizeMatcher.hpp
│   ├── rule/
│   │   ├── Rule.hpp             ← MODIFIED: shared_ptr<IAction>, action(), eventType()
│   │   └── RuleEngine.hpp       ← MODIFIED: match() separate from onEvent()
│   └── watcher/
│       ├── IFileWatcher.hpp
│       └── InotifyFileWatcher.hpp  ← MODIFIED: eventfd, debounce, rescan
├── src/
│   ├── main.cpp                 ← MODIFIED: async pipeline wired up
│   ├── async/
│   │   ├── Task.cpp             ← NEW
│   │   ├── TaskQueue.cpp        ← NEW
│   │   ├── TaskExecutor.cpp     ← NEW
│   │   └── ThreadPool.cpp       ← NEW
│   ├── action/  (unchanged)
│   ├── config/
│   │   └── ConfigManager.cpp    ← MODIFIED: workers, queue_capacity parsing
│   ├── core/    (unchanged)
│   ├── logging/
│   │   ├── ConsoleLogger.cpp    ← MODIFIED: mutex, logMessage()
│   │   └── FileLogger.cpp       ← MODIFIED: mutex, logMessage()
│   ├── matcher/ (unchanged)
│   ├── rule/
│   │   ├── Rule.cpp             ← MODIFIED: shared_ptr ctor, accessors
│   │   └── RuleEngine.cpp       ← MODIFIED: match() method
│   └── watcher/
│       └── InotifyFileWatcher.cpp  ← MODIFIED: eventfd, poll, debounce
└── tests/
    ├── CMakeLists.txt           ← NEW
    ├── test_task_queue.cpp      ← NEW
    ├── test_thread_pool.cpp     ← NEW
    ├── test_task_executor.cpp   ← NEW
    ├── test_watcher.cpp         ← NEW
    ├── test_async_execution.cpp ← NEW
    └── benchmark.cpp            ← NEW
```

---

## Security Notes

`EXECUTE` uses `std::system`, so it can run any command available to the user
running WatchFlow.

- Only run trusted config files.
- Quote `{file}` in shell commands when paths may contain spaces.
- Do not use WatchFlow as a hardened multi-user automation service.
- Prefer native `move` and `copy` actions over shell commands for file sorting.

---

## Known Limitations and Future Work

- **One watched directory per process** — no recursive watching yet.
- **Linux only** — inotify is a Linux kernel interface.
- **YAML subset** — not a full YAML parser; only the documented shape is supported.
- `waitUntilFileStable()` blocks a worker thread during stability polling. For very long downloads, consider a dedicated stability-check thread.
- No persistent job database — tasks in the queue are lost on crash/kill.
- No daemon/systemd installer or GUI.
- Task priority field exists in `Task` but the queue does not yet sort by priority (straightforward upgrade to `std::priority_queue`).
- Per-file lock map grows indefinitely; could be periodically pruned.

---

## License

MIT License.
