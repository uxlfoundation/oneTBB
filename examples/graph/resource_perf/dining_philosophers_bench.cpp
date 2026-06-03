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
#include <memory>
#include <csignal>

#if __TBB_DEBUG_RESOURCE_ACQUISITION
#include <oneapi/tbb/detail/_debug_buffer.h>

// Signal handler for flushing debug buffer
void timeout_signal_handler(int signum) {
    std::fprintf(stderr, "\nReceived signal %d, flushing debug buffer...\n", signum);
    TBB_DEBUG_FLUSH("dining_philosophers_debug.txt");
    std::exit(128 + signum);
}
#endif

const char *philosopher_names[] = {
    "Archimedes", "Bakunin",   "Confucius",    "Democritus",  "Euclid",
    "Favorinus",  "Geminus",   "Heraclitus",   "Ichthyas",    "Jason of Nysa",
    "Kant",       "Lavrov",    "Metrocles",    "Nausiphanes", "Onatas",
    "Phaedrus",   "Quillot",   "Russell",      "Socrates",    "Thales",
    "Udayana",    "Vernadsky", "Wittgenstein", "Xenophilus",  "Yen Yuan",
    "Zenodotus"
};
const int MaxPhilosophers = sizeof(philosopher_names) / sizeof(char *);

class chopstick {};

class philosopher {
public:
    philosopher(const char *name, int count, int tid = 0,
                int left_id = 0, int right_id = 0)
        : my_name(name), my_count(count), my_tid(tid), my_iteration(0),
          my_left_chopstick_id(left_id), my_right_chopstick_id(right_id) {}

    void check() {
        if (my_count != 0) {
            std::printf("ERROR: philosopher %s still had to run %d more times\n", name(), my_count);
            std::exit(-1);
        }
    }

    const char *name() const {
        return my_name;
    }

    int get_tid() const { return my_tid; }
    int get_iteration() const { return my_iteration; }
    int get_left_chopstick_id() const { return my_left_chopstick_id; }
    int get_right_chopstick_id() const { return my_right_chopstick_id; }

#if USE_MODE == 0
    // Mode 0: Used with forward node
    void decrement_count() {
        --my_count;
    }

    int get_count() const {
        return my_count;
    }
#else
    // Modes 1/2: Used with resource_limited_node
    bool should_continue() {
        auto r = my_count > 1;
        if (my_count > 0) {
            --my_count;
            ++my_iteration;
        }
        return r;
    }
#endif

    void think(double work_time_ms) {
        std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(work_time_ms));
    }

    void eat(double work_time_ms) {
        std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(work_time_ms));
    }

    void reset(int count) {
        my_count = count;
        my_iteration = 0;
    }

private:
    const char *my_name;
    int my_count;
    int my_tid;
    int my_iteration;
    int my_left_chopstick_id;
    int my_right_chopstick_id;
};

#if USE_MODE == 0
// Mode 0: Join node implementation (explicit resource passing)

typedef std::tuple<oneapi::tbb::flow::continue_msg, chopstick, chopstick> join_output;
typedef oneapi::tbb::flow::join_node<join_output, oneapi::tbb::flow::reserving> join_node_type;
typedef oneapi::tbb::flow::function_node<oneapi::tbb::flow::continue_msg,
                                         oneapi::tbb::flow::continue_msg> think_node_type;
typedef oneapi::tbb::flow::function_node<join_output, oneapi::tbb::flow::continue_msg> eat_node_type;
typedef oneapi::tbb::flow::multifunction_node<oneapi::tbb::flow::continue_msg, join_output> forward_node_type;
typedef oneapi::tbb::flow::queue_node<oneapi::tbb::flow::continue_msg> thinking_done_type;

std::tuple<std::chrono::duration<double>, std::chrono::duration<double>>
run_dining_philosophers_bench(int num_executions, int num_philosophers, int num_times,
                              double work_time_ms) {
    auto start_construction_time = std::chrono::high_resolution_clock::now();

#if USE_TRACE > 0
    std::unique_ptr<TraceCollector> trace_collector =
        make_trace_collector("dining_philosophers_bench", num_executions, num_philosophers);
#endif

    using namespace oneapi::tbb::flow;

    graph g;

    // Create chopstick queues
    std::vector<queue_node<chopstick>> places(num_philosophers, queue_node<chopstick>(g));
    for (int i = 0; i < num_philosophers; ++i) {
        places[i].try_put(chopstick());
    }

    // Create philosophers
    std::vector<philosopher> philosophers;
    philosophers.reserve(num_philosophers);
    for (int i = 0; i < num_philosophers; ++i) {
        int tid = i + 1;
        philosophers.push_back(philosopher(philosopher_names[i], num_times, tid, i, (i + 1) % num_philosophers));
#if USE_TRACE > 0
        trace_collector->add_thread_name(tid, philosopher_names[i]);
#endif
    }

    // Create nodes
    std::vector<think_node_type *> think_nodes;
    std::vector<thinking_done_type> done_vector(num_philosophers, thinking_done_type(g));
    std::vector<join_node_type> join_vector(num_philosophers, join_node_type(g));
    std::vector<eat_node_type *> eat_nodes;
    std::vector<forward_node_type *> forward_nodes;
    think_nodes.reserve(num_philosophers);
    eat_nodes.reserve(num_philosophers);
    forward_nodes.reserve(num_philosophers);

    for (int i = 0; i < num_philosophers; ++i) {
        think_nodes.push_back(new think_node_type(
            g, unlimited,
            [&phil = philosophers[i], work_time_ms
#if USE_TRACE > 0
            , &trace_collector
#endif
            ](continue_msg) {
#if USE_TRACE > 0
                std::string event_name = std::string(phil.name()) + "_think_" +
                                        std::to_string(phil.get_iteration());
                std::unique_ptr<ScopedTraceEvent> trace =
                    std::make_unique<ScopedTraceEvent>(*trace_collector, event_name, phil.get_tid());
#endif
                phil.think(work_time_ms);
                return continue_msg();
            }));

        eat_nodes.push_back(new eat_node_type(
            g, unlimited,
            [&phil = philosophers[i], work_time_ms
#if USE_TRACE > 0
            , &trace_collector
#endif
            ](const join_output&) {
                {
    #if USE_TRACE > 0
                    std::string event_name = std::string(phil.name()) + "_eat_" +
                                            std::to_string(phil.get_iteration()) +
                                            "_L" + std::to_string(phil.get_left_chopstick_id()) +
                                            "_R" + std::to_string(phil.get_right_chopstick_id());
                    std::unique_ptr<ScopedTraceEvent> trace =
                        std::make_unique<ScopedTraceEvent>(*trace_collector, event_name, phil.get_tid());
    #endif
                    phil.eat(work_time_ms);
                }
                return continue_msg();
            }));

        forward_nodes.push_back(new forward_node_type(
            g, unlimited,
            [&phil = philosophers[i]
#if USE_TRACE > 0
             , &trace_collector
#endif
            ](const continue_msg&, forward_node_type::output_ports_type& out_ports) {
                // Return chopsticks
                std::get<1>(out_ports).try_put(chopstick());
                std::get<2>(out_ports).try_put(chopstick());

                // Continue if not done
                if (phil.get_count() > 0) {
                    phil.decrement_count();
#if USE_TRACE > 0
                    record_input_start(trace_collector, phil.get_tid());
#endif
                    std::get<0>(out_ports).try_put(continue_msg());
                }
            }));
    }

    // Connect graph
    for (int i = 0; i < num_philosophers; ++i) {
        make_edge(*think_nodes[i], done_vector[i]);
        make_edge(done_vector[i], input_port<0>(join_vector[i]));
        make_edge(places[i], input_port<1>(join_vector[i])); // left chopstick
        make_edge(places[(i + 1) % num_philosophers], input_port<2>(join_vector[i])); // right chopstick
        make_edge(join_vector[i], *eat_nodes[i]);
        make_edge(*eat_nodes[i], *forward_nodes[i]);
        make_edge(output_port<0>(*forward_nodes[i]), *think_nodes[i]);
        make_edge(output_port<1>(*forward_nodes[i]), places[i]);
        make_edge(output_port<2>(*forward_nodes[i]), places[(i + 1) % num_philosophers]);
    }

    auto end_construction_time = std::chrono::high_resolution_clock::now();

    // Execute with warm-up + num_executions runs
    std::chrono::high_resolution_clock::time_point start_execution_time;

    for (int i = -1; i < num_executions; ++i) {
        if (i == 0) {
            start_execution_time = std::chrono::high_resolution_clock::now();
        }

        // Start all philosophers thinking
        for (int j = 0; j < num_philosophers; ++j) {
#if USE_TRACE > 0
            record_input_start(trace_collector, j);
#endif
            think_nodes[j]->try_put(continue_msg());
        }

        g.wait_for_all();

        // Reset philosopher counts for next execution
        if (i < num_executions - 1) {
            for (int j = 0; j < num_philosophers; ++j) {
                philosophers[j].reset(num_times);
            }
        }
    }

    auto end_execution_time = std::chrono::high_resolution_clock::now();

    // Validate all philosophers completed
    for (int i = 0; i < num_philosophers; ++i)
        philosophers[i].check();

    // Cleanup
    for (int i = 0; i < num_philosophers; ++i) {
        delete think_nodes[i];
        delete eat_nodes[i];
        delete forward_nodes[i];
    }

    return {end_construction_time - start_construction_time, end_execution_time - start_execution_time};
}

#else
// Modes 1/2: Resource limiter implementation

#if USE_MODE == 1
template<typename T>
using limiter_type = oneapi::tbb::flow::resource_limiter<T>;
#else  // USE_MODE == 2
template<typename T>
using limiter_type = oneapi::tbb::flow::priority_aware_resource_limiter<T>;
#endif

std::tuple<std::chrono::duration<double>, std::chrono::duration<double>>
run_dining_philosophers_bench(int num_executions, int num_philosophers, int num_times,
                              double work_time_ms) {
    auto start_construction_time = std::chrono::high_resolution_clock::now();

#if USE_TRACE > 0
    std::unique_ptr<TraceCollector> trace_collector =
        make_trace_collector("dining_philosophers_bench", num_executions, num_philosophers);
#endif

    using namespace oneapi::tbb::flow;

    graph g;

    // Create chopsticks and resource providers
    std::vector<chopstick> chopsticks(num_philosophers);
    std::vector<std::unique_ptr<limiter_type<chopstick*>>> providers;
    providers.reserve(num_philosophers);
    for (int i = 0; i < num_philosophers; ++i) {
        providers.push_back(std::make_unique<limiter_type<chopstick*>>(&chopsticks[i]));
    }

    // Create philosophers
    std::vector<philosopher> philosophers;
    philosophers.reserve(num_philosophers);
    for (int i = 0; i < num_philosophers; ++i) {
        int tid = i + 1;
        int left_id = i;
        int right_id = (i + 1) % num_philosophers;
        philosophers.push_back(philosopher(philosopher_names[i], num_times, tid, left_id, right_id));
#if USE_TRACE > 0
        trace_collector->add_thread_name(tid, philosopher_names[i]);
#endif
    }

    // Node types
    typedef function_node<continue_msg, continue_msg> think_node_type;
    typedef resource_limited_node<continue_msg, std::tuple<continue_msg>> eat_node_type;
    typedef typename eat_node_type::output_ports_type ports_type;

    std::vector<think_node_type*> think_nodes;
    std::vector<eat_node_type*> eat_nodes;
    think_nodes.reserve(num_philosophers);
    eat_nodes.reserve(num_philosophers);

    for (int i = 0; i < num_philosophers; ++i) {
        int left_id = i;
        int right_id = (i + 1) % num_philosophers;

        // Create think node (NO resources held)
        think_nodes.push_back(new think_node_type(
            g, unlimited,
            [&phil = philosophers[i], work_time_ms
#if USE_TRACE > 0
            , &trace_collector
#endif
            ](continue_msg) {
#if USE_TRACE > 0
                std::string event_name = std::string(phil.name()) + "_think_" +
                                        std::to_string(phil.get_iteration());
                std::unique_ptr<ScopedTraceEvent> trace =
                    std::make_unique<ScopedTraceEvent>(*trace_collector, event_name, phil.get_tid());
#endif
                phil.think(work_time_ms);
                return continue_msg{};
            }));

        // Create eat node (WITH resources held)
        eat_nodes.push_back(new eat_node_type(
            g, unlimited,
            std::tie(*providers[left_id], *providers[right_id]),
            [&phil = philosophers[i], work_time_ms
#if USE_TRACE > 0
            , &trace_collector
#endif
            ](continue_msg, ports_type& ports,
                                     chopstick*, chopstick*) {
                {
    #if USE_TRACE > 0
                    std::string event_name = std::string(phil.name()) + "_eat_" +
                                            std::to_string(phil.get_iteration()) +
                                            "_L" + std::to_string(phil.get_left_chopstick_id()) +
                                            "_R" + std::to_string(phil.get_right_chopstick_id());
                    std::unique_ptr<ScopedTraceEvent> trace =
                        std::make_unique<ScopedTraceEvent>(*trace_collector, event_name, phil.get_tid());
    #endif
                    // Chopsticks are now acquired
                    phil.eat(work_time_ms);
                }
                if (phil.should_continue()) {
#if USE_TRACE > 0
                    record_input_start(trace_collector, phil.get_tid());
#endif
                    std::get<0>(ports).try_put(continue_msg{});  // Loop back to think
                }
            }));
    }

    // Wire nodes together
    for (int i = 0; i < num_philosophers; ++i) {
        make_edge(*think_nodes[i], *eat_nodes[i]);
        make_edge(output_port<0>(*eat_nodes[i]), *think_nodes[i]);
    }

    auto end_construction_time = std::chrono::high_resolution_clock::now();

    // Execute with warm-up + num_executions runs
    std::chrono::high_resolution_clock::time_point start_execution_time;

    for (int i = -1; i < num_executions; ++i) {
        if (i == 0) {
            start_execution_time = std::chrono::high_resolution_clock::now();
        }

        // Bootstrap: start all philosophers thinking
        for (int j = 0; j < num_philosophers; ++j) {
#if USE_TRACE > 0
            record_input_start(trace_collector, j);
#endif
            think_nodes[j]->try_put(continue_msg{});
        }

        g.wait_for_all();

        // Reset philosopher counts for next execution
        if (i < num_executions - 1) {
            for (int j = 0; j < num_philosophers; ++j) {
                philosophers[j].reset(num_times);
            }
        }
    }

    auto end_execution_time = std::chrono::high_resolution_clock::now();

    // Validate all philosophers completed
    for (int i = 0; i < num_philosophers; ++i)
        philosophers[i].check();

    // Cleanup
    for (int i = 0; i < num_philosophers; ++i) {
        delete think_nodes[i];
        delete eat_nodes[i];
    }

    return {end_construction_time - start_construction_time, end_execution_time - start_execution_time};
}

#endif  // USE_MODE

int main(int argc, char* argv[]) {
    // Parse command-line arguments
    int num_executions = (argc >= 2) ? std::atoi(argv[1]) : 1;
    int num_philosophers = (argc >= 3) ? std::atoi(argv[2]) : 5;
    int num_times = (argc >= 4) ? std::atoi(argv[3]) : 10;
    double work_time_ms = (argc >= 5) ? std::atof(argv[4]) : 10.0;

    // Validate inputs
    if (num_philosophers < 2 || num_philosophers > MaxPhilosophers) {
        std::cerr << "Error: num_philosophers must be between 2 and " << MaxPhilosophers << "\n";
        return 1;
    }

#if __TBB_DEBUG_RESOURCE_ACQUISITION
    // Register signal handler and atexit for debug buffer flushing
    std::signal(SIGINT, timeout_signal_handler);
    std::signal(SIGTERM, timeout_signal_handler);
    std::atexit([]() {
        std::fprintf(stderr, "\natexit handler: flushing debug buffer...\n");
        TBB_DEBUG_FLUSH("dining_philosophers_debug.txt");
    });
#endif

    // Print configuration
#if USE_MODE == 0
    std::cout << "USE_MODE=0 (join_node)\n";
#elif USE_MODE == 1
    std::cout << "USE_MODE=1 (resource_limiter)\n";
#else
    std::cout << "USE_MODE=2 (priority_aware_resource_limiter)\n";
#endif

    std::cout << "Benchmark: dining_philosophers\n\n";

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
    std::cout << "  num_philosophers: " << num_philosophers << "\n";
    std::cout << "  num_times (cycles per philosopher): " << num_times << "\n";
    std::cout << "  work_time_ms (think/eat time): " << work_time_ms << "\n";

    // Run benchmark
    auto [construction_time, execution_time] =
        run_dining_philosophers_bench(num_executions, num_philosophers, num_times, work_time_ms);

    // Print results
    // Calculate total work time for context (think + eat) * num_times * num_philosophers
    double total_work_time_ms = 2.0 * work_time_ms * num_times * num_philosophers;
    double delay_ms = 0.0;  // No explicit delay in dining philosophers
    print_results("dining_philosophers_bench", construction_time, execution_time,
                  num_executions, num_philosophers, 0.0, delay_ms, total_work_time_ms);

#if __TBB_DEBUG_RESOURCE_ACQUISITION
    // Flush debug buffer before exit
    TBB_DEBUG_FLUSH("dining_philosophers_debug.txt");
#endif

    return 0;
}
