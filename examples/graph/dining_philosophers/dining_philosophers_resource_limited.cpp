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

#define TBB_PREVIEW_FLOW_GRAPH_RESOURCE_LIMITED_NODE 1

#include <iostream>
#include <thread>
#include <chrono>
#include <memory>
#include <tuple>
#include <cstdlib>
#include <cstdio>

#include "oneapi/tbb/flow_graph.h"
#include "oneapi/tbb/tick_count.h"
#include "oneapi/tbb/spin_mutex.h"
#include "oneapi/tbb/global_control.h"

#include "common/utility/utility.hpp"
#include "common/utility/get_default_num_threads.hpp"
#include "common/utility/trace_collector.h"

//
// Each philosopher uses TWO nodes:
// 1. think_node (function_node) - thinking happens here WITHOUT holding chopsticks
// 2. eat_node (resource_limited_node) - eating happens here WITH chopsticks held
//
// think_node → eat_node → (loop back to think_node)
//

const std::chrono::milliseconds think_time(100);
const std::chrono::milliseconds eat_time(100);
const int num_times = 10;

oneapi::tbb::tick_count t0;
bool verbose = false;
oneapi::tbb::spin_mutex my_mutex;

const char *names[] = { "Archimedes", "Bakunin",   "Confucius",    "Democritus",  "Euclid",
                        "Favorinus",  "Geminus",   "Heraclitus",   "Ichthyas",    "Jason of Nysa",
                        "Kant",       "Lavrov",    "Metrocles",    "Nausiphanes", "Onatas",
                        "Phaedrus",   "Quillot",   "Russell",      "Socrates",    "Thales",
                        "Udayana",    "Vernadsky", "Wittgenstein", "Xenophilus",  "Yen Yuan",
                        "Zenodotus" };
const int NumPhilosophers = sizeof(names) / sizeof(char *);

class chopstick {};

class philosopher {
public:
    philosopher(const char *name, int tid = 0, TraceCollector* collector = nullptr,
                int left_id = 0, int right_id = 0)
        : my_name(name), my_count(num_times), my_tid(tid),
          my_collector(collector), my_iteration(0),
          my_left_chopstick_id(left_id), my_right_chopstick_id(right_id) {}

    ~philosopher() {}

    void check() {
        if (my_count != 0) {
            std::printf("ERROR: philosopher %s still had to run %d more times\n", name(), my_count);
            std::exit(-1);
        }
    }

    const char *name() const {
        return my_name;
    }

    int get_iteration() const { return my_iteration; }
    int get_left_chopstick_id() const { return my_left_chopstick_id; }
    int get_right_chopstick_id() const { return my_right_chopstick_id; }

    bool should_continue() const { return my_count > 0; }
    void decrement_count() { --my_count; }
    void increment_iteration() { ++my_iteration; }

    void think() {
        std::unique_ptr<ScopedTraceEvent> trace;
        if (my_collector) {
            std::string event_name = std::string(my_name) + "_think_" +
                                    std::to_string(my_iteration);
            trace = std::make_unique<ScopedTraceEvent>(*my_collector, event_name, my_tid);
        }

        if (verbose) {
            oneapi::tbb::spin_mutex::scoped_lock lock(my_mutex);
            std::printf("%s thinking\n", name());
        }
        std::this_thread::sleep_for(think_time);
        if (verbose) {
            oneapi::tbb::spin_mutex::scoped_lock lock(my_mutex);
            std::printf("%s done thinking\n", name());
        }
    }

    void eat() {
        std::unique_ptr<ScopedTraceEvent> trace;
        if (my_collector) {
            std::string event_name = std::string(my_name) + "_eat_" +
                                    std::to_string(my_iteration) +
                                    "_L" + std::to_string(my_left_chopstick_id) +
                                    "_R" + std::to_string(my_right_chopstick_id);
            trace = std::make_unique<ScopedTraceEvent>(*my_collector, event_name, my_tid);
        }

        if (verbose) {
            oneapi::tbb::spin_mutex::scoped_lock lock(my_mutex);
            std::printf("%s eating\n", name());
        }
        std::this_thread::sleep_for(eat_time);
        if (verbose) {
            oneapi::tbb::spin_mutex::scoped_lock lock(my_mutex);
            std::printf("%s done eating\n", name());
        }
    }

private:
    friend std::ostream &operator<<(std::ostream &o, philosopher const &p);

    const char *my_name;
    int my_count;
    int my_tid;
    TraceCollector* my_collector;
    int my_iteration;
    int my_left_chopstick_id;
    int my_right_chopstick_id;
};

std::ostream &operator<<(std::ostream &o, philosopher const &p) {
    o << "< philosopher[" << reinterpret_cast<uintptr_t>(const_cast<philosopher *>(&p)) << "] "
      << p.name() << ", my_count=" << p.my_count;

    return o;
}

struct RunOptions {
    utility::thread_number_range threads;
    int number_of_philosophers;
    bool silent;
    bool enable_tracing;
    RunOptions(utility::thread_number_range threads_, int number_of_philosophers_, bool silent_, bool enable_tracing_)
            : threads(threads_),
              number_of_philosophers(number_of_philosophers_),
              silent(silent_),
              enable_tracing(enable_tracing_) {}
};

RunOptions ParseCommandLine(int argc, char *argv[]) {
    int auto_threads = utility::get_default_num_threads();
    utility::thread_number_range threads(
        utility::get_default_num_threads, auto_threads, auto_threads);
    int nPhilosophers = 5;
    bool verbose_local = false;
    bool enable_tracing = false;
    char charbuf[100];
    std::sprintf(charbuf, "%d", NumPhilosophers);
    std::string pCount = "how many philosophers, from 2-";
    pCount += charbuf;

    utility::cli_argument_pack cli_pack;
    cli_pack.positional_arg(threads, "n-of_threads", utility::thread_number_range_desc)
        .positional_arg(nPhilosophers, "n-of-philosophers", pCount)
        .arg(verbose_local, "verbose", "verbose output")
        .arg(enable_tracing, "trace", "enable execution tracing");
    utility::parse_cli_arguments(argc, argv, cli_pack);
    if (nPhilosophers < 2 || nPhilosophers > NumPhilosophers) {
        std::cout << "Number of philosophers (" << nPhilosophers
                  << ") out of range [2:" << NumPhilosophers << "]\n";
        std::cout << cli_pack.usage_string(argv[0]) << std::flush;
        std::exit(-1);
    }
    return RunOptions(threads, nPhilosophers, !verbose_local, enable_tracing);
}

int main(int argc, char *argv[]) {
    using oneapi::tbb::flow::make_edge;
    using oneapi::tbb::flow::output_port;
    using oneapi::tbb::flow::function_node;
    using oneapi::tbb::flow::resource_limited_node;
    using oneapi::tbb::flow::resource_provider;
    using oneapi::tbb::flow::continue_msg;
    using oneapi::tbb::flow::unlimited;

    oneapi::tbb::tick_count main_time = oneapi::tbb::tick_count::now();
    int num_threads;
    int num_philosophers;

    RunOptions options = ParseCommandLine(argc, argv);
    num_philosophers = options.number_of_philosophers;
    verbose = !options.silent;
    bool enable_tracing = options.enable_tracing;

    TraceCollector* trace_collector = enable_tracing ? new TraceCollector() : nullptr;

    for (num_threads = options.threads.first; num_threads <= options.threads.last;
         num_threads = options.threads.step(num_threads)) {
        oneapi::tbb::global_control c(oneapi::tbb::global_control::max_allowed_parallelism,
                                      num_threads);

        oneapi::tbb::flow::graph g;

        if (verbose) {
            std::cout << "\n"
                      << num_philosophers << " philosophers with " << num_threads << " threads"
                      << "\n"
                      << "\n";
        }
        t0 = oneapi::tbb::tick_count::now();

        // Create chopsticks and resource providers
        std::vector<chopstick> chopsticks(num_philosophers);
        std::vector<std::unique_ptr<resource_provider<chopstick*>>> providers;
        providers.reserve(num_philosophers);
        for (int i = 0; i < num_philosophers; ++i) {
            providers.push_back(std::make_unique<resource_provider<chopstick*>>(&chopsticks[i]));
        }

        // Create philosophers
        std::vector<philosopher> philosophers;
        philosophers.reserve(num_philosophers);

        const int philosopher_base_tid = 1;
        std::vector<std::pair<int, std::string>> thread_names;

        // Node types for the hybrid approach
        typedef function_node<continue_msg, continue_msg> think_node_type;
        typedef resource_limited_node<continue_msg, std::tuple<continue_msg>> eat_node_type;
        typedef typename eat_node_type::output_ports_type ports_type;

        std::vector<think_node_type*> think_nodes;
        std::vector<eat_node_type*> eat_nodes;
        think_nodes.reserve(num_philosophers);
        eat_nodes.reserve(num_philosophers);

        for (int i = 0; i < num_philosophers; ++i) {
            int tid = philosopher_base_tid + i;
            int left_id = i;
            int right_id = (i + 1) % num_philosophers;

            philosophers.push_back(
                philosopher(names[i], tid, trace_collector, left_id, right_id));

            if (trace_collector) {
                thread_names.emplace_back(tid, names[i]);
            }

            if (verbose) {
                oneapi::tbb::spin_mutex::scoped_lock lock(my_mutex);
                std::cout << "Built philosopher " << philosophers[i] << "\n";
            }

            // Create think node (NO resources held)
            think_nodes.push_back(new think_node_type(
                g, unlimited,
                [&phil = philosophers[i]](continue_msg) {
                    phil.think();  // Think without holding chopsticks
                    return continue_msg{};
                }));

            // Create eat node (WITH resources held)
            eat_nodes.push_back(new eat_node_type(
                g, unlimited,
                std::tie(*providers[left_id], *providers[right_id]),
                [&phil = philosophers[i]](continue_msg, ports_type& ports,
                                         chopstick*, chopstick*) {
                    // Chopsticks are now acquired
                    phil.eat();  // Eat with chopsticks held

                    if (phil.should_continue()) {
                        phil.decrement_count();
                        phil.increment_iteration();
                        std::get<0>(ports).try_put(continue_msg{});  // Loop back to think
                    }
                    else {
                        if (verbose) {
                            oneapi::tbb::spin_mutex::scoped_lock lock(my_mutex);
                            std::printf("%s has left the building\n", phil.name());
                        }
                    }
                }));
        }

        // Wire the hybrid nodes together
        for (int i = 0; i < num_philosophers; ++i) {
            // think → eat
            make_edge(*think_nodes[i], *eat_nodes[i]);

            // eat → think (loop back)
            make_edge(output_port<0>(*eat_nodes[i]), *think_nodes[i]);
        }

        // Bootstrap: start all philosophers thinking
        for (int i = 0; i < num_philosophers; ++i) {
            think_nodes[i]->try_put(continue_msg{});
        }

        g.wait_for_all();

        oneapi::tbb::tick_count t1 = oneapi::tbb::tick_count::now();
        if (verbose)
            std::cout << "\n"
                      << num_philosophers << " philosophers with " << num_threads
                      << " threads have taken " << (t1 - t0).seconds() << "seconds"
                      << "\n";

        if (trace_collector) {
            std::string trace_filename = "dining_philosophers_resource_limited_" +
                                        std::to_string(num_philosophers) + "_phil_" +
                                        std::to_string(num_threads) + "_threads.json";
            trace_collector->write_trace(trace_filename, 1, "dining_philosophers_resource_limited",
                                        thread_names);
        }

        for (int i = 0; i < num_philosophers; ++i)
            philosophers[i].check();

        for (int i = 0; i < num_philosophers; ++i) {
            delete think_nodes[i];
            delete eat_nodes[i];
        }

        // Resource providers automatically cleaned up by unique_ptr
    }

    delete trace_collector;

    utility::report_elapsed_time((oneapi::tbb::tick_count::now() - main_time).seconds());
    return 0;
}
