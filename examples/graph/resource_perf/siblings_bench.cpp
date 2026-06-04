/*
    Copyright (c) 2026 UXL Foundation Contributors

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include "benchmark_common.h"
#include <csignal>
#include <atomic>

// Signal handler for timeout - allows flushing trace on SIGTERM
#if USE_TRACE == 2  // Lazy mode only
static std::atomic<TraceCollector*> g_trace_collector{nullptr};
#endif

void timeout_signal_handler(int signum) {
    std::fprintf(stderr, "\nReceived signal %d, flushing outputs before exit...\n", signum);
#if USE_TRACE == 2
    TraceCollector* tc = g_trace_collector.load();
    if (tc) {
        tc->flush();
    }
#endif
    std::exit(128 + signum);
}

// Performance for Siblings: input_node drives N sibling nodes
std::tuple<std::chrono::duration<double>, std::chrono::duration<double>>
run_siblings_bench(int num_executions, int num_inputs, int num_nodes, double generation_rate, int num_resources, int concurrency=1) {
    auto start_construction_time = std::chrono::high_resolution_clock::now();

    // Calculate delay between messages based on generation rate
    // Siblings: sum of all stages = num_nodes * cycle_sleep_time_ms
    const double total_graph_time_ms = num_nodes * cycle_sleep_time_ms;
    double delay_ms = total_graph_time_ms / generation_rate;
    if (generation_rate >= 1000)
        delay_ms = 0;

#if USE_TRACE > 0
    std::unique_ptr<TraceCollector> trace_collector = make_trace_collector("siblings_bench", num_executions, num_inputs, generation_rate, num_resources, concurrency);
#if USE_TRACE == 2
    // Register for signal handling so lazy trace gets flushed on timeout
    g_trace_collector.store(trace_collector.get());
    // Also use atexit as backup (Windows timeout may not send catchable signal)
    std::atexit([]() {
        TraceCollector* tc = g_trace_collector.load();
        if (tc) {
            std::fprintf(stderr, "\natexit handler: flushing lazy trace...\n");
            tc->flush();
        }
    });
#endif
    // Register thread names for each sibling node
    for (int i = 0; i < num_nodes; ++i) {
        trace_collector->add_thread_name(i + 1, "sibling_" + std::to_string(i));
    }
#endif

    std::vector<counting_resource> resources(num_resources);

    using namespace oneapi::tbb::flow;
    graph g;

    // Use type traits for mode-specific types
    using node_types_t = node_types<USE_MODE, int, counting_resource*>;
    using node_type = typename node_types_t::node_type;
#if USE_MODE == 0
    using mfn_ports = typename node_types_t::ports_type;
#else
    using ports_type = typename node_types_t::ports_type;
#endif

    // Setup resource limiter with multiple resources
#if USE_MODE == 0
    buffer_node<counting_resource*> resource_limiter(g);
    init_multiple_resources_mode0(resource_limiter, resources);
#elif USE_MODE == 1
    auto resource_limiter = create_limiter_with_resources<limiter_type<counting_resource*>>(resources, num_resources);
#else  // USE_MODE == 2
    auto resource_limiter = create_limiter_with_resources<limiter_type<counting_resource*>>(resources, num_resources);
#endif

    broadcast_node<int> source(g);
    std::vector<node_type*> nodes;

    // Create N sibling nodes
    for (int i = 0; i < num_nodes; ++i) {
#if USE_MODE == 0
        nodes.push_back(new node_type(g, concurrency,
            [&, i](const std::tuple<int, counting_resource*>& input_tuple, mfn_ports& ports) {
                auto [input, resource] = input_tuple;
#if USE_TRACE > 0
                std::unique_ptr<ScopedTraceEvent> trace = make_event(trace_collector, input, i + 1);
#endif
                resource->use();
                std::this_thread::sleep_for(std::chrono::milliseconds(cycle_sleep_time_ms));
                std::get<0>(ports).try_put(input);
                std::get<1>(ports).try_put(resource);
            }));
#else
        nodes.push_back(new node_type(g, concurrency, std::tie(resource_limiter),
            [&, i](int input, ports_type& ports, counting_resource* resource) {
#if USE_TRACE > 0
                std::unique_ptr<ScopedTraceEvent> trace = make_event(trace_collector, input, i + 1);
#endif
                resource->use();
                std::this_thread::sleep_for(std::chrono::milliseconds(cycle_sleep_time_ms));
                std::get<0>(ports).try_put(input);
            }));
#endif
    }

    // Connect source to all sibling nodes
    for (int i = 0; i < num_nodes; ++i) {
#if USE_MODE == 0
        make_edge(source, std::get<0>(nodes[i]->input_ports()));
        make_edge(resource_limiter, std::get<1>(nodes[i]->input_ports()));
        make_edge(std::get<1>(nodes[i]->output_ports()), resource_limiter);
#else
        make_edge(source, *nodes[i]);
#endif
    }

    auto end_construction_time = std::chrono::high_resolution_clock::now();

    // Use execution loop helper
    std::chrono::high_resolution_clock::time_point start_execution_time;
    auto end_execution_time = run_execution_loop(g, source, resources,
                                                  num_executions, num_inputs,
                                                  generation_rate, delay_ms, start_execution_time
#if USE_TRACE > 0
                                                  , trace_collector
#endif
                                                  );

    // Validate resource usage using helper
    int expected_uses = num_inputs * num_nodes * num_executions;
    validate_resource_usage(resources, expected_uses, "siblings_bench");

    // Cleanup
    for (auto* node : nodes) {
        delete node;
    }

    return {end_construction_time - start_construction_time, end_execution_time - start_execution_time};
}

int main(int argc, char* argv[]) {
    // Install signal handler for lazy trace flush on timeout
    std::signal(SIGTERM, timeout_signal_handler);
    std::signal(SIGINT, timeout_signal_handler);
#ifdef SIGBREAK  // Windows
    std::signal(SIGBREAK, timeout_signal_handler);
#endif

    // Parse command-line arguments
    int num_executions = (argc >= 2) ? std::atoi(argv[1]) : 1;
    int num_inputs = (argc >= 3) ? std::atoi(argv[2]) : 100;
    int num_nodes = (argc >= 4) ? std::atoi(argv[3]) : 10;
    double generation_rate = (argc >= 5) ? std::atof(argv[4]) : 5.0;
    int num_resources = (argc >= 6) ? std::atoi(argv[5]) : 1;
    int concurrency = (argc >= 7) ? std::atoi(argv[6]) : 1;

    // Print configuration
#if USE_MODE == 0
    std::cout << "USE_MODE=0 (join_node)\n";
#elif USE_MODE == 1
    std::cout << "USE_MODE=1 (resource_limiter)\n";
#else
    std::cout << "USE_MODE=2 (priority_aware_resource_limiter)\n";
#endif

    std::cout << "Benchmark: siblings\n\n";

    std::cout << "macro settings:\n";
#if USE_MODE == 0
    std::cout << "  USE_MODE=0: flow graph with join_node and explicit resource passing\n";
#elif USE_MODE == 1
    std::cout << "  USE_MODE=1: flow graph with resource_limiter\n";
#else
    std::cout << "  USE_MODE=2: flow graph with priority_aware_resource_limiter\n";
#if __TBB_USE_CONSUMER_LOCAL_COUNTER_FOR_REQUEST_ID
    std::cout << "  __TBB_USE_CONSUMER_LOCAL_COUNTER_FOR_REQUEST_ID=1\n";
#else
    std::cout << "  __TBB_USE_CONSUMER_LOCAL_COUNTER_FOR_REQUEST_ID=0\n";
#endif
#if __TBB_USE_TIMESTAMP_IN_REQUEST_ID
    std::cout << "  __TBB_USE_TIMESTAMP_IN_REQUEST_ID=1\n";
#else
    std::cout << "  __TBB_USE_TIMESTAMP_IN_REQUEST_ID=0\n";
#endif
#if __TBB_USE_PRESSURE
    std::cout << "  __TBB_USE_PRESSURE=1\n";
#else
    std::cout << "  __TBB_USE_PRESSURE=0\n";
#endif
#if __TBB_USE_NOTIFY_ON_REPORT_PRESSURE
    std::cout << "  __TBB_USE_NOTIFY_ON_REPORT_PRESSURE=1\n";
#else
    std::cout << "  __TBB_USE_NOTIFY_ON_REPORT_PRESSURE=0\n";
#endif
#endif

    std::cout << "Configuration parameters:\n";
    std::cout << "  num_executions: " << num_executions << "\n";
    std::cout << "  num_inputs: " << num_inputs << "\n";
    std::cout << "  num_nodes/tree_depth: " << num_nodes << "\n";
    std::cout << "  num_resources: " << num_resources << "\n";
    std::cout << "  generation_rate: " << generation_rate << "\n";
    std::cout << "  concurrency: " << concurrency << " (node in-flight limit)\n";

    // Run benchmark
    auto [construction_time, execution_time] = run_siblings_bench(num_executions, num_inputs, num_nodes, generation_rate, num_resources, concurrency);

    // Print results
    double total_graph_time_ms = num_nodes * cycle_sleep_time_ms;
    double delay_ms = total_graph_time_ms / generation_rate;
    print_results("siblings_bench", construction_time, execution_time, num_executions, num_inputs,
                  generation_rate, delay_ms, total_graph_time_ms);

    return 0;
}
