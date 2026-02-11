# Threading Design: System Threads vs TBB Threads

## Overview

This document describes the architectural differences between Python's standard
`threading.Thread` (OS threads) and TBB's task-based threading model, how the
`TBBThread` implementation bridges these models, and what limitations exist.

This is relevant for [PR #1966](https://github.com/uxlfoundation/oneTBB/pull/1966)
which adds `patch_threading()` to replace `threading.Thread` with a TBB-backed
implementation for free-threaded Python (3.13t+).

## Background: Python Free-Threading

Starting with Python 3.13, CPython supports a "free-threaded" build (NOGIL) where
the Global Interpreter Lock is disabled, allowing true parallel execution. This makes
thread creation overhead more significant since threads now genuinely run in parallel.

TBBThread addresses this by reusing threads from TBB's worker pool instead of
creating new OS threads for each `threading.Thread` instance.

## Architecture Comparison

### System Threads (threading.Thread)

Python's `threading.Thread` maps 1:1 to OS threads:

- **Creation**: `pthread_create` / `CreateThread` per instance
- **Identity**: Stable `ident` + `native_id` for entire thread lifetime
- **TLS**: Natural per-thread isolation (OS-level Thread-Specific Data)
- **Lifecycle**: Explicit start → run → join → terminate
- **Daemon**: Built-in flag; non-daemon threads joined at shutdown
- **Signals**: Main thread receives signals (POSIX constraint)
- **Stack**: Configurable per-thread via `threading.stack_size()`

### TBB Threads (Task-Based Model)

TBB uses M:N scheduling — M tasks mapped onto N worker threads:

- **Creation**: Workers created lazily by runtime, reused across tasks
- **Identity**: Workers are anonymous; same worker runs different tasks
- **TLS**: Per-worker (OS thread), NOT per-task — data can leak between tasks
- **Lifecycle**: Runtime-controlled; workers exist for pool lifetime
- **Scheduling**: Non-preemptive; tasks run to completion
- **Arenas**: Control concurrency level, but workers can migrate between arenas

### Comparison Matrix

| Requirement | System Thread | TBB Worker | TBBThread |
|---|---|---|---|
| 1:1 thread-task mapping | ✅ Yes | ❌ No | ⚠️ Approximated |
| Stable identity | ✅ Stable | ❌ Reused | ⚠️ Set at task start, may be reused |
| TLS isolation | ✅ Natural | ❌ Per-worker | ⚠️ Per-worker (documented limitation) |
| Daemon support | ✅ Built-in | ❌ No concept | ⚠️ Property exists, limited effect |
| join() semantics | ✅ Per-thread | ❌ Task-group | ✅ Event-based per-task |
| Signal handling | ✅ Main thread | ❌ None | ✅ Main thread unchanged |
| Stack size | ✅ Per-thread | ⚠️ Global | ⚠️ TBB pool default |
| Shutdown integration | ✅ _shutdown() | ❌ Needs finalize() | ✅ atexit hook |
| Fork safety | ⚠️ Deprecated | ❌ Always unsafe | ❌ Inherits TBB limitation |
| Exception handling | ⚠️ excepthook | ✅ Captured at join | ✅ Re-raised in join() |

## How TBBThread Works

### Core Design

`TBBThread` submits work to a shared TBB `Pool` (backed by `task_group`) rather
than creating OS threads. This is conceptually similar to a thread pool executor
but with TBB's work-stealing scheduler.

```
threading.Thread (patched) → TBBThread
    └── Pool.apply_async(self._run_wrapper)
        └── TBB task_group.run(callable)
            └── TBB worker thread executes callable
```

### Identity Handling

Thread identity (`ident`, `native_id`) is captured when the task begins executing
on a TBB worker thread:

```python
def _run_wrapper(self):
    self._native_id = _thread.get_native_id()
    self._ident = _thread.get_ident()
    self._started.set()
    # ... run target ...
```

**Limitation**: Since TBB reuses workers, two sequential TBBThread instances may
report the same `native_id`. Code that uses thread identity as a unique key
(e.g., `threading.local()` internals) may see unexpected behavior.

### Synchronization

TBBThread uses `threading.Event` for lifecycle management:
- `_started`: Set when task begins executing on worker
- `_stopped`: Set when task completes (success or exception)
- `join(timeout)`: Waits on `_stopped` event with optional timeout

### Exception Propagation

Unlike standard `threading.Thread` (which calls `excepthook`), TBBThread captures
exceptions and re-raises them in `join()`:

```python
try:
    self._target(*self._args, **self._kwargs)
except Exception as e:
    self._exception = e.with_traceback(None)  # Strip traceback to prevent leaks
```

This is a deliberate design choice — it makes error handling more explicit and
Pythonic, at the cost of differing from stdlib behavior.

### Shutdown Integration

An `atexit` handler ensures cleanup:
1. Joins all non-daemon TBBThread instances (mirrors `threading._shutdown()`)
2. Shuts down the shared TBB pool

## Supported Features

| Feature | Status | Notes |
|---|---|---|
| `start()` / `join()` | ✅ Full | Event-based synchronization |
| `join(timeout)` | ✅ Full | Returns without blocking on timeout |
| `is_alive()` | ✅ Full | Based on started/stopped events |
| `name` property | ✅ Full | Get/set supported |
| `target`, `args`, `kwargs` | ✅ Full | Same constructor interface |
| `daemon` property | ⚠️ Partial | Property works; limited shutdown effect |
| `ident` / `native_id` | ⚠️ Partial | Set at execution time; may be reused |
| Exception in `join()` | ✅ Modified | Re-raised (differs from excepthook) |
| `patch_threading()` | ✅ Full | Global monkey-patch |
| `tbb_threading()` context | ✅ Full | Scoped patch with cleanup |
| `Monkey(threads=True)` | ✅ Full | Integrated with existing Monkey |

## Known Limitations

### 1. Thread-Local Storage (TLS)

`threading.local()` is implemented at the OS thread level. Since TBB workers are
reused across TBBThread instances, thread-local data from one TBBThread may be
visible to a subsequent TBBThread that runs on the same worker.

**Impact**: Libraries that cache per-thread state (connection pools, random
generators, CPython internal caches) may see stale data.

**Mitigation**: For critical per-task state, use `contextvars.ContextVar` instead
of `threading.local()`. Context variables are properly scoped in Python 3.14+.

### 2. Worker Pool Exhaustion

TBBThread tasks run on a shared, fixed-size worker pool. If all workers are
blocked (I/O, locks, `time.sleep()`), no new TBBThreads can start.

**Impact**: Deadlock-like behavior if N+1 TBBThreads are created where N is pool
size, and all N block waiting on the N+1th.

**Mitigation**: Use TBBThread for CPU-bound, short-lived tasks. For I/O-bound
work, use standard `threading.Thread` or `concurrent.futures.ThreadPoolExecutor`.

### 3. Identity Reuse

`native_id` and `ident` reflect the underlying TBB worker, not a unique logical
thread. Two sequential TBBThreads may have the same identity values.

**Impact**: Code using `threading.get_ident()` as a dict key for per-thread
resources may collide.

**Mitigation**: Use the TBBThread object itself as a key, not its `ident`.

### 4. Fork Safety

Forking a process with an active TBB thread pool leaves the child process with
invalid mutex state. This is a pre-existing TBB limitation, not specific to
TBBThread.

**Mitigation**: Use `multiprocessing.set_start_method('forkserver')` or `'spawn'`.

### 5. No OS Thread Name

Python 3.14+ sets the OS-level thread name via `pthread_setname_np`. Since
TBBThread doesn't create OS threads, this feature has no equivalent.

### 6. Trace/Profile Hooks

`sys.settrace()` and `sys.setprofile()` hooks are per-OS-thread. Since TBB
workers are reused, hooks set in one TBBThread may fire in another.

## Future Directions

Python may introduce pluggable threading backends (similar to how `asyncio`
supports different event loops). If CPython adds a formal `ThreadFactory` or
`ThreadBackend` protocol, TBBThread could integrate more cleanly without
monkey-patching.

Potential improvements:
- Per-task context variable isolation (Python 3.14+ `context` parameter)
- Integration with `concurrent.futures` for hybrid scheduling
- TBB arena-per-TBBThread for better isolation (at the cost of pool efficiency)
- Formal `threading._shutdown()` hook instead of atexit
