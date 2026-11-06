# Using the trace collector

`trace_collector.h` records timed events from a running program and writes them to a JSON file in the [Trace Event Format](https://docs.google.com/document/d/1CvAClvFfyA5R-PhYUmn5OOQtYMH4h6I0nSsKchNAySU). That format can be opened in [Perfetto](https://ui.perfetto.dev) and in Chrome's `chrome://tracing`, which show each event as a bar on a timeline.

The header has no dependencies beyond the C++ standard library, so it can be used in any application, not just the benchmarks in this directory. This guide shows how to add it to a new one.

## Quick start

Add `test/perf/common` to the include path and include the header:

```cpp
#include "trace_collector.h"

#include <string>
#include <thread>

int main() {
    // One collector per trace file. The pid must not equal any tid used below.
    TraceCollector tc("example_trace.json", /*pid=*/100, /*category=*/"example");

    // Name the tracks you will record on. A tid is any integer you choose.
    tc.add_thread_name(1, "reader");
    tc.add_thread_name(2, "worker");

    tc.record_instant_event("start", 1);

    std::thread reader([&tc] {
        for (int i = 0; i < 3; ++i) {
            ScopedTraceEvent ev(tc, "read_" + std::to_string(i), 1);
            read_chunk(i);                       // the work being timed
        }
    });
    std::thread worker([&tc] {
        for (int i = 0; i < 3; ++i) {
            tc.record_event("batch_" + std::to_string(i), "B", 2);
            {
                ScopedTraceEvent step(tc, "step", 2);   // nested inside the batch
                process(i);
            }
            tc.record_event("batch_" + std::to_string(i), "E", 2);
        }
    });
    reader.join();
    worker.join();

    tc.record_instant_event("done", 1);
    return 0;
}   // the collector's destructor writes example_trace.json
```

Run the program, then open `example_trace.json` in Perfetto. It shows a "reader" track with three `read_` bars, and a "worker" track with three `batch_` bars, each with a `step` bar nested inside it.

## Concepts

| Term | Meaning |
|---|---|
| **Track (`tid`)** | Each event is placed on a track identified by an integer `tid`. It is a label you choose, not an operating-system thread id. Use one track per thing you want to see on its own row, such as a pipeline stage or a worker. Name tracks with `add_thread_name`. **Number tracks from 1:** Perfetto merges a track with `tid` 0 into another thread ([tracing-chrome#32](https://github.com/thoren-d/tracing-chrome/issues/32)). |
| **Process (`pid`)** | Every event in the file carries the `pid` given to the constructor. Viewers group tracks under it. **Choose a `pid` that is not also one of your `tid`s:** viewers treat the track whose `tid` equals the `pid` as the process's main thread, and don't show it as an ordinary named track. |
| **Category** | One string attached to every event in the file. |
| **Timestamp** | Microseconds since the collector was constructed, measured with `std::chrono::steady_clock`. The construction time is the public member `start_time`. |

## Recording events

| Call | Records | Use it for |
|---|---|---|
| `ScopedTraceEvent ev(tc, name, tid);` | A complete event (`"X"`), covering the lifetime of `ev`. | Timing a block of code. This is the usual choice. |
| `ScopedTraceEvent ev(tc, name, tid, false);` | A begin event (`"B"`) when `ev` is created and an end event (`"E"`) when it is destroyed. | The same, as a begin/end pair. |
| `tc.record_event(name, "B", tid);` and `tc.record_event(name, "E", tid);` | A begin or end event, at the current time. | Intervals that don't match a C++ scope. |
| `tc.record_duration_event(name, tid, ts_us, dur_us);` | A complete event with a start and length you give, both in microseconds. `ts_us` is relative to `tc.start_time`. | Intervals measured some other way, or recorded after the fact. |
| `tc.record_instant_event(name, tid);` | A zero-length marker (`"i"`) at the current time. | Points in time, such as "started" or "message arrived". |
| `tc.add_thread_name(tid, name);` | The display name of a track. | Once per track, before or while recording. |

**Prefer complete events (`"X"`) over begin/end pairs.** A viewer matches each `"E"` with the most recent unmatched `"B"` on the same track, so begin/end pairs only display correctly when they nest. If two pieces of work overlap in time on the same track, as happens when several threads work under one `tid`, their pairs get mismatched. A complete event carries its own start and duration, so each one stays correct on its own. Overlapping events on one track are still drawn differently by different viewers; give work that runs concurrently separate tracks if you need a clean picture.

## Write modes

The fourth constructor argument selects when events reach the file:

| Mode | Behaviour | Choose it when |
|---|---|---|
| `TraceCollector::WriteMode::LAZY` (default) | Events are kept in memory and written, sorted by time, when the collector is destroyed or `flush()` is called. | You want the lowest overhead while the program runs. Memory grows with the number of events. |
| `TraceCollector::WriteMode::EAGER` | Each event is written and flushed to the file as soon as it is recorded. | The program may crash or hang and you want the events up to that point. Each event costs a file write. |

In eager mode, a program that dies before the collector is destroyed leaves a file missing only its closing `]`. The Trace Event Format allows that, so the file can still be loaded.

### Making sure the file is written

In lazy mode nothing is written until the collector is destroyed or `flush()` is called, so make sure one of those happens:

- **Destroy the collector normally.** Give it a scope that ends before the program exits, or own it with a `std::unique_ptr` that is reset.
- **`std::exit` skips the destructors of local objects.** If the program can exit that way, call `flush()` first, or register it with `std::atexit` through a global pointer to the collector.
- **`flush()` finishes the file.** It writes everything recorded so far, once. Events recorded after it are not written, so call it only when tracing is over.
- **Signals.** `workflow_runner.cpp` calls `flush()` from its `SIGTERM`/`SIGINT` handler so an interrupted lazy run still produces a trace. That is best effort: `flush()` allocates memory and takes a lock, which is not safe in a signal handler in general.

The output file is created, or overwritten, at the path given to the constructor. A relative path is relative to the current directory.

## Making tracing optional

To leave tracing off at run time, keep a possibly-null pointer to the collector and only create scoped events when it is set:

```cpp
void do_work(TraceCollector* tc, int tid) {
    std::unique_ptr<ScopedTraceEvent> ev;
    if (tc) ev = std::make_unique<ScopedTraceEvent>(*tc, "do_work", tid);
    // ... the work ...
}

int main(int argc, char* argv[]) {
    std::unique_ptr<TraceCollector> tc;
    if (tracing_requested(argc, argv)) {
        tc = std::make_unique<TraceCollector>("run.json", /*pid=*/100, "app");
    }
    do_work(tc.get(), 1);
}   // tc is destroyed here and, if tracing was on, writes run.json
```

To compile tracing out entirely, put the tracing code under a preprocessor switch. The benchmarks here use `USE_TRACE`: see `resource_common.h` and `workflow_runner.cpp`.

## Viewing the trace

- **Perfetto:** open [ui.perfetto.dev](https://ui.perfetto.dev), choose **Open trace file**, and select the JSON file. The trace is loaded in the browser and is not uploaded.
- **Chrome:** open `chrome://tracing` and use **Load**.

## Limitations

- **Names are not escaped.** Event and track names are written into the JSON exactly as given, so a name containing `"`, `\` or a control character such as a newline makes the file invalid. Use plain names.
- **No event arguments.** Events carry only a name, track, time and duration; the format's `args` field is not supported, nor are counter or flow events. To attach data to an event, put it in the name, for example `"config:threads=8:inputs=600"`.
- **One process name and one category** per file. Tracks can be named; the process is shown by its number.
- **Microsecond resolution.** Timestamps and durations are whole microseconds, so anything shorter than 1 µs has a duration of 0.
- **Every call takes a lock.** All methods are safe to call from any thread, but they serialise on one mutex. With many short events from many threads, the tracing itself can affect what you are measuring.
