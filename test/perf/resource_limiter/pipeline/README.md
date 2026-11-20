# Overview of pipeline

The `pipeline` performance benchmark checks how well resource limiting shares two resources along a chain of three nodes, where the last node needs both resources and each of the others needs one. It builds the same graph with two implementations of resource limiting, a join-based emulation and `resource_limiter` with `resource_limited_node`, and feeds each one messages at the fastest rate the graph can sustain. Both variants run one after the other in a single executable.

It uses the same three resource-holding nodes as [offset_puts](../offset_puts/README.md), but connects them in a pipeline. The time at which each node receives a message therefore comes from the graph itself, not from fixed put times.

## The graph

Two resources, `a` and `b`, have a single instance each. Every message passes through five nodes in turn:

1. **`input`**, an `input_node`, generates the message numbers 0 to 99.
2. **`pacer`**, a serial `function_node` with the `rejecting` policy, busy-waits for 11 ms and passes the message on. Because it rejects messages while it is busy, the input node holds its next message until the pacer is free, so messages enter the rest of the graph at most once every 11 ms.
3. **`consumer_of_a`**, **`consumer_of_b`** and **`consumer_of_a_and_b`** hold resources:

| Node | Resources it holds | Body time |
|---|---|---|
| `consumer_of_a` | `a` | 10 ms |
| `consumer_of_b` | `b` | 10 ms |
| `consumer_of_a_and_b` | `a` and `b` | 1 ms |

The three resource-holding nodes are serial (concurrency 1), and each body busy-waits for its body time. Each body sends its message on to the next node before its resources are given back. The output of `consumer_of_a_and_b` is not connected to anything, so a message is finished when the body of `consumer_of_a_and_b` returns.

| Implementation | How the resources are held |
|---|---|
| Join-based (`pipeline_joins`) | Each resource is a pointer in a `buffer_node`. Each resource-holding node is a `resource_composite_node` from `resource_common.h`: a `buffer_node` for messages, a reserving `join_node` that pairs a message with the resources, and a `multifunction_node` with the `queueing` policy. The body puts the resources back in their `buffer_node`s when it is done. Unlike `workflow_runner`, there is no `rejecting` variant. |
| `resource_limiter` (`pipeline_limited`) | Each resource is a pointer held by a `resource_limiter`, and each resource-holding node is a `resource_limited_node`, which acquires its resources before its body runs and releases them after the body returns. |

The pacer and the input node are the same in both implementations.

## The two variants

The variants run in this order, each with a new graph and new resources:

| Variant | Implementation |
|---|---|
| `pipeline_joins` | Join-based |
| `pipeline_limited` | `resource_limiter` |

Each variant writes two files named after it: `<variant>.csv` and `<variant>.json`. Unlike `offset_puts`, there is a single input pattern.

Each message uses `a` for 11 ms, 10 ms in `consumer_of_a` and 1 ms in `consumer_of_a_and_b`, and the same holds for `b`. The pacer's 11 ms is therefore the fastest rate the graph can keep up with: both resources must be in use the whole time. Time that a resource spends idle while messages are waiting is never made up before the input stops. The messages it delayed stay in the graph, so the number of messages in flight rises and stays up.

`consumer_of_a_and_b` competes with `consumer_of_a` for `a` and with `consumer_of_b` for `b`. When `consumer_of_b` finishes a message, it sends it to `consumer_of_a_and_b`, which then needs both resources. At about the same time, `consumer_of_a` and `consumer_of_b` have their next messages, or are about to receive them, and each needs one of those resources. How the implementation settles this decides whether a resource sits idle. At the start, the first message passes through `consumer_of_a` before it reaches `consumer_of_b`, so the two nodes start 10 ms apart, as in the `offset_puts` pattern of `offset_puts`.

## Running the benchmark

Configure oneTBB with `-DTBB_TEST_PERF=ON` and build the `pipeline` target. The executable is placed in the build's output directory, next to the oneTBB library:

```sh
cmake -S . -B build/perf -DCMAKE_BUILD_TYPE=Release -DTBB_TEST_PERF=ON
cmake --build build/perf --target pipeline
```

`pipeline` takes no command-line options and exits with code 0. Its only output on stdout is a `Lazy trace written to <file>` line as each trace file is written. It writes its four output files to the current directory, replacing those of an earlier run. Each variant takes a little over 1.1 s: 100 pacer periods of 11 ms, plus the time to finish the messages still in the graph when the input stops. A whole run therefore takes at least 2.2 s.

Up to three bodies busy-wait at once: the pacer, `consumer_of_a` and `consumer_of_b`. The machine needs at least three hardware threads for the bodies to start on time.

Tracing is always compiled into `pipeline`. The `TBB_TEST_PERF_TRACE` CMake option does not apply to it. If `pipeline.cpp` is compiled by hand with `USE_TRACE=0`, it writes no files and instead prints `Elapsed time: <seconds> seconds` for each variant.

### Changing the settings

The settings are constants in `pipeline.cpp`:

| Constant | Value | Meaning |
|---|---|---|
| `g_num_inputs` | `100` | Number of messages. |
| `long_duration` | `10000` | Body time of `consumer_of_a` and `consumer_of_b`, in microseconds. |
| `short_duration` | `1000` | Body time of `consumer_of_a_and_b`, in microseconds. |

The two durations are defined separately in `run_pipeline_using_joins` and `run_pipeline_using_limited_resources`, so change both. The pacer busy-waits for `long_duration + short_duration`, so after a change the input still arrives at the fastest rate the graph can sustain.

## Output

### CSV files

Each CSV file records how many messages were in flight during the run, and how long the run took. For example, from a run of `pipeline_joins`:

```
join iteration, active messages
0, 0
1, 1
2, 2
3, 3
4, 3
...
99, 17
100, 16
100, 16
total time (us), 1.14392e+06
```

- **The header line** is `join iteration, active messages` for the join-based variant and `limited iteration, active messages` for the `resource_limiter` variant.
- **One line per call of the input node's body**, with the number of the message the call is about to generate and the number of active messages. The count is read at the start of the call, before the message is generated. A message is active from when it is generated until `consumer_of_a_and_b` finishes it, so the count includes the message the input node is holding for the pacer and the messages waiting for resources, as well as those running. Lines 0 to 99 belong to the 100 messages. The lines numbered 100 come from the calls that found the input finished, and there can be more than one.
- **The last line** is the total time in microseconds, from just before the input node is activated until every message has been processed. It is written in the default stream format, which usually means scientific notation with 6 significant digits.

An implementation that keeps up holds the active count small and steady once the pipeline has filled. A count that keeps rising means the graph is finishing messages more slowly than they arrive. Because they arrive at the fastest rate the graph can sustain, that happens whenever a resource sits idle while messages are waiting. Every message then takes longer to pass through the graph, and memory consumption would grow with the count if the messages were large.

The total time is a weaker measure. Once the input stops, a message waiting for `consumer_of_a_and_b` needs only 1 ms, so a backlog can add little to the total time even when the active count is high. The trace shows where the messages waited.

### Trace files

Each trace file is a JSON file in the Trace Event Format, written by the trace collector (see [Using the trace collector](../../common/TRACE_COLLECTOR_README.md)). The collector keeps the events in memory and writes the file when its variant finishes. It can be opened in [Perfetto](https://ui.perfetto.dev). The trace has:

- **One process**, numbered 200 in the join-based trace and 100 in the `resource_limiter` trace. Its events have the category `pipeline` in both.
- **One track per node:** `consumer_of_a`, `consumer_of_b`, `consumer_of_a_and_b`, `pacer` and `input`. `workflow_runner` instead has one track per worker thread.
- **One slice per body execution**, on its node's track and named after the node. Slice names don't include the message number. The `input` track has a slice for every call of the input node's body, including the calls that found the input finished, and each slice includes writing that call's CSV line. A `resource_limited_node` releases its resources only after its body returns. In the join-based variant, the slice ends just before the body puts its resources back, so in both variants a slice ends before the next node that uses the same resource can start.

Timestamps are in microseconds from when the variant's trace collector was created, just before its graph was built. The trace doesn't record which thread ran each body.

### Viewing a trace in Perfetto

Open [ui.perfetto.dev](https://ui.perfetto.dev), choose **Open trace file** in the left sidebar, and select the trace. The trace is processed in your browser and is not uploaded. To compare the two variants, open each one in its own browser tab.

The tracks show the five nodes rather than the worker threads used in `workflow_runner`'s traces. This is safe because all five nodes are serial: the three resource-holding nodes and the pacer have concurrency 1, and an `input_node` makes one call of its body at a time. Slices on a track therefore never overlap, and each track shows exactly when its node was busy.

Because each resource has a single instance, a slice on `consumer_of_a_and_b` never overlaps a slice on `consumer_of_a` or `consumer_of_b`. A time when neither `consumer_of_a` nor `consumer_of_a_and_b` has a slice is a time when `a` was free, and likewise for `b`.

Things to look at:

- **The `pacer` track** shows when messages enter the pipeline: each pacer slice ends as its message is sent on to `consumer_of_a`.
- **The start of the run**, where `consumer_of_a` and `consumer_of_b` start 10 ms apart. Check whether they stay apart or come into step, and how often `consumer_of_a_and_b` gets both resources.
- **Times when `a` or `b` was free** while messages were waiting. That time is lost for the rest of the run, and it shows as a rise in the active count in the CSV file. The CSV line for message `n` is written during the slice numbered `n` on the `input` track, counting from 0, so the two can be matched up.
