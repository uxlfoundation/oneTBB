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

#define TBB_PREVIEW_FLOW_GRAPH_FEATURES 1
#include <tbb/flow_graph.h>
#include <tbb/tick_count.h>
#include <fstream>
#include "resource_common.h"
#include <iostream>

#ifndef USE_TRACE
#define USE_TRACE 1
#endif

// The capture list of a node body: everything by value, plus the trace collector
// `tc` by reference when tracing is compiled in. Without tracing there is no `tc`
// to capture.
#if USE_TRACE > 0
#include "trace_collector.h"
#define TRACE_CAPTURE =, &active_messages, &tc
#else
#define TRACE_CAPTURE =, &active_messages
#endif

int g_num_inputs = 100;

void run_pipeline_using_joins() {
    std::atomic<int> active_messages{0};
#if USE_TRACE > 0
    TraceCollector tc("pipeline_joins.json", /*pid=*/200, /*category=*/"pipeline");
    tc.add_thread_name(1, "consumer_of_a");
    tc.add_thread_name(2, "consumer_of_b");
    tc.add_thread_name(3, "consumer_of_a_and_b");
    tc.add_thread_name(4, "pacer");
    tc.add_thread_name(5, "input");
    std::ofstream csv_file("pipeline_joins.csv");
    csv_file << "join iteration, active messages" << std::endl;
#endif

    // The time for the single consumers is long_duration
    double long_duration = 10000;

    // The time for the consumer that requires both resources is short_duration
    double short_duration = 1000;

    tbb::flow::graph g;

    counting_resource resource_a;
    counting_resource resource_b;

    // use resource_limiter
    tbb::flow::buffer_node<counting_resource *> buffer_a{g};
    buffer_a.try_put(&resource_a);
    tbb::flow::buffer_node<counting_resource *> buffer_b{g};
    buffer_b.try_put(&resource_b);

    using tuple_1_type = std::tuple<int, counting_resource*>;
    using tuple_2_type = std::tuple<int, counting_resource*, counting_resource*>;

    using single_consumer_type = resource_composite_node<tuple_1_type>;
    using double_consumer_type = resource_composite_node<tuple_2_type>;

    // node that puts pressure on the upstream input node to match expected throughput of the pipeline
    tbb::flow::function_node<int, int, tbb::flow::rejecting> pacer_node(g, 1, 
        [TRACE_CAPTURE](int input) {
#if USE_TRACE > 0
            ScopedTraceEvent ev(tc, "pacer", 4);
#endif
            busy_wait_us(long_duration + short_duration);
            return input;
        }
    );

    // Each traced scope ends before the body hands its resources back, matching
    // resource_limited_node, which releases them only after the body returns.
    // Otherwise the next holder could start before this event closes.
    single_consumer_type consumer_of_a(g, 1,
        [TRACE_CAPTURE](const tuple_1_type& input_tuple,
            typename single_consumer_type::body_ports_type& ports) {
        int input = std::get<0>(input_tuple);
        counting_resource* resource = std::get<1>(input_tuple);
        {
        #if USE_TRACE > 0
            ScopedTraceEvent ev(tc, "consumer_of_a", 1);
        #endif
            resource->use();
            busy_wait_us(long_duration);
            std::get<0>(ports).try_put(input); // return the input to the first port
        }
        std::get<1>(ports).try_put(resource); // return the resource
    });
    tbb::flow::make_edge(pacer_node, consumer_of_a);
    tbb::flow::make_edge(buffer_a, tbb::flow::input_port<1>(consumer_of_a));
    tbb::flow::make_edge(tbb::flow::output_port<1>(consumer_of_a), buffer_a);

    single_consumer_type consumer_of_b(g, 1,
        [TRACE_CAPTURE](const tuple_1_type& input_tuple,
            typename single_consumer_type::body_ports_type& ports) {
        int input = std::get<0>(input_tuple);
        counting_resource* resource = std::get<1>(input_tuple);
        {
        #if USE_TRACE > 0
            ScopedTraceEvent ev(tc, "consumer_of_b", 2);
        #endif
            resource->use();
            busy_wait_us(long_duration);
            std::get<0>(ports).try_put(input); // return the input to the first port
        }
        std::get<1>(ports).try_put(resource); // return the resource
    });
    tbb::flow::make_edge(buffer_b, tbb::flow::input_port<1>(consumer_of_b));
    tbb::flow::make_edge(tbb::flow::output_port<1>(consumer_of_b), buffer_b);

    double_consumer_type consumer_of_a_and_b(g, 1,
        [TRACE_CAPTURE](const tuple_2_type& input_tuple,
            typename double_consumer_type::body_ports_type& ports) {
        int input = std::get<0>(input_tuple);
        counting_resource* resource_a = std::get<1>(input_tuple);
        counting_resource* resource_b = std::get<2>(input_tuple);
        {
        #if USE_TRACE > 0
            ScopedTraceEvent ev(tc, "consumer_of_a_and_b", 3);
        #endif
            resource_a->use();
            resource_b->use();
            busy_wait_us(short_duration);
            std::get<0>(ports).try_put(input); // return the input to the first port
            active_messages.fetch_sub(1);
        }
        std::get<1>(ports).try_put(resource_a); // return the resource
        std::get<2>(ports).try_put(resource_b); // return the resource
    });
    tbb::flow::make_edge(buffer_a, tbb::flow::input_port<1>(consumer_of_a_and_b));
    tbb::flow::make_edge(buffer_b, tbb::flow::input_port<2>(consumer_of_a_and_b));
    tbb::flow::make_edge(tbb::flow::output_port<1>(consumer_of_a_and_b), buffer_a);
    tbb::flow::make_edge(tbb::flow::output_port<2>(consumer_of_a_and_b), buffer_b);

    tbb::flow::make_edge(tbb::flow::output_port<0>(consumer_of_a),
                          tbb::flow::input_port<0>(consumer_of_b));

    tbb::flow::make_edge(tbb::flow::output_port<0>(consumer_of_b),
                          tbb::flow::input_port<0>(consumer_of_a_and_b));

    int i = 0;
    tbb::flow::input_node<int> src( g, 
        [&]( oneapi::tbb::flow_control &fc ) -> int {
#if USE_TRACE > 0
            ScopedTraceEvent ev(tc, "input", 5);
            csv_file << i << ", " << active_messages.load() << std::endl;
#endif
            if ( i < g_num_inputs ) {
                active_messages.fetch_add(1);
                return i++;
            }
            fc.stop();
            return {};
        });
    tbb::flow::make_edge(src, pacer_node);

    tbb::tick_count start_time = tbb::tick_count::now();
    src.activate();
    g.wait_for_all();
    tbb::tick_count end_time = tbb::tick_count::now();
#if USE_TRACE > 0
    csv_file << "total time (us), " << (end_time - start_time).seconds() * 1e6 << std::endl;
#else
    std::cout << "Elapsed time: " << (end_time - start_time).seconds() << " seconds" << std::endl;
#endif
}

void run_pipeline_using_limited_resources() {
    std::atomic<int> active_messages{0};
#if USE_TRACE > 0
    TraceCollector tc("pipeline_limited.json", /*pid=*/100, /*category=*/"pipeline");
    tc.add_thread_name(1, "consumer_of_a");
    tc.add_thread_name(2, "consumer_of_b");
    tc.add_thread_name(3, "consumer_of_a_and_b");
    tc.add_thread_name(4, "pacer");
    tc.add_thread_name(5, "input");
    std::ofstream csv_file("pipeline_limited.csv");
    csv_file << "limited iteration, active messages" << std::endl;
#endif

    // The time for the single consumers is long_duration
    double long_duration = 10000;

    // The time for the consumer that requires both resources is short_duration
    double short_duration = 1000;

    tbb::flow::graph g;

    counting_resource resource_a;
    counting_resource resource_b;

    // use buffer node
    // Each limiter holds one handle -- a pointer to its resource, as in the
    // join version -- so a counting_resource is never copied.
    tbb::flow::resource_limiter<counting_resource*> rl_a{&resource_a};
    tbb::flow::resource_limiter<counting_resource*> rl_b{&resource_b};

    // node that puts pressure on the upstream input node to match expected throughput of the pipeline
    tbb::flow::function_node<int, int, tbb::flow::rejecting> pacer_node(g, 1, 
        [TRACE_CAPTURE](int input) {
#if USE_TRACE > 0
            ScopedTraceEvent ev(tc, "pacer", 4);
#endif
            busy_wait_us(long_duration+short_duration);
            return input;
        }
    );

    using node_type = tbb::flow::resource_limited_node<int, std::tuple<int>>;

    node_type consumer_of_a(g, 1, std::tie(rl_a),
        [TRACE_CAPTURE](const int& input, typename node_type::output_ports_type& ports,
            counting_resource*& a) {
    #if USE_TRACE > 0
        ScopedTraceEvent ev(tc, "consumer_of_a", 1);
    #endif
        a->use();
        busy_wait_us(long_duration);
        std::get<0>(ports).try_put(input);
    });
    tbb::flow::make_edge(pacer_node, consumer_of_a);

    node_type consumer_of_b(g, 1, std::tie(rl_b),
        [TRACE_CAPTURE](const int& input, typename node_type::output_ports_type& ports,
            counting_resource*& b) {
    #if USE_TRACE > 0
        ScopedTraceEvent ev(tc, "consumer_of_b", 2);
    #endif
        b->use();
        busy_wait_us(long_duration);
        std::get<0>(ports).try_put(input);
    });

    node_type consumer_of_a_and_b(g, 1, std::tie(rl_a, rl_b),
        [TRACE_CAPTURE](const int& input, typename node_type::output_ports_type& ports,
            counting_resource*& a, counting_resource*& b) {
    #if USE_TRACE > 0
        ScopedTraceEvent ev(tc, "consumer_of_a_and_b", 3);
    #endif
        a->use();
        b->use();
        busy_wait_us(short_duration);
        std::get<0>(ports).try_put(input); // return the input to the first port
        active_messages.fetch_sub(1);
    });

    tbb::flow::make_edge(tbb::flow::output_port<0>(consumer_of_a), consumer_of_b);
    tbb::flow::make_edge(tbb::flow::output_port<0>(consumer_of_b), consumer_of_a_and_b);

    int i = 0;
    tbb::flow::input_node<int> src( g, 
        [&]( oneapi::tbb::flow_control &fc ) -> int {
#if USE_TRACE > 0
            ScopedTraceEvent ev(tc, "input", 5);
            csv_file << i << ", " << active_messages.load() << std::endl;
#endif
            if ( i < g_num_inputs ) {
                active_messages.fetch_add(1);
                return i++;
            }
            fc.stop();
            return {};
        });

    tbb::flow::make_edge(src, pacer_node);

    tbb::tick_count start_time = tbb::tick_count::now();
    src.activate();
    g.wait_for_all();
    tbb::tick_count end_time = tbb::tick_count::now();
#if USE_TRACE > 0
    csv_file << "total time (us), " << (end_time - start_time).seconds() * 1e6 << std::endl;
#else
    std::cout << "Elapsed time: " << (end_time - start_time).seconds() << " seconds" << std::endl;
#endif
}

int main() {
    run_pipeline_using_joins();
    run_pipeline_using_limited_resources();
    return 0;
}

