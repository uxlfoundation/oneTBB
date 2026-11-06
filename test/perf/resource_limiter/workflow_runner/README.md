# Overview of workflow_runner

The `workflow_runner` performance benchmark reads workflows described in JSON format and then executes the workflow using different variants of resource limiting.

The two primary variants can be built based on a compile-time macro definition:


| `USE_LIMITER_NODE` | `JN_BODY_POLICY` | Variant | Implementation |
|---|---|---|---|
| `1` | n/a | `limiter_impl` | `resource_limiter` and `resource_limited_node` objects are used directly. |
| `0` | `queueing` (default) | `jn_impl` | Use a `composite_node` that contains a `buffer_node`, a reserving `join_node` and a `multifunction_node` to emulate a `resource_limited_node`. Resources are stored in `buffer_node` objects instead of `resource_limiters`. The `multifunction_node` queues messages that arrive while it is busy, so the `join_node` can take resources before the body is able to run. |
| `0` | `rejecting` | `jn_impl` | The same `composite_node`, but the `multifunction_node` rejects messages while it is busy. The `join_node` then releases its reservations, so resources stay available in their `buffer_node` until the body can run. Trace file names get a `_rejecting` suffix. |

## Running the benchmark

Each variant is a separate executable: `workflow_runner_jn_impl`, `workflow_runner_jn_impl_rejecting` and `workflow_runner_limiter_impl`. Configure oneTBB with `-DTBB_TEST_PERF=ON` and build the `workflow_runner_all` target to get all three; they are placed in the build's output directory, next to the oneTBB library:

```sh
cmake -S . -B build/perf -DCMAKE_BUILD_TYPE=Release -DTBB_TEST_PERF=ON
cmake --build build/perf --target workflow_runner_all
```

All variants take the same options. They print this usage, under the name `workflow_bench`, when a run stops with an error, for example when `--json` is missing:

```
Usage: workflow_bench --json=<path> [options]
Options:
  --json=<path>        Path to workflow JSON file (required)
  --num_threads=<n>    Number of worker threads, 0 = all cores (default: 4)
  --gb_per_thread=<n>  GB of memory per thread (default: 2.0)
  --num_inputs=<n>     Number of input messages (default: 100)
  --num_trials=<n>     Number of trial executions (default: 1)
  --allow_unbound_window=<0|1>  Permit C >= num_inputs, i.e. an
                       admission window that never binds. Fatal by
                       default; such a run measures an unthrottled
                       graph and its memory fractions are
                       num_inputs/C artefacts (default: 0)
  --trace=<off|minimal|full>     Trace detail level (default: off)
  --tracing-mode=<off|eager|lazy>  When to write the trace (default: lazy)
                       off suppresses the trace even if --trace= asks for one
```

Options can also be given as `--flag value`.

### Choosing the memory budget

A message is admitted only when the remaining memory budget can hold every data product the workflow declares, and that memory is returned as products are destroyed. The budget is `--gb_per_thread` times the number of threads, so the number of messages that can be in flight at once is

```
C = floor(gb_per_thread * threads * 2^30 / bytes_per_message)
```

where `bytes_per_message` is the sum of the workflow's `estimated_memory_usage` values. The run is refused unless `1 <= C < --num_inputs`, because with `C >= --num_inputs` the budget never limits anything. Pass `--allow_unbound_window=1` to run that case anyway.

For example, every message of `test_inputs/synthetic_joins.json` needs 10 MB, so 4 threads at 0.01 GB each give `C = 4`:

```sh
workflow_runner_limiter_impl --json=test_inputs/synthetic_joins.json \
    --num_threads=4 --gb_per_thread=0.01 --num_inputs=200
```

### Output

A run prints, on stdout:

- **A build banner:** the variant, the oneTBB tree and compiler it was built with, and the `JN_BODY_POLICY` or resource-limiting version.
- **The configuration it resolved:** the thread count, memory budget, `C`, and the joins it created.
- **Per-trial and summary results:** timings, the steady-state window, throughput and memory use.
- **One `BENCH_RECORD_JSON {...}` line** with all of the above in machine-readable form, plus the message latency distribution, which is reported only here. It is the last line of the run, unless `BENCH_LATENCY_RAW=1` adds the raw latencies after it.

Failures of the internal consistency checks (latency samples, resource uses, product accounting) are printed to stderr as lines starting with `Error:`, but do not change the exit code. Check for them when scripting runs.

With `--trace=minimal` or `--trace=full`, a Chrome-trace JSON file is written to the current directory, named after the input and the run configuration: `<workflow>_<variant>[_rejecting]_exec<trials>_input<n>_p<threads>_gb<gb*1000>_aw<C>_<eager|lazy>_<minimal|full>.json`.

| Exit code | Meaning |
|---|---|
| `0` | The run completed. |
| `1` | The arguments or the input file could not be used, e.g. `--json` is missing. The reason is on stderr after `Error:`, followed by the usage. |
| `2` | The run was refused, e.g. the memory budget gives an admission window that doesn't bind, or an option value isn't a number. The reason is on stderr after `FATAL:`. |
| `86` | The watchdog stopped a run that did not finish in time. |

### Environment variables

| Variable | Effect |
|---|---|
| `BENCH_WATCHDOG_S=<seconds>` | Time limit for the whole run (default 300; `0` disables it). When it expires, the run prints the messages still in flight, emits its record and exits with code 86. |
| `BENCH_LATENCY_RAW=1` | Also prints every message latency, in completion order, on a `BENCH_LATENCY_RAW_MS` line. |
| `BENCH_OCCUPANCY_DUMP=<path>` | Writes the number of resident messages over time to `<path>` as CSV (`t_s,resident,delta`). The file is rewritten each trial, so it holds the last trial. |

## Workflow JSON format

A workflow, passed with `--json`, describes the work done for one input message: a set of algorithms connected by the data products they produce and consume. `workflow_runner` builds one flow-graph node per algorithm and sends `--num_inputs` messages through the graph.

```json
{
  "algorithms": {
    "decode": {
      "estimated_execution_time": "0.002 s",
      "consumes": ["input"],
      "produces": ["decoded"],
      "resource_usage": ["disk"]
    },
    "transform": {
      "estimated_execution_time": "0.005 s",
      "consumes": ["decoded"],
      "produces": ["features"],
      "resource_usage": []
    },
    "merge": {
      "estimated_execution_time": "1.5e-03 s",
      "consumes": ["decoded", "features", "input"],
      "produces": ["result"],
      "resource_usage": ["disk", "service"]
    }
  },
  "data_products": {
    "input":    {"type": "Bytes",   "instance": null,   "estimated_memory_usage": "4 MB"},
    "decoded":  {"type": "Records", "instance": null,   "estimated_memory_usage": "2.5 MB"},
    "features": {"type": "Records", "instance": "fast", "estimated_memory_usage": "512 KB"},
    "result":   {"type": "Summary", "instance": null,   "estimated_memory_usage": "17 B"}
  },
  "limited resources": {
    "disk": {},
    "service": {}
  }
}
```

Here `input` is produced by no algorithm, so it is an external input, created for each message when the message is admitted. `decode` holds the `disk` resource while it runs. `merge` waits until it has `decoded`, `features` and `input` for the same message, then runs while holding both `disk` and `service`. Each message needs about 7 MB of the memory budget.

### Top-level keys

| Key | Contents |
|---|---|
| `algorithms` | An object mapping each algorithm name to its description. |
| `data_products` | An object mapping each data product name to its description. |
| `limited resources` | An object whose keys are the resource names. The values are ignored, so use `{}`. Note the space in this key. |

The order of entries does not matter: the graph is built in dependency order.

### Algorithms

| Field | Meaning |
|---|---|
| `estimated_execution_time` | How long the algorithm runs per message, as a string in seconds, e.g. `"0.002 s"` or `"1.5e-03 s"`. The benchmark busy-waits for this long. Required; seconds are the only unit understood. |
| `consumes` | Names of the data products the algorithm needs. |
| `produces` | Names of the data products the algorithm creates. |
| `resource_usage` | Names of the limited resources the algorithm holds while it runs, at most 2. Empty for none. |

A missing array is treated as empty.

### Data products

| Field | Meaning |
|---|---|
| `estimated_memory_usage` | The size of one instance, as a string: a number and a unit of `B`, `KB`, `MB` or `GB` (case-insensitive, 1 KB = 1024 B), e.g. `"512 KB"`. Required. These sizes make up the per-message memory budget; see [Choosing the memory budget](#choosing-the-memory-budget). |
| `type`, `instance` | Descriptive strings, and `instance` may be `null`. They are stored on each product but do not affect the run. |

### How a workflow is run

- **External inputs.** A product that is consumed but produced by no algorithm is an external input. When a message is admitted, the input generator creates all of its external inputs at once.
- **Connections.** An algorithm receives inputs from every algorithm that produces something it consumes, and from the input generator if it consumes an external input. When there is more than one source, a `join_node` first collects the parts with the same message id. An algorithm can have at most 4 sources.
- **Resources.** Each limited resource has a single handle. An algorithm with resources processes one message at a time and holds a handle of every resource it lists while it runs, so algorithms that share a resource never run at the same time. Algorithms without resources run with unlimited concurrency.
- **Memory and completion.** A message reserves the total size of all declared data products when it is admitted. Each product returns its size to the budget when it is destroyed, once no algorithm needs it any more. A message is complete when its last product has been destroyed; that is the end point of its latency.

### Rules

A workflow must follow these rules:

| Rule | If it is broken |
|---|---|
| Every product named in `consumes` or `produces` is declared in `data_products`. | The run fails. |
| Every declared product is produced by exactly one algorithm, or is an external input that some algorithm consumes. | The memory accounting no longer balances: the run reports `Error:` lines and may admit only some of the `--num_inputs` messages. |
| Every name in `resource_usage` is declared in `limited resources`. | The run stops with exit code 1. |
| The algorithms form no cycle. | The run stops with exit code 1. |
| No algorithm lists more than 2 resources or has more than 4 sources. | The run is refused at startup with exit code 2. The limits are the constants `MAX_RESOURCE_ARITY` and `MAX_JOIN_ARITY` in `workflow_runner.cpp` and can be changed if needed. |

### Current Implementation Limitations

The format has no way to set an algorithm's concurrency. This might be changed if needed by real workflows. It follows from whether the algorithm lists any resources, and is the same in both variants:

| Algorithm | Concurrency |
|---|---|
| Lists one or more resources in `resource_usage` | Is always (`flow::serial`). Since the workflow runner only supports one instance of each resource this is not a significant limitation since at most 1 instance of the node can run concurrently due to the resource constraint anyway. Even so, an algorithm with a resource and an `estimated_execution_time` of 10 ms completes at most 100 messages per second, whatever `--num_threads` is. |
| Lists no resources | Unlimited (`flow::unlimited`): it can work on as many messages at once as there are threads, so it is bounded only by `--num_threads` and the admission window `C`. |

Each entry in `limited resources` is exactly one instance of that resource, a single handle. The value of the entry is ignored, so there is no way to declare several instances of the same resource, such as a pool of identical connections; an entry like `"disk": {"count": 4}` is accepted but still gives one instance.

Both limits are fixed in the code rather than the format: the concurrency of resource-limited algorithms is the constant `RESOURCE_NODE_CONCURRENCY` in `workflow_runner.cpp`, and the harness creates one handle per resource. Supporting more would need both a way to give a resource's instance count in the JSON and a concurrency level above 1 for the algorithms that use it. This can be addressed if required to model important workflows.