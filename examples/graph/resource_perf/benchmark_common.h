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

#ifndef TBB_examples_resource_perf_benchmark_common_HPP
#define TBB_examples_resource_perf_benchmark_common_HPP

#define TBB_PREVIEW_FLOW_GRAPH_RESOURCE_LIMITING 1

#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <vector>

#include "oneapi/tbb/flow_graph.h"
using namespace oneapi::tbb::flow;

#ifndef USE_TRACE
#define USE_TRACE 2
#endif

#if USE_TRACE > 0
#include "common/utility/trace_collector.h"
#include <sstream>
#endif

// USE_MODE: 0 = flow graph with join_node, 1 = flow graph with resource limiting, 2 = priority-aware resource limiter
#ifndef USE_MODE
#define USE_MODE 0
#endif

// Other settings that can be configured via compile-time macros when using mode 2 (priority-aware resource limiter):
// __TBB_USE_CONSUMER_LOCAL_COUNTER_FOR_REQUEST_ID: use local or else global counter for request ID generation (useful only for priority-aware resource limiter)
// __TBB_USE_TIMESTAMP_IN_REQUEST_ID: include timestamp in request ID generation or not (useful only for local counter and priority-aware resource limiter)
// __TBB_USE_PRESSURE: enable or disable pressure awareness in the priority-aware resource limiter
// __TBB_USE_NOTIFY_ON_REPORT_PRESSURE: enable or disable eager notification on pressure report in priority-aware resource limiter

#if USE_MODE == 0
template<typename InputTuple>
class resource_composite_node : public oneapi::tbb::flow::composite_node<InputTuple, InputTuple> {
    using base_type = oneapi::tbb::flow::composite_node<InputTuple, InputTuple>;
    using mfn_type = multifunction_node<InputTuple, InputTuple>;

    // a buffer for the first type in the tuple
    buffer_node<std::tuple_element_t<0, InputTuple>> input_buffer;
    join_node<InputTuple, reserving> input_join;
    mfn_type body_node;
public:
    using input_type = InputTuple;
    using input_ports_type = typename base_type::input_ports_type;
    using output_ports_type = typename mfn_type::output_ports_type;

    // we assume that the body does the work of sending back the resources
    // we don't do this automatically
    template<typename BodyFunc>
    resource_composite_node(tbb::flow::graph& g, size_t concurrency_limit, BodyFunc body_func)
        : base_type(g),
          input_buffer(g),
          input_join(g),
          body_node(g, concurrency_limit, body_func) {
        // Connect the internal nodes: buffer -> join -> body
        make_edge(input_buffer, oneapi::tbb::flow::input_port<0>(input_join));
        make_edge(input_join, body_node);

        // Determine tuple size
        constexpr size_t tuple_size = std::tuple_size<InputTuple>::value;

        if constexpr (tuple_size == 2) {
            base_type::set_external_ports(
                std::tie(input_buffer, input_port<1>(input_join)),
                std::tie(output_port<0>(body_node), output_port<1>(body_node))
            );
        } else if constexpr (tuple_size == 3) {
            base_type::set_external_ports(
                std::tie(input_buffer, input_port<1>(input_join), input_port<2>(input_join)),
                std::tie(output_port<0>(body_node), output_port<1>(body_node), output_port<2>(body_node))
            );
        }
    }
};
#elif USE_MODE == 1
template<typename T>
using limiter_type = oneapi::tbb::flow::resource_limiter<T>;
#else
// USE_MODE == 2
template<typename T>
using limiter_type = oneapi::tbb::flow::priority_aware_resource_limiter<T>;
#endif

const int genie_sleep_time_ms = 10;
const int cycle_sleep_time_ms = 10;
// Thread IDs represent nodes, not actual worker threads
// These are used by both tracing (USE_TRACE) and debug logging (__TBB_DEBUG_RESOURCE_ACQUISITION)
constexpr int ROOT_NODE_TID = 1;
constexpr int GENIE_NODE_TID = 2;
constexpr int ROOT_GENIE_NODE_TID = 3;
constexpr int PROPAGATING_NODE_TID = 4;
constexpr int CALIBRATION_A_NODE_TID = 5;
constexpr int CALIBRATION_B_NODE_TID = 6;
constexpr int CALIBRATION_C_NODE_TID = 7;

#if USE_TRACE > 0
std::unique_ptr<TraceCollector> make_trace_collector(std::string_view graph_name, int num_executions = 10, int num_inputs = 100,
                                                      double generation_rate = -1, int num_resources = -1, int concurrency = -1) {
       // Create trace filename based on configuration
    std::ostringstream filename;
    filename << graph_name << "_mode" << (USE_MODE == 0 ? "join" : (USE_MODE == 1 ? "limited" : "priority"));

#if USE_MODE == 2
    // Counter type: local or global
    #if __TBB_USE_CONSUMER_LOCAL_COUNTER_FOR_REQUEST_ID
        filename << "_local";
        #if __TBB_USE_TIMESTAMP_IN_REQUEST_ID
            filename << "_ts";
        #endif
    #else
        filename << "_global";
    #endif

    // Notify on report_pressure
    #if __TBB_USE_NOTIFY_ON_REPORT_PRESSURE
        filename << "_notify";
    #endif
#endif

    filename << "_exec" << num_executions
             << "_input" << num_inputs;

    // Add rate, resources, and concurrency if provided
    if (generation_rate >= 0) {
        filename << "_r" << static_cast<int>(generation_rate);
    }
    if (num_resources >= 0) {
        filename << "_rl" << num_resources;
    }
    if (concurrency >= 0) {
        filename << "_conc" << concurrency;
    }

    filename << ".json";

    // Create TraceCollector based on USE_TRACE value
    TraceCollector::WriteMode mode = (USE_TRACE == 1)
        ? TraceCollector::WriteMode::EAGER
        : TraceCollector::WriteMode::LAZY;

    std::unique_ptr<TraceCollector> trace_collector = std::make_unique<TraceCollector>(filename.str(), 1, "resource_limited_ubench", mode);

    std::cout << "Tracing enabled: " << (USE_TRACE == 1 ? "EAGER" : "LAZY")
              << " mode, output: " << filename.str() << "\n";

    return trace_collector;
}

std::unique_ptr<ScopedTraceEvent> make_event(std::unique_ptr<TraceCollector>& trace_collector, int input, int tid) {
    std::string event_name = "input_" + std::to_string(input);
    return std::make_unique<ScopedTraceEvent>(*trace_collector, event_name, tid);
}

inline void record_input_start(std::unique_ptr<TraceCollector>& trace_collector, int input) {
    if (trace_collector) {
        std::string event_name = "latency_start_input_" + std::to_string(input);
        trace_collector->record_instant_event(event_name, 0);  // tid=0 for source events
    }
}
#endif


struct counting_resource {
    std::atomic<std::size_t> counter;
    counting_resource() : counter(0) {}
    void use() {
        ++counter;
    }
};

// ============================================================================
// Helper Functions and Templates for Benchmark Refactoring
// ============================================================================

// Phase 1: Execution Loop Template
template<typename SourceNode, typename Resource>
std::chrono::high_resolution_clock::time_point
run_execution_loop(graph& g, SourceNode& source, Resource& resource,
                   int num_executions, int num_inputs,
                   double generation_rate, double delay_ms,
                   std::chrono::high_resolution_clock::time_point& start_time
#if USE_TRACE > 0
                   , std::unique_ptr<TraceCollector>& trace_collector
#endif
                   ) {
    for (int i = -1; i < num_executions; ++i) {
        if (i == 0) {
            resource.counter = 0;
            start_time = std::chrono::high_resolution_clock::now();
        }

        for (int j = 0; j < num_inputs; ++j) {
#if USE_TRACE > 0
            record_input_start(trace_collector, j);
#endif
            source.try_put(j);

            // Add delay between messages (except after last message)
            if (j < num_inputs - 1 && delay_ms > 0) {
                std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(delay_ms));
            }
        }
        g.wait_for_all();
    }

    return std::chrono::high_resolution_clock::now();
}

// Overload for multiple resources (vector)
template<typename SourceNode>
std::chrono::high_resolution_clock::time_point
run_execution_loop(graph& g, SourceNode& source, std::vector<counting_resource>& resources,
                   int num_executions, int num_inputs,
                   double generation_rate, double delay_ms,
                   std::chrono::high_resolution_clock::time_point& start_time
#if USE_TRACE > 0
                   , std::unique_ptr<TraceCollector>& trace_collector
#endif
                   ) {
    for (int i = -1; i < num_executions; ++i) {
        if (i == 0) {
            // Reset all resource counters
            for (auto& res : resources) {
                res.counter = 0;
            }
            start_time = std::chrono::high_resolution_clock::now();
        }

        for (int j = 0; j < num_inputs; ++j) {
#if USE_TRACE > 0
            record_input_start(trace_collector, j);
#endif
            source.try_put(j);

            // Add delay between messages (except after last message)
            if (j < num_inputs - 1 && delay_ms > 0) {
                std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(delay_ms));
            }
        }
        g.wait_for_all();
    }

    return std::chrono::high_resolution_clock::now();
}

// Phase 1: Result Validation Helper
inline void validate_resource_usage(const counting_resource& resource,
                                    int expected_uses,
                                    const char* benchmark_name) {
    if (resource.counter != expected_uses) {
        std::cerr << "Error: " << benchmark_name << " resource was used "
                  << resource.counter << " times, expected "
                  << expected_uses << " times." << std::endl;
    }
}

// Overload for multiple resources
inline void validate_resource_usage(const std::vector<counting_resource>& resources,
                                    int expected_uses_total,
                                    const char* benchmark_name) {
    std::size_t total_uses = 0;
    for (const auto& res : resources) {
        total_uses += res.counter.load();
    }
    if (total_uses != expected_uses_total) {
        std::cerr << "Error: " << benchmark_name << " resources were used "
                  << total_uses << " times total, expected "
                  << expected_uses_total << " times." << std::endl;
    }
}

// Phase 2: Mode-Specific Type Traits
template<int Mode, typename InputType, typename ResourceType>
struct node_types;

#if USE_MODE == 0
// Mode 0: join_node with explicit resource passing
template<typename InputType, typename ResourceType>
struct node_types<0, InputType, ResourceType> {
    using resource_tuple = std::tuple<InputType, ResourceType>;
    using node_type = resource_composite_node<resource_tuple>;
    using ports_type = typename multifunction_node<resource_tuple, resource_tuple>::output_ports_type;
};
#elif USE_MODE == 1
// Mode 1: resource_limiter
template<typename InputType, typename ResourceType>
struct node_types<1, InputType, ResourceType> {
    using node_type = resource_limited_node<InputType, std::tuple<InputType>>;
    using ports_type = typename node_type::output_ports_type;
};
#elif USE_MODE == 2
// Mode 2: priority_aware_resource_limiter
template<typename InputType, typename ResourceType>
struct node_types<2, InputType, ResourceType> {
    using node_type = resource_limited_node<InputType, std::tuple<InputType>>;
    using ports_type = typename node_type::output_ports_type;
};
#endif

// Phase 2: Resource Limiter Setup Helper Macro
// Extra level of indirection needed for proper macro expansion
#define SETUP_RESOURCE_LIMITER(mode, graph_var, resource_var, limiter_var) \
    SETUP_RESOURCE_LIMITER_EXPAND(mode, graph_var, resource_var, limiter_var)

#define SETUP_RESOURCE_LIMITER_EXPAND(mode, graph_var, resource_var, limiter_var) \
    SETUP_RESOURCE_LIMITER_IMPL_##mode(graph_var, resource_var, limiter_var)

#define SETUP_RESOURCE_LIMITER_IMPL_0(g, res, lim) \
    buffer_node<counting_resource*> lim(g); \
    lim.try_put(&res);

#define SETUP_RESOURCE_LIMITER_IMPL_1(g, res, lim) \
    limiter_type<counting_resource*> lim(&res);

#define SETUP_RESOURCE_LIMITER_IMPL_2(g, res, lim) \
    limiter_type<counting_resource*> lim(&res);

// Helper function to initialize multiple resources into buffer_node (Mode 0)
template<typename BufferNode>
inline void init_multiple_resources_mode0(BufferNode& buffer, std::vector<counting_resource>& resources) {
    for (auto& res : resources) {
        buffer.try_put(&res);
    }
}

// Helper template to create resource_limiter with multiple resources (Mode 1/2)
// Using index_sequence to expand vector into variadic arguments
template<typename LimiterType, std::size_t... Is>
inline auto create_limiter_impl(std::vector<counting_resource>& resources, std::index_sequence<Is...>) {
    return LimiterType(&resources[Is]...);
}

template<typename LimiterType>
inline auto create_limiter_with_resources(std::vector<counting_resource>& resources, int num_resources) {
    // Dispatch based on num_resources
    switch (num_resources) {
        case 1: return LimiterType(&resources[0]);
        case 2: return create_limiter_impl<LimiterType>(resources, std::make_index_sequence<2>{});
        case 3: return create_limiter_impl<LimiterType>(resources, std::make_index_sequence<3>{});
        case 5: return create_limiter_impl<LimiterType>(resources, std::make_index_sequence<5>{});
        case 10: return create_limiter_impl<LimiterType>(resources, std::make_index_sequence<10>{});
        case 20: return create_limiter_impl<LimiterType>(resources, std::make_index_sequence<20>{});
        default:
            std::cerr << "Error: num_resources=" << num_resources << " not supported. Supported values: 1,2,3,5,10,20\n";
            std::exit(1);
    }
}

// Phase 5: Trace Setup Helper
#if USE_TRACE > 0
template<typename... ThreadNames>
std::unique_ptr<TraceCollector> setup_trace(const char* graph_name,
                                            int num_executions,
                                            int num_inputs,
                                            ThreadNames&&... thread_names) {
    auto trace = make_trace_collector(graph_name, num_executions, num_inputs);
    int tid = 1;
    (trace->add_thread_name(tid++, std::forward<ThreadNames>(thread_names)), ...);
    return trace;
}
#endif

// Result Printing Helper
inline void print_results(const std::string& bench_name, std::chrono::duration<double> construction_time,
                         std::chrono::duration<double> execution_time, int num_executions, int num_inputs,
                         double generation_rate, double delay_ms, double total_graph_time_ms) {
    std::cout << bench_name << " results:\n";
    std::cout << "  Construction time: " << construction_time.count() << " seconds\n";
    std::cout << "  Execution time: " << execution_time.count() << " seconds\n";
    std::cout << "  Total time: " << (construction_time + execution_time).count() << " seconds\n";
    std::cout << "  Time per execution: " << (execution_time.count() / num_executions) << " seconds\n";
    std::cout << "  Time per input: " << (execution_time.count() / (num_executions * num_inputs)) << " seconds\n";

    // Display generation rate and delay info
    if (bench_name == "baseline_cycle_bench") {
        std::cout << "  Generation rate: N/A (not applicable to self-propagating cycle)\n";
    } else {
        std::cout << "  Generation rate: " << generation_rate
                  << " (delay: " << delay_ms << "ms, graph_time: " << total_graph_time_ms << "ms)\n";
    }
}

#endif // TBB_examples_resource_perf_benchmark_common_HPP
