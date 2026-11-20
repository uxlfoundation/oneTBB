# Overview of offset_puts

The `offset_puts` performance benchmark checks whether a node that needs two resources gets to run when it shares those resources with two longer-running nodes. It builds the same small graph with two implementations of resource limiting, a join-based emulation and `resource_limiter` with `resource_limited_node`, and sends each one two patterns of input. All four variants run one after the other in a single executable.

## The graph

Two resources, `a` and `b`, have a single instance each. Three nodes use them:

| Node | Resources it holds | Body time |
|---|---|---|
| `consumer_of_a` | `a` | 10 ms |
| `consumer_of_b` | `b` | 10 ms |
| `consumer_of_a_and_b` | `a` and `b` | 1 ms |

Every node is serial (concurrency 1), and each body busy-waits for its body time. The outputs of the nodes are not connected to anything, so a message is finished when the body that received it returns.

| Implementation | How the resources are held |
|---|---|
| Join-based (`*_joins` files) | Each resource is a pointer in a `buffer_node`. Each node is a `resource_composite_node` from `resource_common.h`: a `buffer_node` for messages, a reserving `join_node` that pairs a message with the resources, and a `multifunction_node` with the `queueing` policy. The body puts the resources back in their `buffer_node`s when it is done. Unlike `workflow_runner`, there is no `rejecting` variant. |
| `resource_limiter` (`*_limited` files) | Each resource is a pointer held by a `resource_limiter`, and each node is a `resource_limited_node`, which acquires its resources before its body runs and releases them after the body returns. |

## The four variants

Each variant sends 100 messages to each node, one per node in each of 100 iterations of 11 ms. In every iteration, `a` is needed for 10 ms by `consumer_of_a` and for 1 ms by `consumer_of_a_and_b`, and the same holds for `b`. 11 ms is therefore the shortest iteration the graph can keep up with: both resources must be in use the whole time.

The two input patterns differ only in when the three messages of an iteration are put, measured from the start of the iteration:

| Pattern | `consumer_of_a` | `consumer_of_b` | `consumer_of_a_and_b` | Next iteration |
|---|---|---|---|---|
| `puts` | 0 ms | 0 ms | 0 ms | 11 ms |
| `offset_puts` | 0 ms | 5 ms | 10 ms | 11 ms |

- **`puts`:** `consumer_of_a` and `consumer_of_b` start together and release their resources at about 10 ms, so `consumer_of_a_and_b` can take both and finish before the next iteration starts.
- **`offset_puts`:** if every node starts as soon as its message arrives, `a` is in use from 0 to 10 ms and `b` from 5 to 15 ms, and the next message for `consumer_of_a` arrives at 11 ms, while `b` is still in use. `a` and `b` are then never free at the same time. While input is arriving, `consumer_of_a_and_b` can run only if the implementation keeps `a` free for it until `b` is released, instead of handing `a` to the next `consumer_of_a` message. Otherwise its messages wait until the input stops.

The variants run in this order, each with a new graph and new resources:

| Variant | Implementation | Pattern |
|---|---|---|
| `puts_joins` | Join-based | `puts` |
| `offset_puts_joins` | Join-based | `offset_puts` |
| `puts_limited` | `resource_limiter` | `puts` |
| `offset_puts_limited` | `resource_limiter` | `offset_puts` |

Each variant writes two files named after it: `<variant>.csv` and `<variant>.json`.

## Running the benchmark

Configure oneTBB with `-DTBB_TEST_PERF=ON` and build the `offset_puts` target. The executable is placed in the build's output directory, next to the oneTBB library:

```sh
cmake -S . -B build/perf -DCMAKE_BUILD_TYPE=Release -DTBB_TEST_PERF=ON
cmake --build build/perf --target offset_puts
```

`offset_puts` takes no command-line options and exits with code 0. Its only output on stdout is a `Lazy trace written to <file>` line as each trace file is written. It writes its eight output files to the current directory, replacing those of an earlier run. Each variant takes a little over 1.1 s: the length of its input loop, plus the time to finish any messages still waiting when the loop ends. A whole run therefore takes at least 4.4 s.

The thread that puts the messages busy-waits between puts, so the bodies run on oneTBB worker threads. At most two bodies run at once, so the machine needs at least three hardware threads for the bodies to start on time.

Tracing is always compiled into `offset_puts`. The `TBB_TEST_PERF_TRACE` CMake option does not apply to it.

### Changing the settings

The settings are constants in `offset_puts.cpp`:

| Constant | Value | Meaning |
|---|---|---|
| `g_num_inputs` | `100` | Number of iterations, and so of messages per node. |
| `long_duration` | `10000` | Body time of `consumer_of_a` and `consumer_of_b`, in microseconds. |
| `short_duration` | `1000` | Body time of `consumer_of_a_and_b`, in microseconds. |

The two durations are defined separately in `run_puts_using_joins` and `run_puts_using_limited_resources`, so change both. An iteration lasts `long_duration + short_duration` in both patterns. The `offset_puts` pattern puts the message for `consumer_of_b` `long_duration / 2` after the one for `consumer_of_a`, and the message for `consumer_of_a_and_b` `long_duration` after it.

## Output

### CSV files

Each CSV file records how many messages were in flight during the run, and how long the run took. For example, from a run of `offset_puts_joins`:

```
join iteration, active messages
0, 0
1, 2
2, 3
...
98, 99
99, 100
total time (us), 1.21788e+06
```

- **The header line** is `join iteration, active messages` for the join-based variants and `offset puts iteration, active messages` for the `resource_limiter` variants, whatever their pattern.
- **One line per iteration**, 100 in all, with the iteration number and the number of active messages. The count is read at the start of the iteration, before its messages are put. A message is active from just before it is put until its body finishes, so the count includes messages waiting for their resources as well as those running.
- **The last line** is the total time in microseconds, from just before the first put until every message has been processed. It is written in the default stream format, which usually means scientific notation with 6 significant digits.

An implementation that keeps up holds the active count small and steady, and its total time is a little over 1.1 s (`1.1e+06`), the length of the input loop. A count that grows through the run means messages are arriving faster than the graph finishes them. The backlog is processed after the last put, which adds to the total time and results in a growing memory consumption if the messages were actually large in size. The trace shows which node the messages were waiting for.

### Trace files

Each trace file is a JSON file in the Trace Event Format, written by the trace collector (see [Using the trace collector](../../common/TRACE_COLLECTOR_README.md)). The collector keeps the events in memory and writes the file when its variant finishes. It can be opened in [Perfetto](https://ui.perfetto.dev). The trace has:

- **One process**, numbered 200 in the join-based traces and 100 in the `resource_limiter` traces. Its events have the category `join_puts` or `limited_puts`, respectively.
- **One track per node:** `consumer_of_a`, `consumer_of_b` and `consumer_of_a_and_b`. `workflow_runner` instead has one track per worker thread.
- **One slice per body execution**, on its node's track and named after the node. Slice names don't include the message number. A `resource_limited_node` releases its resources only after its body returns. In the join-based variants, the slice ends just before the body puts its resources back, so in every variant a slice ends before the next node that uses the same resource can start.

Timestamps are in microseconds from when the variant's trace collector was created, just before its graph was built. The trace has no events for the puts, and doesn't record which thread ran each body.

### Viewing a trace in Perfetto

Open [ui.perfetto.dev](https://ui.perfetto.dev), choose **Open trace file** in the left sidebar, and select the trace. The trace is processed in your browser and is not uploaded. To compare two variants, open each one in its own browser tab.

The tracks show the three nodes rather than the worker threads used in `workflow_runner`'s traces. This is safe because all three nodes are serial: a node runs one body at a time, so slices on its track never overlap, and each track shows exactly when its node was busy.

Because each resource has a single instance, a slice on `consumer_of_a_and_b` never overlaps a slice on either of the other two tracks. A time when neither `consumer_of_a` nor `consumer_of_a_and_b` has a slice is a time when `a` was free, and likewise for `b`.

In the `offset_puts` variants, look at where the `consumer_of_a_and_b` slices are:

- **Spread through the run**, between the `consumer_of_a` slices: `consumer_of_a_and_b` got both resources while input was arriving.
- **Bunched together after the last `consumer_of_a` and `consumer_of_b` slices:** it got them only once the input stopped. These are the messages that made the active count in the CSV file grow.