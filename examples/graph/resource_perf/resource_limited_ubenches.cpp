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

// USE_TRACE: 0 = no tracing, 1 = eager, 2 = lazy
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
using limiter_type = oneapi::tbb::flow::pressure_aware_resource_limiter<T>;
#endif

#if USE_TRACE > 0
// Thread IDs represent nodes, not actual worker threads
constexpr int ROOT_NODE_TID = 1;
constexpr int GENIE_NODE_TID = 2;
constexpr int ROOT_GENIE_NODE_TID = 3;

std::unique_ptr<TraceCollector> make_trace_collector(std::string_view graph_name, int num_executions = 10, int num_inputs = 100) {
       // Create trace filename based on configuration
    std::ostringstream filename;
    filename << graph_name << "_mode" << (USE_MODE == 0 ? "join" : (USE_MODE == 1 ? "limited" : "pressure"));

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

// returns both the time to construct the graph and the time to execute the graph
std::tuple<std::chrono::duration<double>, std::chrono::duration<double>>
run_genie_bench(int num_executions = 10, int num_inputs = 100) {
    auto start_construction_time = std::chrono::high_resolution_clock::now();

     
#if USE_TRACE > 0
    std::unique_ptr<TraceCollector> trace_collector = make_trace_collector("genie_bench", num_executions, num_inputs);
    trace_collector->add_thread_name(ROOT_NODE_TID, "root_node");
    trace_collector->add_thread_name(GENIE_NODE_TID, "genie_node");
    trace_collector->add_thread_name(ROOT_GENIE_NODE_TID, "root_genie_node");
#endif

    counting_resource root_resource;
    counting_resource genie_resource;

    using namespace oneapi::tbb::flow;
   graph g;

#if USE_MODE == 0
    // providers of resources
    buffer_node<counting_resource*> root_limiter(g);
    root_limiter.try_put(&root_resource);
    buffer_node<counting_resource*> genie_limiter(g);
    genie_limiter.try_put(&genie_resource);

    using node_type_1 = resource_composite_node<std::tuple<int, counting_resource*>>;
    using node_type_2 = resource_composite_node<std::tuple<int, counting_resource*, counting_resource*>>;
#else
    // providers of resources
    limiter_type<counting_resource*> root_limiter(&root_resource);
    limiter_type<counting_resource*> genie_limiter(&genie_resource);

    using node_type = resource_limited_node<int, std::tuple<int>>;
    using ports_type = typename node_type::output_ports_type;
#endif


    const int sleep_time_ms = 10; // Simulated work time for each node


    broadcast_node<int> start(g);


    #if USE_MODE == 0
    using mfn_ports_1 = typename multifunction_node<std::tuple<int, counting_resource*>, std::tuple<int, counting_resource*>>::output_ports_type;
    using mfn_ports_2 = typename multifunction_node<std::tuple<int, counting_resource*, counting_resource*>, std::tuple<int, counting_resource*, counting_resource*>>::output_ports_type;

    node_type_1 root_node(g, 1,
        [&](const std::tuple<int, counting_resource*> &input_tuple, mfn_ports_1& ports) {
            auto [input, root] = input_tuple;
#if USE_TRACE > 0
            std::unique_ptr<ScopedTraceEvent> trace = make_event(trace_collector, input, ROOT_NODE_TID);
#endif
            root->use();
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time_ms)); // Simulate work
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
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time_ms)); // Simulate work
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
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time_ms)); // Simulate work
            std::get<0>(ports).try_put(input);
            std::get<1>(ports).try_put(root); // return the resource
            std::get<2>(ports).try_put(genie); // return the resource
        });
#else
    node_type root_node(g, 1, std::tie(root_limiter),
        [&](int input, ports_type& ports, counting_resource* root) {
#if USE_TRACE > 0
            std::unique_ptr<ScopedTraceEvent> trace = make_event(trace_collector, input, ROOT_NODE_TID);
#endif
            root->use();
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time_ms)); // Simulate work
            std::get<0>(ports).try_put(input);
        });

    node_type genie_node(g, 1, std::tie(genie_limiter),
        [&](int input, ports_type& ports, counting_resource* genie) {
#if USE_TRACE > 0
            std::unique_ptr<ScopedTraceEvent> trace = make_event(trace_collector, input, GENIE_NODE_TID);
#endif
            genie->use();
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time_ms)); // Simulate work
            std::get<0>(ports).try_put(input);
        });

    node_type root_genie_node(g, 1, std::tie(root_limiter, genie_limiter),
        [&](int input, ports_type& ports, counting_resource* root, counting_resource* genie) {
#if USE_TRACE > 0
            std::unique_ptr<ScopedTraceEvent> trace = make_event(trace_collector, input, ROOT_GENIE_NODE_TID);
#endif
            root->use();
            genie->use();
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time_ms)); // Simulate work
            std::get<0>(ports).try_put(input);
        });
#endif



#if USE_MODE == 0
    make_edge(start, std::get<0>(root_node.input_ports()));
    make_edge(start, std::get<0>(root_genie_node.input_ports()));
    make_edge(start, std::get<0>(genie_node.input_ports()));

    make_edge(root_limiter, std::get<1>(root_node.input_ports()));
    make_edge(root_limiter, std::get<1>(root_genie_node.input_ports()));
    make_edge(std::get<1>(root_node.output_ports()), root_limiter);
    make_edge(std::get<1>(root_genie_node.output_ports()), root_limiter);

    make_edge(genie_limiter, std::get<1>(genie_node.input_ports()));
    make_edge(genie_limiter, std::get<2>(root_genie_node.input_ports()));
    make_edge(std::get<1>(genie_node.output_ports()), genie_limiter);
    make_edge(std::get<2>(root_genie_node.output_ports()), genie_limiter);
#else
    make_edge(start, root_node);
    make_edge(start, root_genie_node);
    make_edge(start, genie_node);
#endif

    auto end_construction_time = std::chrono::high_resolution_clock::now();
    std::chrono::high_resolution_clock::time_point start_execution_time = std::chrono::high_resolution_clock::now();
        
    for (int i = -1; i < num_executions; ++i) {
        if (i == 0) {
            root_resource.counter = 0;
            genie_resource.counter = 0;
            start_execution_time = std::chrono::high_resolution_clock::now();
        }

        for (int i = 0; i < num_inputs; ++i) {
            start.try_put(i);
        }
        g.wait_for_all();
    }
    auto end_execution_time = std::chrono::high_resolution_clock::now();

    if (root_resource.counter != num_inputs * 2 * num_executions) {
        std::cerr << "Error: root resource was used " << root_resource.counter
                  << " times, expected " << num_inputs * 2 * num_executions << " times." << std::endl;
    }
    if (genie_resource.counter != num_inputs * 2 * num_executions) {
        std::cerr << "Error: genie resource was used " << genie_resource.counter
                  << " times, expected " << num_inputs * 2 * num_executions << " times." << std::endl;
    }
    return {end_construction_time - start_construction_time, end_execution_time - start_execution_time};
}

// returns both the time to construct the graph and the time to execute the graph
std::tuple<std::chrono::duration<double>, std::chrono::duration<double>>
run_genie_diamond_bench(int num_executions = 10, int num_inputs = 100) {
    auto start_construction_time = std::chrono::high_resolution_clock::now();

     
#if USE_TRACE > 0
    std::unique_ptr<TraceCollector> trace_collector = make_trace_collector("genie_diamond_bench", num_executions, num_inputs);
    trace_collector->add_thread_name(ROOT_NODE_TID, "root_node");
    trace_collector->add_thread_name(GENIE_NODE_TID, "genie_node");
    trace_collector->add_thread_name(ROOT_GENIE_NODE_TID, "root_genie_node");
#endif

    counting_resource root_resource;
    counting_resource genie_resource;

    using namespace oneapi::tbb::flow;
   graph g;

#if USE_MODE == 0
    // providers of resources
    buffer_node<counting_resource*> root_limiter(g);
    root_limiter.try_put(&root_resource);
    buffer_node<counting_resource*> genie_limiter(g);
    genie_limiter.try_put(&genie_resource);

    using node_type_1 = resource_composite_node<std::tuple<int, counting_resource*>>;
    using node_type_2 = resource_composite_node<std::tuple<std::tuple<int, int>, counting_resource*, counting_resource*>>;
#else
    // providers of resources
    limiter_type<counting_resource*> root_limiter(&root_resource);
    limiter_type<counting_resource*> genie_limiter(&genie_resource);

    using node_type_1 = resource_limited_node<int, std::tuple<int>>;
    using node_type_2 = resource_limited_node<std::tuple<int, int>, std::tuple<std::tuple<int, int>>>;
    using ports_type = typename node_type_1::output_ports_type;
#endif

    const int sleep_time_ms = 10; // Simulated work time for each node

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
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time_ms)); // Simulate work
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
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time_ms)); // Simulate work
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
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time_ms)); // Simulate work
            std::get<0>(ports).try_put(std::make_tuple(input_from_root, input_from_genie));
            std::get<1>(ports).try_put(root); // return the resource
            std::get<2>(ports).try_put(genie); // return the resource
        });
#else
    node_type_1 root_node(g, 1, std::tie(root_limiter),
        [&](int input, typename node_type_1::output_ports_type& ports, counting_resource* root) {
#if USE_TRACE > 0
            std::unique_ptr<ScopedTraceEvent> trace = make_event(trace_collector, input, ROOT_NODE_TID);
#endif
            root->use();
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time_ms)); // Simulate work
            std::get<0>(ports).try_put(input);
        });

    node_type_1 genie_node(g, 1, std::tie(genie_limiter),
        [&](int input, typename node_type_1::output_ports_type& ports, counting_resource* genie) {
#if USE_TRACE > 0
            std::unique_ptr<ScopedTraceEvent> trace = make_event(trace_collector, input, GENIE_NODE_TID);
#endif
            genie->use();
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time_ms)); // Simulate work
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
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time_ms)); // Simulate work
            std::get<0>(ports).try_put(std::make_tuple(input_from_root, input_from_genie));
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
#else
    make_edge(start, root_node);
    make_edge(start, genie_node);
    make_edge(root_node, oneapi::tbb::flow::input_port<0>(middle_join));
    make_edge(genie_node, oneapi::tbb::flow::input_port<1>(middle_join));
    make_edge(middle_join, root_genie_node);
#endif

    auto end_construction_time = std::chrono::high_resolution_clock::now();
    std::chrono::high_resolution_clock::time_point start_execution_time = std::chrono::high_resolution_clock::now();
        
    for (int i = -1; i < num_executions; ++i) {
        if (i == 0) {
            root_resource.counter = 0;
            genie_resource.counter = 0;
            start_execution_time = std::chrono::high_resolution_clock::now();
        }

        for (int i = 0; i < num_inputs; ++i) {
            start.try_put(i);
        }
        g.wait_for_all();
    }
    auto end_execution_time = std::chrono::high_resolution_clock::now();

    if (root_resource.counter != num_inputs * 2 * num_executions) {
        std::cerr << "Error: root resource was used " << root_resource.counter
                  << " times, expected " << num_inputs * 2 * num_executions << " times." << std::endl;
    }
    if (genie_resource.counter != num_inputs * 2 * num_executions) {
        std::cerr << "Error: genie resource was used " << genie_resource.counter
                  << " times, expected " << num_inputs * 2 * num_executions << " times." << std::endl;
    }
    return {end_construction_time - start_construction_time, end_execution_time - start_execution_time};
}

// Baseline Cycle: self-propagating chain with N nodes that executes M times
// Measures overhead of resource acquisition with no contention
std::tuple<std::chrono::duration<double>, std::chrono::duration<double>>
run_baseline_cycle_bench(int num_executions = 10, int num_nodes = 10) {
    auto start_construction_time = std::chrono::high_resolution_clock::now();

#if USE_TRACE > 0
    std::unique_ptr<TraceCollector> trace_collector = make_trace_collector("baseline_cycle_bench", num_executions, num_nodes);
#endif

    counting_resource shared_resource;

    using namespace oneapi::tbb::flow;
    graph g;

#if USE_MODE == 0
    buffer_node<counting_resource*> resource_limiter(g);
    resource_limiter.try_put(&shared_resource);

    using node_type = resource_composite_node<std::tuple<int, counting_resource*>>;
    using mfn_ports = typename multifunction_node<std::tuple<int, counting_resource*>, std::tuple<int, counting_resource*>>::output_ports_type;
#else
    limiter_type<counting_resource*> resource_limiter(&shared_resource);
    using node_type = resource_limited_node<int, std::tuple<int>>;
    using ports_type = typename node_type::output_ports_type;
#endif

    const int sleep_time_ms = 1;
    std::vector<node_type*> nodes;

    // Create N nodes
    for (int i = 0; i < num_nodes; ++i) {
#if USE_MODE == 0
        nodes.push_back(new node_type(g, 1,
            [&, i](const std::tuple<int, counting_resource*>& input_tuple, mfn_ports& ports) {
                auto [input, resource] = input_tuple;

                // N_0 is conditional - check stop condition BEFORE using resource
                if (i == 0 && input >= num_executions) {
                    std::get<1>(ports).try_put(resource); // return resource only, don't execute
                    return;
                }

#if USE_TRACE > 0
                std::unique_ptr<ScopedTraceEvent> trace = make_event(trace_collector, input, i + 1);
#endif
                resource->use();
                std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time_ms));
                std::get<0>(ports).try_put(input + (i == num_nodes - 1 ? 1 : 0)); // increment on last node
                std::get<1>(ports).try_put(resource);
            }));
#else
        nodes.push_back(new node_type(g, 1, std::tie(resource_limiter),
            [&, i](int input, ports_type& ports, counting_resource* resource) {
                // N_0 is conditional - check stop condition BEFORE using resource
                if (i == 0 && input >= num_executions) {
                    // Don't send message forward, cycle stops, don't execute
                    return;
                }

#if USE_TRACE > 0
                std::unique_ptr<ScopedTraceEvent> trace = make_event(trace_collector, input, i + 1);
#endif
                resource->use();
                std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time_ms));
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

    // Start the cycle with initial message
    shared_resource.counter = 0;
    auto start_execution_time = std::chrono::high_resolution_clock::now();

#if USE_MODE == 0
    std::get<0>(nodes[0]->input_ports()).try_put(0);
#else
    nodes[0]->try_put(0);
#endif

    g.wait_for_all();
    auto end_execution_time = std::chrono::high_resolution_clock::now();

    int expected_uses = num_executions * num_nodes;
    if (shared_resource.counter != expected_uses) {
        std::cerr << "Error: resource was used " << shared_resource.counter
                  << " times, expected " << expected_uses << " times." << std::endl;
    }

    // Cleanup
    for (auto* node : nodes) {
        delete node;
    }

    return {end_construction_time - start_construction_time, end_execution_time - start_execution_time};
}

// Performance of a Chain: input_node drives N sequential nodes
std::tuple<std::chrono::duration<double>, std::chrono::duration<double>>
run_chain_bench(int num_executions = 10, int num_inputs = 100, int num_nodes = 10) {
    auto start_construction_time = std::chrono::high_resolution_clock::now();

#if USE_TRACE > 0
    std::unique_ptr<TraceCollector> trace_collector = make_trace_collector("chain_bench", num_executions, num_inputs);
#endif

    counting_resource shared_resource;

    using namespace oneapi::tbb::flow;
    graph g;

#if USE_MODE == 0
    buffer_node<counting_resource*> resource_limiter(g);
    resource_limiter.try_put(&shared_resource);

    using node_type = resource_composite_node<std::tuple<int, counting_resource*>>;
    using mfn_ports = typename multifunction_node<std::tuple<int, counting_resource*>, std::tuple<int, counting_resource*>>::output_ports_type;
#else
    limiter_type<counting_resource*> resource_limiter(&shared_resource);
    using node_type = resource_limited_node<int, std::tuple<int>>;
    using ports_type = typename node_type::output_ports_type;
#endif

    const int sleep_time_ms = 1;

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
                std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time_ms));
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
                std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time_ms));
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
    std::chrono::high_resolution_clock::time_point start_execution_time = std::chrono::high_resolution_clock::now();

    for (int i = -1; i < num_executions; ++i) {
        if (i == 0) {
            shared_resource.counter = 0;
            start_execution_time = std::chrono::high_resolution_clock::now();
        }

        for (int j = 0; j < num_inputs; ++j) {
            source.try_put(j);
        }
        g.wait_for_all();
    }
    auto end_execution_time = std::chrono::high_resolution_clock::now();

    int expected_uses = num_inputs * num_nodes * num_executions;
    if (shared_resource.counter != expected_uses) {
        std::cerr << "Error: resource was used " << shared_resource.counter
                  << " times, expected " << expected_uses << " times." << std::endl;
    }

    // Cleanup
    for (auto* node : nodes) {
        delete node;
    }

    return {end_construction_time - start_construction_time, end_execution_time - start_execution_time};
}

// Performance for Siblings: input_node drives N sibling nodes
std::tuple<std::chrono::duration<double>, std::chrono::duration<double>>
run_siblings_bench(int num_executions = 10, int num_inputs = 100, int num_nodes = 10) {
    auto start_construction_time = std::chrono::high_resolution_clock::now();

#if USE_TRACE > 0
    std::unique_ptr<TraceCollector> trace_collector = make_trace_collector("siblings_bench", num_executions, num_inputs);
#endif

    counting_resource shared_resource;

    using namespace oneapi::tbb::flow;
    graph g;

#if USE_MODE == 0
    buffer_node<counting_resource*> resource_limiter(g);
    resource_limiter.try_put(&shared_resource);

    using node_type = resource_composite_node<std::tuple<int, counting_resource*>>;
    using mfn_ports = typename multifunction_node<std::tuple<int, counting_resource*>, std::tuple<int, counting_resource*>>::output_ports_type;
#else
    limiter_type<counting_resource*> resource_limiter(&shared_resource);
    using node_type = resource_limited_node<int, std::tuple<int>>;
    using ports_type = typename node_type::output_ports_type;
#endif

    const int sleep_time_ms = 1;

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
                std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time_ms));
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
                std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time_ms));
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
    std::chrono::high_resolution_clock::time_point start_execution_time = std::chrono::high_resolution_clock::now();

    for (int i = -1; i < num_executions; ++i) {
        if (i == 0) {
            shared_resource.counter = 0;
            start_execution_time = std::chrono::high_resolution_clock::now();
        }

        for (int j = 0; j < num_inputs; ++j) {
            source.try_put(j);
        }
        g.wait_for_all();
    }
    auto end_execution_time = std::chrono::high_resolution_clock::now();

    int expected_uses = num_inputs * num_nodes * num_executions;
    if (shared_resource.counter != expected_uses) {
        std::cerr << "Error: resource was used " << shared_resource.counter
                  << " times, expected " << expected_uses << " times." << std::endl;
    }

    // Cleanup
    for (auto* node : nodes) {
        delete node;
    }

    return {end_construction_time - start_construction_time, end_execution_time - start_execution_time};
}

// Performance of a Tree: binary tree structure
std::tuple<std::chrono::duration<double>, std::chrono::duration<double>>
run_tree_bench(int num_executions = 10, int num_inputs = 100, int num_nodes = 10) {
    auto start_construction_time = std::chrono::high_resolution_clock::now();

#if USE_TRACE > 0
    std::unique_ptr<TraceCollector> trace_collector = make_trace_collector("tree_bench", num_executions, num_inputs);
#endif

    counting_resource shared_resource;

    using namespace oneapi::tbb::flow;
    graph g;

#if USE_MODE == 0
    buffer_node<counting_resource*> resource_limiter(g);
    resource_limiter.try_put(&shared_resource);

    using node_type = resource_composite_node<std::tuple<int, counting_resource*>>;
    using mfn_ports = typename multifunction_node<std::tuple<int, counting_resource*>, std::tuple<int, counting_resource*>>::output_ports_type;
#else
    limiter_type<counting_resource*> resource_limiter(&shared_resource);
    using node_type = resource_limited_node<int, std::tuple<int, int>>;
    using ports_type = typename node_type::output_ports_type;
#endif

    const int sleep_time_ms = 1;

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
                std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time_ms));
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
                std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time_ms));
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
    std::chrono::high_resolution_clock::time_point start_execution_time = std::chrono::high_resolution_clock::now();

    for (int i = -1; i < num_executions; ++i) {
        if (i == 0) {
            shared_resource.counter = 0;
            start_execution_time = std::chrono::high_resolution_clock::now();
        }

        for (int j = 0; j < num_inputs; ++j) {
            source.try_put(j);
        }
        g.wait_for_all();
    }
    auto end_execution_time = std::chrono::high_resolution_clock::now();

    int expected_uses = num_inputs * total_nodes * num_executions;
    if (shared_resource.counter != expected_uses) {
        std::cerr << "Error: resource was used " << shared_resource.counter
                  << " times, expected " << expected_uses << " times." << std::endl;
    }

    // Cleanup
    for (auto* node : nodes) {
        delete node;
    }

    return {end_construction_time - start_construction_time, end_execution_time - start_execution_time};
}

void print_results(const std::string& bench_name, std::chrono::duration<double> construction_time, std::chrono::duration<double> execution_time, int num_executions, int num_inputs) {
    std::cout << bench_name << " results:\n";
    std::cout << "  Construction time: " << construction_time.count() << " seconds\n";
    std::cout << "  Execution time: " << execution_time.count() << " seconds\n";
    std::cout << "  Total time: " << (construction_time + execution_time).count() << " seconds\n";
    std::cout << "  Time per execution: " << (execution_time.count() / num_executions) << " seconds\n";
    std::cout << "  Time per input: " << (execution_time.count() / (num_executions * num_inputs)) << " seconds\n";
}

int main(int argc, char* argv[]) {
    // usage: resource_limited_ubenches [benchmark] [num_executions] [num_inputs/num_nodes] [num_nodes/tree_depth]
    // benchmark: genie, genie_diamond, baseline_cycle, chain, siblings, tree, all
    // USE_MODE: 0 = flow graph with join_node, 1 = flow graph with resource limiting, 2 = pressure-aware resource limiter
    // USE_TRACE: 0 = no tracing, 1 = eager, 2 = lazy

    std::string benchmark = "all";
    int num_executions = 1;
    int num_inputs = 100;
    int num_nodes = 10;

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
        num_nodes = std::atoi(argv[4]);
    }

    std::cout << "USE_MODE=" << USE_MODE << " (";
    if (USE_MODE == 0) std::cout << "join_node";
    else if (USE_MODE == 1) std::cout << "resource_limiter";
    else if (USE_MODE == 2) std::cout << "pressure_aware_resource_limiter";
    std::cout << ")\n";
    std::cout << "Benchmark: " << benchmark << "\n\n";

    std::cout << "macro settings:\n";
#if USE_MODE == 0
    std::cout << "  USE_MODE=0: flow graph with join_node and explicit resource passing\n";
#elif USE_MODE == 1
    std::cout << "  USE_MODE=1: flow graph with resource_limited_node and notify all resource limiter\n";
#elif USE_MODE == 2
    std::cout << "  USE_MODE=2: flow graph with pressure-aware resource limiter\n";
    std::cout << "  __TBB_USE_TIMESTAMP_IN_REQUEST_ID=" << __TBB_USE_TIMESTAMP_IN_REQUEST_ID << "\n";
    std::cout << "  __TBB_USE_CONSUMER_LOCAL_COUNTER_FOR_REQUEST_ID=" << __TBB_USE_CONSUMER_LOCAL_COUNTER_FOR_REQUEST_ID << "\n";
    std::cout << "  __TBB_USE_NOTIFY_ON_REPORT_PRESSURE=" << __TBB_USE_NOTIFY_ON_REPORT_PRESSURE << "\n";
#endif

    std::cout << "Configuration parameters:\n";
    std::cout << "  num_executions: " << num_executions << "\n";
    std::cout << "  num_inputs: " << num_inputs << "\n";
    std::cout << "  num_nodes/tree_depth: " << num_nodes << "\n\n";

    if (benchmark == "genie" || benchmark == "all") {
        auto [construction_time, execution_time] = run_genie_bench(num_executions, num_inputs);
        print_results("genie_bench", construction_time, execution_time, num_executions, num_inputs);
        std::cout << "\n";
    }

    if (benchmark == "genie_diamond" || benchmark == "all") {
        auto [diamond_construction_time, diamond_execution_time] = run_genie_diamond_bench(num_executions, num_inputs);
        print_results("genie_diamond_bench", diamond_construction_time, diamond_execution_time, num_executions, num_inputs);
        std::cout << "\n";
    }

    if (benchmark == "baseline_cycle" || benchmark == "all") {
        auto [cycle_construction_time, cycle_execution_time] = run_baseline_cycle_bench(num_executions, num_nodes);
        print_results("baseline_cycle_bench", cycle_construction_time, cycle_execution_time, num_executions, num_nodes);
        std::cout << "\n";
    }

    if (benchmark == "chain" || benchmark == "all") {
        auto [chain_construction_time, chain_execution_time] = run_chain_bench(num_executions, num_inputs, num_nodes);
        print_results("chain_bench", chain_construction_time, chain_execution_time, num_executions, num_inputs);
        std::cout << "\n";
    }

    if (benchmark == "siblings" || benchmark == "all") {
        auto [siblings_construction_time, siblings_execution_time] = run_siblings_bench(num_executions, num_inputs, num_nodes);
        print_results("siblings_bench", siblings_construction_time, siblings_execution_time, num_executions, num_inputs);
        std::cout << "\n";
    }

    if (benchmark == "tree" || benchmark == "all") {
        auto [tree_construction_time, tree_execution_time] = run_tree_bench(num_executions, num_inputs, num_nodes);
        print_results("tree_bench", tree_construction_time, tree_execution_time, num_executions, num_inputs);
        std::cout << "\n";
    }

    return 0;
}
