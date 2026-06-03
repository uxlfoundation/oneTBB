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

#if _MSC_VER
// Suppress "decorated name length exceeded, name was truncated" warning
#pragma warning(disable : 4503)
#endif

#include "benchmark_common.h"

#include <thread>
#include <chrono>

// Genie Diamond benchmark: ROOT and GENIE nodes with diamond topology (dependence)
// This implements the "Root-Genie Dependence" workload from workloads.md
std::tuple<std::chrono::duration<double>, std::chrono::duration<double>>
run_genie_diamond_bench(int num_executions, int num_inputs, double generation_rate) {
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
#if USE_TRACE > 0
            record_input_start(trace_collector, j);
#endif
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
        std::cerr << "Error: genie_diamond_bench db_resources were used "
                  << actual_db_uses << " times total, expected "
                  << expected_uses_db_total << " times." << std::endl;
    }

    return {end_construction_time - start_construction_time, end_execution_time - start_execution_time};
}

int main(int argc, char* argv[]) {
    // Parse command-line arguments
    int num_executions = (argc >= 2) ? std::atoi(argv[1]) : 1;
    int num_inputs = (argc >= 3) ? std::atoi(argv[2]) : 100;
    double generation_rate = (argc >= 4) ? std::atof(argv[3]) : 1.0;

    // Print configuration
#if USE_MODE == 0
    std::cout << "USE_MODE=0 (join_node)\n";
#elif USE_MODE == 1
    std::cout << "USE_MODE=1 (resource_limiter)\n";
#else
    std::cout << "USE_MODE=2 (priority_aware_resource_limiter)\n";
#endif

    std::cout << "Benchmark: genie_diamond (Root-Genie Dependence)\n\n";

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
    std::cout << "  generation_rate: " << generation_rate << "\n";

    // Run benchmark
    auto [construction_time, execution_time] =
        run_genie_diamond_bench(num_executions, num_inputs, generation_rate);

    // Print results
    // Genie diamond: max(genie, root) + genie_root = 10ms + 10ms = 20ms
    double total_graph_time_ms = 2.0 * genie_sleep_time_ms;
    double delay_ms = total_graph_time_ms / generation_rate;
    double total_work_time_ms = total_graph_time_ms * num_inputs;

    print_results("genie_diamond_bench", construction_time, execution_time,
                  num_executions, num_inputs, generation_rate, delay_ms, total_work_time_ms);

    return 0;
}
