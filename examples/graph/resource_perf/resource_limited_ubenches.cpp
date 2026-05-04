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

#define TBB_PREVIEW_FLOW_GRAPH_RESOURCE_LIMITING 1

#include<iostream>
#include<chrono>

#include "oneapi/tbb/flow_graph.h"
using namespace oneapi::tbb::flow;



#ifndef USE_TRACE
#define USE_TRACE 2
#endif

#if USE_TRACE > 0
#include "common/utility/trace_collector.h"
#include <sstream>
#endif

// USE_MODE: 0 = flow graph with join_node, 1 = flow graph with resource limiting, 2 = pressure-aware resource limiter
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

#if USE_TRACE > 0
// Thread IDs represent nodes, not actual worker threads
constexpr int ROOT_NODE_TID = 1;
constexpr int GENIE_NODE_TID = 2;
constexpr int ROOT_GENIE_NODE_TID = 3;
constexpr int PROPAGATING_NODE_TID = 4;
constexpr int CALIBRATION_A_NODE_TID = 5;
constexpr int CALIBRATION_B_NODE_TID = 6;
constexpr int CALIBRATION_C_NODE_TID = 7;

std::unique_ptr<TraceCollector> make_trace_collector(std::string_view graph_name, int num_executions = 10, int num_inputs = 100) {
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
             << "_input" << num_inputs << ".json";

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
                   std::chrono::high_resolution_clock::time_point& start_time) {
    for (int i = -1; i < num_executions; ++i) {
        if (i == 0) {
            resource.counter = 0;
            start_time = std::chrono::high_resolution_clock::now();
        }

        for (int j = 0; j < num_inputs; ++j) {
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
                   std::chrono::high_resolution_clock::time_point& start_time) {
    for (int i = -1; i < num_executions; ++i) {
        if (i == 0) {
            // Reset all resource counters
            for (auto& res : resources) {
                res.counter = 0;
            }
            start_time = std::chrono::high_resolution_clock::now();
        }

        for (int j = 0; j < num_inputs; ++j) {
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

// ============================================================================
// Benchmark Functions
// ============================================================================

// returns both the time to construct the graph and the time to execute the graph
std::tuple<std::chrono::duration<double>, std::chrono::duration<double>>
run_genie_bench(int num_executions = 10, int num_inputs = 100, double generation_rate = 1.0) {
    auto start_construction_time = std::chrono::high_resolution_clock::now();

    // Calculate delay between messages based on generation rate
    // Genie: max(genie, root) + genie_root = 10ms + 10ms = 20ms
    const double total_graph_time_ms = 2.0 * genie_sleep_time_ms;
    const double delay_ms = total_graph_time_ms / generation_rate;

#if USE_TRACE > 0
    std::unique_ptr<TraceCollector> trace_collector = make_trace_collector("genie_bench", num_executions, num_inputs);
    trace_collector->add_thread_name(ROOT_NODE_TID, "root_node");
    trace_collector->add_thread_name(GENIE_NODE_TID, "genie_node");
    trace_collector->add_thread_name(ROOT_GENIE_NODE_TID, "root_genie_node");
    trace_collector->add_thread_name(PROPAGATING_NODE_TID, "propagating_node");
    trace_collector->add_thread_name(CALIBRATION_A_NODE_TID, "calibration_a_node");
    trace_collector->add_thread_name(CALIBRATION_B_NODE_TID, "calibration_b_node");
    trace_collector->add_thread_name(CALIBRATION_C_NODE_TID, "calibration_c_node");
#endif

    counting_resource root_resource;
    counting_resource genie_resource;
    counting_resource db_resource_1;
    counting_resource db_resource_2;

    using namespace oneapi::tbb::flow;
    graph g;

#if USE_MODE == 0
    // providers of resources
    buffer_node<counting_resource*> root_limiter(g);
    root_limiter.try_put(&root_resource);
    buffer_node<counting_resource*> genie_limiter(g);
    genie_limiter.try_put(&genie_resource);
    buffer_node<counting_resource*> db_limiter(g);
    db_limiter.try_put(&db_resource_1);
    db_limiter.try_put(&db_resource_2);

    using node_type_0 = function_node<int, int>;
    using node_type_1 = resource_composite_node<std::tuple<int, counting_resource*>>;
    using node_type_2 = resource_composite_node<std::tuple<int, counting_resource*, counting_resource*>>;
#else
    // providers of resources
    limiter_type<counting_resource*> root_limiter(&root_resource);
    limiter_type<counting_resource*> genie_limiter(&genie_resource);
    limiter_type<counting_resource*> db_limiter(&db_resource_1, &db_resource_2);

    using node_type_0 = function_node<int, int>;
    using node_type = resource_limited_node<int, std::tuple<int>>;
    using ports_type = typename node_type::output_ports_type;
#endif

    broadcast_node<int> start(g);

    #if USE_MODE == 0
    using mfn_ports_1 = typename multifunction_node<std::tuple<int, counting_resource*>, std::tuple<int, counting_resource*>>::output_ports_type;
    using mfn_ports_2 = typename multifunction_node<std::tuple<int, counting_resource*, counting_resource*>, std::tuple<int, counting_resource*, counting_resource*>>::output_ports_type;

    node_type_1 root_node(g, unlimited,
        [&](const std::tuple<int, counting_resource*> &input_tuple, mfn_ports_1& ports) {
            auto [input, root] = input_tuple;
#if USE_TRACE > 0
            std::unique_ptr<ScopedTraceEvent> trace = make_event(trace_collector, input, ROOT_NODE_TID);
#endif
            root->use();
            std::this_thread::sleep_for(std::chrono::milliseconds(genie_sleep_time_ms)); // Simulate work
            std::get<0>(ports).try_put(input);
            std::get<1>(ports).try_put(root); // return the resource
        });

    node_type_1 genie_node(g, unlimited,
        [&](const std::tuple<int, counting_resource*>& input_tuple, mfn_ports_1& ports) {
            auto [input, genie] = input_tuple;
#if USE_TRACE > 0
            std::unique_ptr<ScopedTraceEvent> trace = make_event(trace_collector, input, GENIE_NODE_TID);
#endif
            genie->use();
            std::this_thread::sleep_for(std::chrono::milliseconds(genie_sleep_time_ms)); // Simulate work
            std::get<0>(ports).try_put(input);
            std::get<1>(ports).try_put(genie); // return the resource
        });


    node_type_2 root_genie_node(g, 1,
        [&](const std::tuple<int, counting_resource*, counting_resource*> &input_tuple, mfn_ports_2& ports) {
            auto [input, root, genie] = input_tuple;
#if USE_TRACE > 0
            std::unique_ptr<ScopedTraceEvent> trace = make_event(trace_collector, input, ROOT_GENIE_NODE_TID);
#endif
            root->use();
            genie->use();
            std::this_thread::sleep_for(std::chrono::milliseconds(genie_sleep_time_ms)); // Simulate work
            std::get<0>(ports).try_put(input);
            std::get<1>(ports).try_put(root); // return the resource
            std::get<2>(ports).try_put(genie); // return the resource
        });

    node_type_0 propagating_node(g, unlimited, 
        [&](const int& input) {
#if USE_TRACE > 0
            std::unique_ptr<ScopedTraceEvent> trace = make_event(trace_collector, input, PROPAGATING_NODE_TID);
#endif
            std::this_thread::sleep_for(std::chrono::milliseconds(cycle_sleep_time_ms)); // Simulate work
            return input;
        }); 

    node_type_1 calibration_a_node(g, unlimited,
        [&](const std::tuple<int, counting_resource*>& input_tuple, mfn_ports_1& ports) {
            auto [input, db] = input_tuple;
#if USE_TRACE > 0
            std::unique_ptr<ScopedTraceEvent> trace = make_event(trace_collector, input, CALIBRATION_A_NODE_TID);
#endif
            db->use();
            std::this_thread::sleep_for(std::chrono::milliseconds(cycle_sleep_time_ms)); // Simulate work
            std::get<0>(ports).try_put(input);
            std::get<1>(ports).try_put(db); // return the resource
        });

    node_type_1 calibration_b_node(g, unlimited,
        [&](const std::tuple<int, counting_resource*>& input_tuple, mfn_ports_1& ports) {
            auto [input, db] = input_tuple;
#if USE_TRACE > 0
            std::unique_ptr<ScopedTraceEvent> trace = make_event(trace_collector, input, CALIBRATION_B_NODE_TID);
#endif
            db->use();
            std::this_thread::sleep_for(std::chrono::milliseconds(cycle_sleep_time_ms)); // Simulate work
            std::get<0>(ports).try_put(input);
            std::get<1>(ports).try_put(db); // return the resource
        });

    node_type_1 calibration_c_node(g, 1,
        [&](const std::tuple<int, counting_resource*>& input_tuple, mfn_ports_1& ports) {
            auto [input, db] = input_tuple;
#if USE_TRACE > 0
            std::unique_ptr<ScopedTraceEvent> trace = make_event(trace_collector, input, CALIBRATION_C_NODE_TID);
#endif
            db->use();
            std::this_thread::sleep_for(std::chrono::milliseconds(cycle_sleep_time_ms)); // Simulate work
            std::get<0>(ports).try_put(input);
            std::get<1>(ports).try_put(db); // return the resource
        });
#else
    node_type root_node(g, 1, std::tie(root_limiter),
        [&](int input, ports_type& ports, counting_resource* root) {
#if USE_TRACE > 0
            std::unique_ptr<ScopedTraceEvent> trace = make_event(trace_collector, input, ROOT_NODE_TID);
#endif
            root->use();
            std::this_thread::sleep_for(std::chrono::milliseconds(genie_sleep_time_ms)); // Simulate work
            std::get<0>(ports).try_put(input);
        });

    node_type genie_node(g, 1, std::tie(genie_limiter),
        [&](int input, ports_type& ports, counting_resource* genie) {
#if USE_TRACE > 0
            std::unique_ptr<ScopedTraceEvent> trace = make_event(trace_collector, input, GENIE_NODE_TID);
#endif
            genie->use();
            std::this_thread::sleep_for(std::chrono::milliseconds(genie_sleep_time_ms)); // Simulate work
            std::get<0>(ports).try_put(input);
        });

    node_type root_genie_node(g, 1, std::tie(root_limiter, genie_limiter),
        [&](int input, ports_type& ports, counting_resource* root, counting_resource* genie) {
#if USE_TRACE > 0
            std::unique_ptr<ScopedTraceEvent> trace = make_event(trace_collector, input, ROOT_GENIE_NODE_TID);
#endif
            root->use();
            genie->use();
            std::this_thread::sleep_for(std::chrono::milliseconds(genie_sleep_time_ms)); // Simulate work
            std::get<0>(ports).try_put(input);
        });

    node_type_0 propagating_node(g, unlimited, 
        [&](const int& input) {
#if USE_TRACE > 0
            std::unique_ptr<ScopedTraceEvent> trace = make_event(trace_collector, input, PROPAGATING_NODE_TID);
#endif
            std::this_thread::sleep_for(std::chrono::milliseconds(cycle_sleep_time_ms)); // Simulate work
            return input;
        });

        node_type calibration_a_node(g, unlimited, std::tie(db_limiter),
            [&](int input, ports_type& ports, counting_resource* db) {
#if USE_TRACE > 0
                std::unique_ptr<ScopedTraceEvent> trace = make_event(trace_collector, input, CALIBRATION_A_NODE_TID);
#endif
                db->use();
                std::this_thread::sleep_for(std::chrono::milliseconds(cycle_sleep_time_ms)); // Simulate work
                std::get<0>(ports).try_put(input);
            });

        node_type calibration_b_node(g, unlimited, std::tie(db_limiter),
            [&](int input, ports_type& ports, counting_resource* db) {
#if USE_TRACE > 0
                std::unique_ptr<ScopedTraceEvent> trace = make_event(trace_collector, input, CALIBRATION_B_NODE_TID);
#endif
                db->use();
                std::this_thread::sleep_for(std::chrono::milliseconds(cycle_sleep_time_ms)); // Simulate work
                std::get<0>(ports).try_put(input);
            });

        node_type calibration_c_node(g, 1, std::tie(db_limiter),
            [&](int input, ports_type& ports, counting_resource* db) {
#if USE_TRACE > 0
                std::unique_ptr<ScopedTraceEvent> trace = make_event(trace_collector, input, CALIBRATION_C_NODE_TID);
#endif
                db->use();
                std::this_thread::sleep_for(std::chrono::milliseconds(cycle_sleep_time_ms)); // Simulate work
                std::get<0>(ports).try_put(input);
            });
#endif


#if USE_MODE == 0
    make_edge(start, std::get<0>(root_node.input_ports()));
    make_edge(start, std::get<0>(root_genie_node.input_ports()));
    make_edge(start, std::get<0>(genie_node.input_ports()));
    make_edge(start, propagating_node);
    make_edge(start, std::get<0>(calibration_a_node.input_ports()));
    make_edge(start, std::get<0>(calibration_b_node.input_ports()));
    make_edge(start, std::get<0>(calibration_c_node.input_ports()));

    make_edge(root_limiter, std::get<1>(root_node.input_ports()));
    make_edge(root_limiter, std::get<1>(root_genie_node.input_ports()));
    make_edge(std::get<1>(root_node.output_ports()), root_limiter);
    make_edge(std::get<1>(root_genie_node.output_ports()), root_limiter);

    make_edge(genie_limiter, std::get<1>(genie_node.input_ports()));
    make_edge(genie_limiter, std::get<2>(root_genie_node.input_ports()));
    make_edge(std::get<1>(genie_node.output_ports()), genie_limiter);
    make_edge(std::get<2>(root_genie_node.output_ports()), genie_limiter);

    make_edge(db_limiter, std::get<1>(calibration_a_node.input_ports()));
    make_edge(db_limiter, std::get<1>(calibration_b_node.input_ports()));
    make_edge(db_limiter, std::get<1>(calibration_c_node.input_ports()));

    make_edge(std::get<1>(calibration_a_node.output_ports()), db_limiter);
    make_edge(std::get<1>(calibration_b_node.output_ports()), db_limiter);
    make_edge(std::get<1>(calibration_c_node.output_ports()), db_limiter);
#else
    make_edge(start, root_node);
    make_edge(start, root_genie_node);
    make_edge(start, genie_node);
    make_edge(start, propagating_node);
    make_edge(start, calibration_a_node);
    make_edge(start, calibration_b_node);
    make_edge(start, calibration_c_node);
#endif

    auto end_construction_time = std::chrono::high_resolution_clock::now();
    std::chrono::high_resolution_clock::time_point start_execution_time = std::chrono::high_resolution_clock::now();

    for (int i = -1; i < num_executions; ++i) {
        if (i == 0) {
            root_resource.counter = 0;
            genie_resource.counter = 0;
            db_resource_1.counter = 0;
            db_resource_2.counter = 0;
            start_execution_time = std::chrono::high_resolution_clock::now();
        }

        for (int j = 0; j < num_inputs; ++j) {
            start.try_put(j);

            // Add delay between messages (except after last message)
            if (j < num_inputs - 1 && delay_ms > 0) {
                std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(delay_ms));
            }
        }
        g.wait_for_all();
    }
    auto end_execution_time = std::chrono::high_resolution_clock::now();

    // Validate resource usages using helper
    int expected_uses_root_genie = num_inputs * 2 * num_executions;
    validate_resource_usage(root_resource, expected_uses_root_genie, "genie_bench:root_resource");
    validate_resource_usage(genie_resource, expected_uses_root_genie, "genie_bench:genie_resource");

    // Validate db resources: 3 nodes share 2 resources from db_limiter
    int expected_uses_db_total = num_inputs * 3 * num_executions;
    std::size_t actual_db_uses = db_resource_1.counter.load() + db_resource_2.counter.load();
    if (actual_db_uses != expected_uses_db_total) {
        std::cerr << "Error: genie_bench db_resources were used "
                  << actual_db_uses << " times total, expected "
                  << expected_uses_db_total << " times." << std::endl;
    }

    return {end_construction_time - start_construction_time, end_execution_time - start_execution_time};
}

// returns both the time to construct the graph and the time to execute the graph
std::tuple<std::chrono::duration<double>, std::chrono::duration<double>>
run_genie_diamond_bench(int num_executions = 10, int num_inputs = 100, double generation_rate = 1.0) {
    auto start_construction_time = std::chrono::high_resolution_clock::now();

    // Calculate delay between messages based on generation rate
    // Genie Diamond: max(genie, root) + genie_root = 10ms + 10ms = 20ms
    const double total_graph_time_ms = 2.0 * genie_sleep_time_ms;
    const double delay_ms = total_graph_time_ms / generation_rate;

#if USE_TRACE > 0
    std::unique_ptr<TraceCollector> trace_collector = make_trace_collector("genie_diamond_bench", num_executions, num_inputs);
    trace_collector->add_thread_name(ROOT_NODE_TID, "root_node");
    trace_collector->add_thread_name(GENIE_NODE_TID, "genie_node");
    trace_collector->add_thread_name(ROOT_GENIE_NODE_TID, "root_genie_node");
#endif

    counting_resource root_resource;
    counting_resource genie_resource;
    counting_resource db_resource_1;
    counting_resource db_resource_2;

    using namespace oneapi::tbb::flow;
   graph g;

#if USE_MODE == 0
    // providers of resources
    buffer_node<counting_resource*> root_limiter(g);
    root_limiter.try_put(&root_resource);
    buffer_node<counting_resource*> genie_limiter(g);
    genie_limiter.try_put(&genie_resource);
    buffer_node<counting_resource*> db_limiter(g);
    db_limiter.try_put(&db_resource_1);
    db_limiter.try_put(&db_resource_2);


    using node_type_0 = function_node<int, int>;
    using node_type_1 = resource_composite_node<std::tuple<int, counting_resource*>>;
    using node_type_2 = resource_composite_node<std::tuple<std::tuple<int, int>, counting_resource*, counting_resource*>>;
#else
    // providers of resources
    limiter_type<counting_resource*> root_limiter(&root_resource);
    limiter_type<counting_resource*> genie_limiter(&genie_resource);
    limiter_type<counting_resource*> db_limiter(&db_resource_1, &db_resource_2);

    using node_type_0 = function_node<int, int>;
    using node_type_1 = resource_limited_node<int, std::tuple<int>>;
    using node_type_2 = resource_limited_node<std::tuple<int, int>, std::tuple<std::tuple<int, int>>>;
    using ports_type = typename node_type_1::output_ports_type;
#endif

    broadcast_node<int> start(g);

    #if USE_MODE == 0
    using mfn_ports_1 = typename multifunction_node<std::tuple<int, counting_resource*>, std::tuple<int, counting_resource*>>::output_ports_type;
    using mfn_ports_2 = typename multifunction_node<std::tuple<std::tuple<int, int>, counting_resource*, counting_resource*>, std::tuple<std::tuple<int, int>, counting_resource*, counting_resource*>>::output_ports_type;

    node_type_1 root_node(g, 1,
        [&](const std::tuple<int, counting_resource*> &input_tuple, mfn_ports_1& ports) {
            auto [input, root] = input_tuple;
#if USE_TRACE > 0
            std::unique_ptr<ScopedTraceEvent> trace = make_event(trace_collector, input, ROOT_NODE_TID);
#endif
            root->use();
            std::this_thread::sleep_for(std::chrono::milliseconds(genie_sleep_time_ms)); // Simulate work
            std::get<0>(ports).try_put(input);
            std::get<1>(ports).try_put(root); // return the resource
        });

    node_type_1 genie_node(g, 1,
        [&](const std::tuple<int, counting_resource*>& input_tuple, mfn_ports_1& ports) {
            auto [input, genie] = input_tuple;
#if USE_TRACE > 0
            std::unique_ptr<ScopedTraceEvent> trace = make_event(trace_collector, input, GENIE_NODE_TID);
#endif
            genie->use();
            std::this_thread::sleep_for(std::chrono::milliseconds(genie_sleep_time_ms)); // Simulate work
            std::get<0>(ports).try_put(input);
            std::get<1>(ports).try_put(genie); // return the resource
        });

    node_type_2 root_genie_node(g, 1,
        [&](const std::tuple<std::tuple<int, int>, counting_resource*, counting_resource*> &input_tuple, mfn_ports_2& ports) {
            auto [inputs, root, genie] = input_tuple;
            auto [input_from_root, input_from_genie] = inputs;
#if USE_TRACE > 0
            std::unique_ptr<ScopedTraceEvent> trace = make_event(trace_collector, input_from_root, ROOT_GENIE_NODE_TID);
#endif
            root->use();
            genie->use();
            std::this_thread::sleep_for(std::chrono::milliseconds(genie_sleep_time_ms)); // Simulate work
            std::get<0>(ports).try_put(std::make_tuple(input_from_root, input_from_genie));
            std::get<1>(ports).try_put(root); // return the resource
            std::get<2>(ports).try_put(genie); // return the resource
        });

    node_type_0 propagating_node(g, unlimited, 
        [&](const int& input) {
#if USE_TRACE > 0
            std::unique_ptr<ScopedTraceEvent> trace = make_event(trace_collector, input, PROPAGATING_NODE_TID);
#endif
            std::this_thread::sleep_for(std::chrono::milliseconds(cycle_sleep_time_ms)); // Simulate work
            return input;
        }); 

    node_type_1 calibration_a_node(g, unlimited,
        [&](const std::tuple<int, counting_resource*>& input_tuple, mfn_ports_1& ports) {
            auto [input, db] = input_tuple;
#if USE_TRACE > 0
            std::unique_ptr<ScopedTraceEvent> trace = make_event(trace_collector, input, CALIBRATION_A_NODE_TID);
#endif
            db->use();
            std::this_thread::sleep_for(std::chrono::milliseconds(cycle_sleep_time_ms)); // Simulate work
            std::get<0>(ports).try_put(input);
            std::get<1>(ports).try_put(db); // return the resource
        });

    node_type_1 calibration_b_node(g, unlimited,
        [&](const std::tuple<int, counting_resource*>& input_tuple, mfn_ports_1& ports) {
            auto [input, db] = input_tuple;
#if USE_TRACE > 0
            std::unique_ptr<ScopedTraceEvent> trace = make_event(trace_collector, input, CALIBRATION_B_NODE_TID);
#endif
            db->use();
            std::this_thread::sleep_for(std::chrono::milliseconds(cycle_sleep_time_ms)); // Simulate work
            std::get<0>(ports).try_put(input);
            std::get<1>(ports).try_put(db); // return the resource
        });

    node_type_1 calibration_c_node(g, 1,
        [&](const std::tuple<int, counting_resource*>& input_tuple, mfn_ports_1& ports) {
            auto [input, db] = input_tuple;
#if USE_TRACE > 0
            std::unique_ptr<ScopedTraceEvent> trace = make_event(trace_collector, input, CALIBRATION_C_NODE_TID);
#endif
            db->use();
            std::this_thread::sleep_for(std::chrono::milliseconds(cycle_sleep_time_ms)); // Simulate work
            std::get<0>(ports).try_put(input);
            std::get<1>(ports).try_put(db); // return the resource
        });
#else
    node_type_1 root_node(g, 1, std::tie(root_limiter),
        [&](int input, typename node_type_1::output_ports_type& ports, counting_resource* root) {
#if USE_TRACE > 0
            std::unique_ptr<ScopedTraceEvent> trace = make_event(trace_collector, input, ROOT_NODE_TID);
#endif
            root->use();
            std::this_thread::sleep_for(std::chrono::milliseconds(genie_sleep_time_ms)); // Simulate work
            std::get<0>(ports).try_put(input);
        });

    node_type_1 genie_node(g, 1, std::tie(genie_limiter),
        [&](int input, typename node_type_1::output_ports_type& ports, counting_resource* genie) {
#if USE_TRACE > 0
            std::unique_ptr<ScopedTraceEvent> trace = make_event(trace_collector, input, GENIE_NODE_TID);
#endif
            genie->use();
            std::this_thread::sleep_for(std::chrono::milliseconds(genie_sleep_time_ms)); // Simulate work
            std::get<0>(ports).try_put(input);
        });

    node_type_2 root_genie_node(g, 1, std::tie(root_limiter, genie_limiter),
        [&](std::tuple<int, int> inputs, typename node_type_2::output_ports_type& ports, counting_resource* root, counting_resource* genie) {
            auto [input_from_root, input_from_genie] = inputs;
#if USE_TRACE > 0
            std::unique_ptr<ScopedTraceEvent> trace = make_event(trace_collector, input_from_root, ROOT_GENIE_NODE_TID);
#endif
            root->use();
            genie->use();
            std::this_thread::sleep_for(std::chrono::milliseconds(genie_sleep_time_ms)); // Simulate work
            std::get<0>(ports).try_put(std::make_tuple(input_from_root, input_from_genie));
        });

    node_type_0 propagating_node(g, unlimited,
        [&](const int& input) {
#if USE_TRACE > 0
            std::unique_ptr<ScopedTraceEvent> trace = make_event(trace_collector, input, PROPAGATING_NODE_TID);
#endif
            std::this_thread::sleep_for(std::chrono::milliseconds(cycle_sleep_time_ms)); // Simulate work
            return input;
        });

        node_type_1 calibration_a_node(g, unlimited, std::tie(db_limiter),
            [&](int input, ports_type& ports, counting_resource* db) {
#if USE_TRACE > 0
                std::unique_ptr<ScopedTraceEvent> trace = make_event(trace_collector, input, CALIBRATION_A_NODE_TID);
#endif
                db->use();
                std::this_thread::sleep_for(std::chrono::milliseconds(cycle_sleep_time_ms)); // Simulate work
                std::get<0>(ports).try_put(input);
            });

        node_type_1 calibration_b_node(g, unlimited, std::tie(db_limiter),
            [&](int input, ports_type& ports, counting_resource* db) {
#if USE_TRACE > 0
                std::unique_ptr<ScopedTraceEvent> trace = make_event(trace_collector, input, CALIBRATION_B_NODE_TID);
#endif
                db->use();
                std::this_thread::sleep_for(std::chrono::milliseconds(cycle_sleep_time_ms)); // Simulate work
                std::get<0>(ports).try_put(input);
            });

        node_type_1 calibration_c_node(g, 1, std::tie(db_limiter),
            [&](int input, ports_type& ports, counting_resource* db) {
#if USE_TRACE > 0
                std::unique_ptr<ScopedTraceEvent> trace = make_event(trace_collector, input, CALIBRATION_C_NODE_TID);
#endif
                db->use();
                std::this_thread::sleep_for(std::chrono::milliseconds(cycle_sleep_time_ms)); // Simulate work
                std::get<0>(ports).try_put(input);
            });
#endif

    // Key functions for key_matching join - both ports receive int and use it as key
    auto key_func = [](int i) { return i; };
    join_node<std::tuple<int, int>, key_matching<int>> middle_join(g, key_func, key_func);

#if USE_MODE == 0
    make_edge(start, std::get<0>(root_node.input_ports()));
    make_edge(start, std::get<0>(genie_node.input_ports()));
    make_edge(std::get<0>(root_node.output_ports()), oneapi::tbb::flow::input_port<0>(middle_join));
    make_edge(std::get<0>(genie_node.output_ports()), oneapi::tbb::flow::input_port<1>(middle_join));
    make_edge(middle_join, std::get<0>(root_genie_node.input_ports()));

    make_edge(root_limiter, std::get<1>(root_node.input_ports()));
    make_edge(root_limiter, std::get<1>(root_genie_node.input_ports()));
    make_edge(std::get<1>(root_node.output_ports()), root_limiter);
    make_edge(std::get<1>(root_genie_node.output_ports()), root_limiter);

    make_edge(genie_limiter, std::get<1>(genie_node.input_ports()));
    make_edge(genie_limiter, std::get<2>(root_genie_node.input_ports()));
    make_edge(std::get<1>(genie_node.output_ports()), genie_limiter);
    make_edge(std::get<2>(root_genie_node.output_ports()), genie_limiter);

    make_edge(start, propagating_node);
    make_edge(start, std::get<0>(calibration_a_node.input_ports()));
    make_edge(start, std::get<0>(calibration_b_node.input_ports()));
    make_edge(start, std::get<0>(calibration_c_node.input_ports()));
    make_edge(db_limiter, std::get<1>(calibration_a_node.input_ports()));
    make_edge(db_limiter, std::get<1>(calibration_b_node.input_ports()));
    make_edge(db_limiter, std::get<1>(calibration_c_node.input_ports()));
    make_edge(std::get<1>(calibration_a_node.output_ports()), db_limiter);
    make_edge(std::get<1>(calibration_b_node.output_ports()), db_limiter);
    make_edge(std::get<1>(calibration_c_node.output_ports()), db_limiter);
#else
    make_edge(start, root_node);
    make_edge(start, genie_node);
    make_edge(root_node, oneapi::tbb::flow::input_port<0>(middle_join));
    make_edge(genie_node, oneapi::tbb::flow::input_port<1>(middle_join));
    make_edge(middle_join, root_genie_node);
    make_edge(start, propagating_node);
    make_edge(start, calibration_a_node);
    make_edge(start, calibration_b_node);
    make_edge(start, calibration_c_node);
#endif

    auto end_construction_time = std::chrono::high_resolution_clock::now();
    std::chrono::high_resolution_clock::time_point start_execution_time = std::chrono::high_resolution_clock::now();

    for (int i = -1; i < num_executions; ++i) {
        if (i == 0) {
            root_resource.counter = 0;
            genie_resource.counter = 0;
            db_resource_1.counter = 0;
            db_resource_2.counter = 0;
            start_execution_time = std::chrono::high_resolution_clock::now();
        }

        for (int j = 0; j < num_inputs; ++j) {
            start.try_put(j);

            // Add delay between messages (except after last message)
            if (j < num_inputs - 1 && delay_ms > 0) {
                std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(delay_ms));
            }
        }
        g.wait_for_all();
    }
    auto end_execution_time = std::chrono::high_resolution_clock::now();

    // Validate both resource usages using helper
    int expected_uses = num_inputs * 2 * num_executions;
    validate_resource_usage(root_resource, expected_uses, "genie_diamond_bench:root_resource");
    validate_resource_usage(genie_resource, expected_uses, "genie_diamond_bench:genie_resource");

    // Validate db resources: 3 nodes share 2 resources from db_limiter
    int expected_uses_db_total = num_inputs * 3 * num_executions;
    std::size_t actual_db_uses = db_resource_1.counter.load() + db_resource_2.counter.load();
    if (actual_db_uses != expected_uses_db_total) {
        std::cerr << "Error: genie_bench db_resources were used "
                  << actual_db_uses << " times total, expected "
                  << expected_uses_db_total << " times." << std::endl;
    }

    return {end_construction_time - start_construction_time, end_execution_time - start_execution_time};
}

// Baseline Cycle: self-propagating chain with N nodes that executes M times
// Measures overhead of resource acquisition with no contention
std::tuple<std::chrono::duration<double>, std::chrono::duration<double>>
run_baseline_cycle_bench(int num_executions = 10, int num_inputs = 100, int num_nodes = 10, double generation_rate = 1.0, int num_resources = 1) {
    auto start_construction_time = std::chrono::high_resolution_clock::now();
    // Note: generation_rate is not used in baseline_cycle (self-propagating cycle)

#if USE_TRACE > 0
    std::unique_ptr<TraceCollector> trace_collector = make_trace_collector("baseline_cycle_bench", num_executions, num_inputs);
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

    std::vector<node_type*> nodes;

    // Create N nodes
    for (int i = 0; i < num_nodes; ++i) {
#if USE_MODE == 0
        nodes.push_back(new node_type(g, 1,
            [&, i](const std::tuple<int, counting_resource*>& input_tuple, mfn_ports& ports) {
                auto [input, resource] = input_tuple;

                // N_0 is conditional - check stop condition BEFORE using resource
                if (i == 0 && input >= num_inputs) {
                    std::get<1>(ports).try_put(resource); // return resource only, don't execute
                    return;
                }

#if USE_TRACE > 0
                std::unique_ptr<ScopedTraceEvent> trace = make_event(trace_collector, input, i + 1);
#endif
                resource->use();
                std::this_thread::sleep_for(std::chrono::milliseconds(cycle_sleep_time_ms));
                std::get<0>(ports).try_put(input + (i == num_nodes - 1 ? 1 : 0)); // increment on last node
                std::get<1>(ports).try_put(resource);
            }));
#else
        nodes.push_back(new node_type(g, 1, std::tie(resource_limiter),
            [&, i](int input, ports_type& ports, counting_resource* resource) {
                // N_0 is conditional - check stop condition BEFORE using resource
                if (i == 0 && input >= num_inputs) {
                    // Don't send message forward, cycle stops, don't execute
                    return;
                }

#if USE_TRACE > 0
                std::unique_ptr<ScopedTraceEvent> trace = make_event(trace_collector, input, i + 1);
#endif
                resource->use();
                std::this_thread::sleep_for(std::chrono::milliseconds(cycle_sleep_time_ms));
                std::get<0>(ports).try_put(input + (i == num_nodes - 1 ? 1 : 0));
            }));
#endif
    }

    // Connect nodes in a cycle
    for (int i = 0; i < num_nodes - 1; ++i) {
#if USE_MODE == 0
        make_edge(std::get<0>(nodes[i]->output_ports()), std::get<0>(nodes[i + 1]->input_ports()));
        make_edge(resource_limiter, std::get<1>(nodes[i]->input_ports()));
        make_edge(std::get<1>(nodes[i]->output_ports()), resource_limiter);
#else
        make_edge(*nodes[i], *nodes[i + 1]);
#endif
    }

    // Close the cycle: last node connects back to first
#if USE_MODE == 0
    make_edge(std::get<0>(nodes[num_nodes - 1]->output_ports()), std::get<0>(nodes[0]->input_ports()));
    make_edge(resource_limiter, std::get<1>(nodes[num_nodes - 1]->input_ports()));
    make_edge(std::get<1>(nodes[num_nodes - 1]->output_ports()), resource_limiter);
#else
    make_edge(*nodes[num_nodes - 1], *nodes[0]);
#endif

    auto end_construction_time = std::chrono::high_resolution_clock::now();

    // Execute with warm-up + num_executions runs
    std::chrono::high_resolution_clock::time_point start_execution_time;

    for (int i = -1; i < num_executions; ++i) {
        if (i == 0) {
            // Reset all resource counters
            for (auto& res : resources) {
                res.counter = 0;
            }
            start_execution_time = std::chrono::high_resolution_clock::now();
        }

        // Start the cycle with initial message
#if USE_MODE == 0
        std::get<0>(nodes[0]->input_ports()).try_put(0);
#else
        nodes[0]->try_put(0);
#endif

        g.wait_for_all();
    }

    auto end_execution_time = std::chrono::high_resolution_clock::now();

    // Validate resource usage using helper
    int expected_uses = num_inputs * num_nodes * num_executions;
    validate_resource_usage(resources, expected_uses, "baseline_cycle_bench");

    // Cleanup
    for (auto* node : nodes) {
        delete node;
    }

    return {end_construction_time - start_construction_time, end_execution_time - start_execution_time};
}

// Performance of a Chain: input_node drives N sequential nodes
std::tuple<std::chrono::duration<double>, std::chrono::duration<double>>
run_chain_bench(int num_executions = 10, int num_inputs = 100, int num_nodes = 10, double generation_rate = 1.0, int num_resources = 1) {
    auto start_construction_time = std::chrono::high_resolution_clock::now();

    // Calculate delay between messages based on generation rate
    // Chain: sum of all stages = num_nodes * cycle_sleep_time_ms
    const double total_graph_time_ms = num_nodes * cycle_sleep_time_ms;
    const double delay_ms = total_graph_time_ms / generation_rate;

#if USE_TRACE > 0
    std::unique_ptr<TraceCollector> trace_collector = make_trace_collector("chain_bench", num_executions, num_inputs);
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

    // Create N nodes in a chain
    for (int i = 0; i < num_nodes; ++i) {
#if USE_MODE == 0
        nodes.push_back(new node_type(g, 1,
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
        nodes.push_back(new node_type(g, 1, std::tie(resource_limiter),
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

    // Connect source to first node
#if USE_MODE == 0
    make_edge(source, std::get<0>(nodes[0]->input_ports()));
#else
    make_edge(source, *nodes[0]);
#endif

    // Connect nodes in a chain
    for (int i = 0; i < num_nodes; ++i) {
        if (i < num_nodes - 1) {
#if USE_MODE == 0
            make_edge(std::get<0>(nodes[i]->output_ports()), std::get<0>(nodes[i + 1]->input_ports()));
#else
            make_edge(*nodes[i], *nodes[i + 1]);
#endif
        }
#if USE_MODE == 0
        make_edge(resource_limiter, std::get<1>(nodes[i]->input_ports()));
        make_edge(std::get<1>(nodes[i]->output_ports()), resource_limiter);
#endif
    }

    auto end_construction_time = std::chrono::high_resolution_clock::now();

    // Use execution loop helper
    std::chrono::high_resolution_clock::time_point start_execution_time;
    auto end_execution_time = run_execution_loop(g, source, resources,
                                                  num_executions, num_inputs,
                                                  generation_rate, delay_ms, start_execution_time);

    // Validate resource usage using helper
    int expected_uses = num_inputs * num_nodes * num_executions;
    validate_resource_usage(resources, expected_uses, "chain_bench");

    // Cleanup
    for (auto* node : nodes) {
        delete node;
    }

    return {end_construction_time - start_construction_time, end_execution_time - start_execution_time};
}

// Performance for Siblings: input_node drives N sibling nodes
std::tuple<std::chrono::duration<double>, std::chrono::duration<double>>
run_siblings_bench(int num_executions = 10, int num_inputs = 100, int num_nodes = 10, double generation_rate = 1.0, int num_resources = 1) {
    auto start_construction_time = std::chrono::high_resolution_clock::now();

    // Calculate delay between messages based on generation rate
    // Siblings: sum of all stages = num_nodes * cycle_sleep_time_ms
    const double total_graph_time_ms = num_nodes * cycle_sleep_time_ms;
    const double delay_ms = total_graph_time_ms / generation_rate;

#if USE_TRACE > 0
    std::unique_ptr<TraceCollector> trace_collector = make_trace_collector("siblings_bench", num_executions, num_inputs);
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
        nodes.push_back(new node_type(g, 1,
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
        nodes.push_back(new node_type(g, 1, std::tie(resource_limiter),
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
                                                  generation_rate, delay_ms, start_execution_time);

    // Validate resource usage using helper
    int expected_uses = num_inputs * num_nodes * num_executions;
    validate_resource_usage(resources, expected_uses, "siblings_bench");

    // Cleanup
    for (auto* node : nodes) {
        delete node;
    }

    return {end_construction_time - start_construction_time, end_execution_time - start_execution_time};
}

// Performance of a Tree: binary tree structure
std::tuple<std::chrono::duration<double>, std::chrono::duration<double>>
run_tree_bench(int num_executions = 10, int num_inputs = 100, int num_nodes = 10, double generation_rate = 1.0, int num_resources = 1) {
    auto start_construction_time = std::chrono::high_resolution_clock::now();

    // Calculate delay between messages based on generation rate
    // Tree: sum of all stages = num_nodes * cycle_sleep_time_ms
    const double total_graph_time_ms = num_nodes * cycle_sleep_time_ms;
    const double delay_ms = total_graph_time_ms / generation_rate;

#if USE_TRACE > 0
    std::unique_ptr<TraceCollector> trace_collector = make_trace_collector("tree_bench", num_executions, num_inputs);
#endif

    std::vector<counting_resource> resources(num_resources);

    using namespace oneapi::tbb::flow;
    graph g;

    // Tree benchmark has unique output type for modes 1&2 (binary fanout)
#if USE_MODE == 0
    using node_types_t = node_types<USE_MODE, int, counting_resource*>;
    using node_type = typename node_types_t::node_type;
    using mfn_ports = typename node_types_t::ports_type;
#else
    using node_type = resource_limited_node<int, std::tuple<int, int>>;
    using ports_type = typename node_type::output_ports_type;
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

    // Calculate tree depth from num_nodes: largest complete binary tree with <= num_nodes
    // depth k means 2^k - 1 nodes, so k = floor(log2(num_nodes + 1))
    int tree_depth = 0;
    int temp = num_nodes + 1;
    while (temp > 1) {
        temp >>= 1;
        ++tree_depth;
    }
    // Adjust if 2^tree_depth - 1 > num_nodes
    if (((1 << tree_depth) - 1) > num_nodes) {
        --tree_depth;
    }
    int total_nodes = (1 << tree_depth) - 1;

    std::cout << "Tree configuration: depth=" << tree_depth << ", total_nodes=" << total_nodes
              << " (from num_nodes=" << num_nodes << ")" << std::endl;
    std::vector<node_type*> nodes(total_nodes);

    // Create all nodes in the binary tree
    for (int i = 0; i < total_nodes; ++i) {
#if USE_MODE == 0
        nodes[i] = new node_type(g, 1,
            [&, i](const std::tuple<int, counting_resource*>& input_tuple, mfn_ports& ports) {
                auto [input, resource] = input_tuple;
#if USE_TRACE > 0
                std::unique_ptr<ScopedTraceEvent> trace = make_event(trace_collector, input, i + 1);
#endif
                resource->use();
                std::this_thread::sleep_for(std::chrono::milliseconds(cycle_sleep_time_ms));
                std::get<0>(ports).try_put(input);
                std::get<1>(ports).try_put(resource);
            });
#else
        nodes[i] = new node_type(g, 1, std::tie(resource_limiter),
            [&, i](int input, ports_type& ports, counting_resource* resource) {
#if USE_TRACE > 0
                std::unique_ptr<ScopedTraceEvent> trace = make_event(trace_collector, input, i + 1);
#endif
                resource->use();
                std::this_thread::sleep_for(std::chrono::milliseconds(cycle_sleep_time_ms));
                // Send to both output ports for binary tree fanout
                std::get<0>(ports).try_put(input);
                std::get<1>(ports).try_put(input);
            });
#endif
    }

    // Connect source to root node (index 0)
#if USE_MODE == 0
    make_edge(source, std::get<0>(nodes[0]->input_ports()));
#else
    make_edge(source, *nodes[0]);
#endif

    // Connect tree structure: node i connects to children at 2*i+1 and 2*i+2
    for (int i = 0; i < total_nodes; ++i) {
        int left_child = 2 * i + 1;
        int right_child = 2 * i + 2;

#if USE_MODE == 0
        // For mode 0, we only have one output for messages
        if (left_child < total_nodes) {
            make_edge(std::get<0>(nodes[i]->output_ports()), std::get<0>(nodes[left_child]->input_ports()));
        }
        if (right_child < total_nodes) {
            make_edge(std::get<0>(nodes[i]->output_ports()), std::get<0>(nodes[right_child]->input_ports()));
        }
        make_edge(resource_limiter, std::get<1>(nodes[i]->input_ports()));
        make_edge(std::get<1>(nodes[i]->output_ports()), resource_limiter);
#else
        // For modes 1 and 2, we have two output ports for binary fanout
        if (left_child < total_nodes) {
            make_edge(output_port<0>(*nodes[i]), *nodes[left_child]);
        }
        if (right_child < total_nodes) {
            make_edge(output_port<1>(*nodes[i]), *nodes[right_child]);
        }
#endif
    }

    auto end_construction_time = std::chrono::high_resolution_clock::now();

    // Use execution loop helper
    std::chrono::high_resolution_clock::time_point start_execution_time;
    auto end_execution_time = run_execution_loop(g, source, resources,
                                                  num_executions, num_inputs,
                                                  generation_rate, delay_ms, start_execution_time);

    // Validate resource usage using helper
    int expected_uses = num_inputs * total_nodes * num_executions;
    validate_resource_usage(resources, expected_uses, "tree_bench");

    // Cleanup
    for (auto* node : nodes) {
        delete node;
    }

    return {end_construction_time - start_construction_time, end_execution_time - start_execution_time};
}

void print_results(const std::string& bench_name, std::chrono::duration<double> construction_time, std::chrono::duration<double> execution_time, int num_executions, int num_inputs, double generation_rate, double delay_ms, double total_graph_time_ms) {
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

int main(int argc, char* argv[]) {
    // usage: resource_limited_ubenches [benchmark] [num_executions] [num_inputs] [generation_rate] [num_nodes] [num_resources]
    // benchmark: genie, genie_diamond, baseline_cycle, chain, siblings, tree, all
    // USE_MODE: 0 = flow graph with join_node, 1 = flow graph with resource limiting, 2 = pressure-aware resource limiter
    // USE_TRACE: 0 = no tracing, 1 = eager, 2 = lazy

    std::string benchmark = "all";
    int num_executions = 1;
    int num_inputs = 100;
    double generation_rate = 5.0;
    int num_nodes = 10;
    int num_resources = 1;

    // Parse command line arguments
    if (argc >= 2) {
        benchmark = argv[1];
    }
    if (argc >= 3) {
        num_executions = std::atoi(argv[2]);
    }
    if (argc >= 4) {
        num_inputs = std::atoi(argv[3]);
    }
    if (argc >= 5) {
        generation_rate = std::atof(argv[4]);
    }
    if (argc >= 6) {
        num_nodes = std::atoi(argv[5]);
    }
    if (argc >= 7) {
        num_resources = std::atoi(argv[6]);
    }

    std::cout << "USE_MODE=" << USE_MODE << " (";
    if (USE_MODE == 0) std::cout << "join_node";
    else if (USE_MODE == 1) std::cout << "resource_limiter";
    else if (USE_MODE == 2) std::cout << "priority_aware_resource_limiter";
    std::cout << ")\n";
    std::cout << "Benchmark: " << benchmark << "\n\n";

    std::cout << "macro settings:\n";
#if USE_MODE == 0
    std::cout << "  USE_MODE=0: flow graph with join_node and explicit resource passing\n";
#elif USE_MODE == 1
    std::cout << "  USE_MODE=1: flow graph with resource_limited_node and notify all resource limiter\n";
#elif USE_MODE == 2
    std::cout << "  USE_MODE=2: flow graph with priority-aware resource limiter\n";
    std::cout << "  __TBB_USE_PRESSURE=" << __TBB_USE_PRESSURE << "\n";
    std::cout << "  __TBB_USE_TIMESTAMP_IN_REQUEST_ID=" << __TBB_USE_TIMESTAMP_IN_REQUEST_ID << "\n";
    std::cout << "  __TBB_USE_CONSUMER_LOCAL_COUNTER_FOR_REQUEST_ID=" << __TBB_USE_CONSUMER_LOCAL_COUNTER_FOR_REQUEST_ID << "\n";
    std::cout << "  __TBB_USE_NOTIFY_ON_REPORT_PRESSURE=" << __TBB_USE_NOTIFY_ON_REPORT_PRESSURE << "\n";
#endif

    std::cout << "Configuration parameters:\n";
    std::cout << "  num_executions: " << num_executions << "\n";
    std::cout << "  num_inputs: " << num_inputs << "\n";
    std::cout << "  num_nodes/tree_depth: " << num_nodes << "\n";
    std::cout << "  num_resources: " << num_resources << "\n";
    std::cout << "  generation_rate: " << generation_rate << "\n\n";

    if (benchmark == "genie" || benchmark == "all") {
        auto [construction_time, execution_time] = run_genie_bench(num_executions, num_inputs, generation_rate);
        double total_graph_time_ms = 2.0 * genie_sleep_time_ms;
        double delay_ms = total_graph_time_ms / generation_rate;
        print_results("genie_bench", construction_time, execution_time, num_executions, num_inputs,
                      generation_rate, delay_ms, total_graph_time_ms);
        std::cout << "\n";
    }

    if (benchmark == "genie_diamond" || benchmark == "all") {
        auto [diamond_construction_time, diamond_execution_time] = run_genie_diamond_bench(num_executions, num_inputs, generation_rate);
        double total_graph_time_ms = 2.0 * genie_sleep_time_ms;
        double delay_ms = total_graph_time_ms / generation_rate;
        print_results("genie_diamond_bench", diamond_construction_time, diamond_execution_time, num_executions, num_inputs,
                      generation_rate, delay_ms, total_graph_time_ms);
        std::cout << "\n";
    }

    if (benchmark == "baseline_cycle" || benchmark == "all") {
        auto [cycle_construction_time, cycle_execution_time] = run_baseline_cycle_bench(num_executions, num_inputs, num_nodes, generation_rate, num_resources);
        double total_graph_time_ms = 0;  // N/A for self-propagating cycle
        double delay_ms = 0;  // Not applicable
        print_results("baseline_cycle_bench", cycle_construction_time, cycle_execution_time, num_executions, num_inputs,
                      generation_rate, delay_ms, total_graph_time_ms);
        std::cout << "\n";
    }

    if (benchmark == "chain" || benchmark == "all") {
        auto [chain_construction_time, chain_execution_time] = run_chain_bench(num_executions, num_inputs, num_nodes, generation_rate, num_resources);
        double total_graph_time_ms = num_nodes * cycle_sleep_time_ms;
        double delay_ms = total_graph_time_ms / generation_rate;
        print_results("chain_bench", chain_construction_time, chain_execution_time, num_executions, num_inputs,
                      generation_rate, delay_ms, total_graph_time_ms);
        std::cout << "\n";
    }

    if (benchmark == "siblings" || benchmark == "all") {
        auto [siblings_construction_time, siblings_execution_time] = run_siblings_bench(num_executions, num_inputs, num_nodes, generation_rate, num_resources);
        double total_graph_time_ms = num_nodes * cycle_sleep_time_ms;
        double delay_ms = total_graph_time_ms / generation_rate;
        print_results("siblings_bench", siblings_construction_time, siblings_execution_time, num_executions, num_inputs,
                      generation_rate, delay_ms, total_graph_time_ms);
        std::cout << "\n";
    }

    if (benchmark == "tree" || benchmark == "all") {
        auto [tree_construction_time, tree_execution_time] = run_tree_bench(num_executions, num_inputs, num_nodes, generation_rate, num_resources);
        double total_graph_time_ms = num_nodes * cycle_sleep_time_ms;
        double delay_ms = total_graph_time_ms / generation_rate;
        print_results("tree_bench", tree_construction_time, tree_execution_time, num_executions, num_inputs,
                      generation_rate, delay_ms, total_graph_time_ms);
        std::cout << "\n";
    }

    return 0;
}
