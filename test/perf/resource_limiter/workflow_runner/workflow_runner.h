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

#ifndef TBB_test_perf_workflow_runner_H
#define TBB_test_perf_workflow_runner_H

#include "resource_common.h"
#include "oneapi/tbb/global_control.h"
#include <atomic>
#include <cctype>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <regex>
#include <queue>
#include <stdexcept>
#include <fstream>

// ============================================================================
// Tracing Configuration Enums (Forward declarations for use in DataProduct)
// ============================================================================

enum class TraceLevel {
    OFF,      // No tracing
    MINIMAL,  // Latency + algorithm duration events only
    FULL      // Minimal + DataProduct allocation/deallocation
};

// NOT RESET BETWEEN TRIALS, deliberately
struct MemoryGauge {
    // UNITS: byte-MICROseconds.
    // current sum of estimated_memory_usage_bytes over live products
    static inline std::atomic<std::uint64_t> live_bytes{0};
    // running maximum of live bytes
    static inline std::atomic<std::uint64_t> peak_bytes{0};
    // the integral of live_bytes over time, in byte-microseconds
    static inline std::atomic<std::uint64_t> byte_us{0};

    static inline std::atomic<std::uint64_t> constructed{0};
    static inline std::atomic<std::uint64_t> destroyed{0};

    static void on_construct(std::uint64_t bytes) {
        const std::uint64_t level =
            live_bytes.fetch_add(bytes, std::memory_order_relaxed) + bytes;
        std::uint64_t seen = peak_bytes.load(std::memory_order_relaxed);
        while (seen < level && !peak_bytes.compare_exchange_weak(
                                   seen, level, std::memory_order_relaxed,
                                   std::memory_order_relaxed)) {
        }
        constructed.fetch_add(1, std::memory_order_relaxed);
    }

    static void on_destroy(std::uint64_t bytes, std::int64_t lifetime_ns) {
        live_bytes.fetch_sub(bytes, std::memory_order_relaxed);
        const std::uint64_t lifetime_us =
            lifetime_ns > 0 ? static_cast<std::uint64_t>(lifetime_ns) / 1000u : 0u;
        byte_us.fetch_add(bytes * lifetime_us, std::memory_order_relaxed);
        destroyed.fetch_add(1, std::memory_order_relaxed);
    }
};

// ============================================================================
// MessageClock: absolute admission and completion times, per message
// ============================================================================
struct MessageClock {
    static constexpr std::int64_t kUnset = -1;
    static inline std::unique_ptr<std::atomic<std::int64_t>[]> admit_ns;
    static inline std::unique_ptr<std::atomic<std::int64_t>[]> done_ns;
    static inline int capacity = 0;

    // Called once, before any trial. num_inputs is fixed for the whole run.
    static void configure(int num_inputs) {
        capacity = num_inputs > 0 ? num_inputs : 0;
        if (capacity == 0) {
            admit_ns.reset();
            done_ns.reset();
            return;
        }
        admit_ns = std::make_unique<std::atomic<std::int64_t>[]>(capacity);
        done_ns = std::make_unique<std::atomic<std::int64_t>[]>(capacity);
        begin_trial();
    }

    // Called between trials, on the serial path with no graph work in flight, so
    // plain relaxed stores are enough -- the graph tasks that read these cells are
    // spawned after this returns and joined before it is called again.
    static void begin_trial() {
        for (int i = 0; i < capacity; ++i) {
            admit_ns[i].store(kUnset, std::memory_order_relaxed);
            done_ns[i].store(kUnset, std::memory_order_relaxed);
        }
    }

    static void on_admit(int message_id) {
        if (message_id < 0 || message_id >= capacity) return;
        admit_ns[message_id].store(bench_now_ns(), std::memory_order_relaxed);
    }

    static void on_product_death(int message_id, std::int64_t now_ns) {
        if (message_id < 0 || message_id >= capacity) return;
        std::atomic<std::int64_t>& cell = done_ns[message_id];
        std::int64_t seen = cell.load(std::memory_order_relaxed);
        while (seen < now_ns && !cell.compare_exchange_weak(
                                   seen, now_ns, std::memory_order_relaxed,
                                   std::memory_order_relaxed)) {
        }
    }
};

// One trial's steady-state window, reduced from MessageClock after
// wait_for_all().
struct SteadyWindow {
    double span_s = 0.0;         // length of the steady interval
    int completions = 0;         // messages that finished inside it
    double residence_mean_s = 0.0;  // MEAN message residence; used to trim each end
    double in_flight_mean = 0.0; // mean messages resident over the steady interval
    int in_flight_max = 0;       // max messages resident at any instant of the trial
    double execution_s = 0.0;    // first admission to last completion
    double occupancy_drift = 0.0;
    double rate_drift = 0.0;
    const char* skip_reason = nullptr;
};

// Take the per-message timestamps in MessageClock and work out a "steady-state" 
// interval: the time when the pipeline is neither filling up nor draining. 
// Calculate throughput and occupancy inside that interval and check whether 
// the interval really is steady.
inline SteadyWindow reduce_steady_window(int num_inputs) {
    SteadyWindow out;

    if (num_inputs <= 0 || num_inputs != MessageClock::capacity) {
        out.skip_reason = "message clock is not configured for this num_inputs";
        return out;
    }

    std::vector<std::int64_t> admit(static_cast<std::size_t>(num_inputs));
    std::vector<std::int64_t> done(static_cast<std::size_t>(num_inputs));
    for (int id = 0; id < num_inputs; ++id) {
        admit[id] = MessageClock::admit_ns[id].load(std::memory_order_relaxed);
        done[id] = MessageClock::done_ns[id].load(std::memory_order_relaxed);
        if (admit[id] == MessageClock::kUnset ||
            done[id] == MessageClock::kUnset) {
            out.skip_reason = "a message has no admission or completion stamp";
            return out;
        }
    }

    const std::int64_t t0 = *std::min_element(admit.begin(), admit.end());
    const std::int64_t t1 = *std::max_element(done.begin(), done.end());
    out.execution_s = static_cast<double>(t1 - t0) / 1e9;

    std::vector<std::pair<std::int64_t, int>> events;
    events.reserve(static_cast<std::size_t>(num_inputs) * 2u);
    for (int id = 0; id < num_inputs; ++id) {
        events.emplace_back(admit[id], +1);
        events.emplace_back(done[id], -1);
    }
    std::sort(events.begin(), events.end(),
              [](const std::pair<std::int64_t, int>& a,
                 const std::pair<std::int64_t, int>& b) {
                  if (a.first != b.first) return a.first < b.first;
                  return a.second > b.second;
              });

    if (const char* dump_path = std::getenv("BENCH_OCCUPANCY_DUMP")) {
        std::ofstream dump(dump_path);
        if (dump) {
            dump << "t_s,resident,delta\n";
            int running = 0;
            for (const auto& ev : events) {
                running += ev.second;
                dump << (static_cast<double>(ev.first - t0) / 1e9) << ',' << running
                     << ',' << ev.second << '\n';
            }
        }
    }

    double residence_sum_s = 0.0;
    for (int id = 0; id < num_inputs; ++id) {
        residence_sum_s += static_cast<double>(done[id] - admit[id]) / 1e9;
    }
    out.residence_mean_s = residence_sum_s / static_cast<double>(num_inputs);

    const std::int64_t trim_ns = static_cast<std::int64_t>(out.residence_mean_s * 1e9);
    const std::int64_t lo = t0 + trim_ns;
    const std::int64_t hi = t1 - trim_ns;

    const std::int64_t mid = lo + (hi - lo) / 2;
    int resident = 0;
    double integral_first_s = 0.0;
    double integral_second_s = 0.0;
    std::int64_t prev = events.empty() ? t0 : events.front().first;
    for (const auto& ev : events) {
        if (ev.first > prev) {
            const auto integrate = [&](std::int64_t a, std::int64_t b, double& acc) {
                const std::int64_t seg_lo = std::max(prev, a);
                const std::int64_t seg_hi = std::min(ev.first, b);
                if (seg_hi > seg_lo) {
                    acc += static_cast<double>(resident) *
                           (static_cast<double>(seg_hi - seg_lo) / 1e9);
                }
            };
            integrate(lo, mid, integral_first_s);
            integrate(mid, hi, integral_second_s);
            prev = ev.first;
        }
        resident += ev.second;
        if (resident > out.in_flight_max) out.in_flight_max = resident;
    }

    if (hi - lo < trim_ns || trim_ns <= 0) {
        out.skip_reason =
            "run is shorter than 3 mean residence times; steady state not reached "
            "(raise --num_inputs, or lower C)";
        return out;
    }
    out.span_s = static_cast<double>(hi - lo) / 1e9;
    out.in_flight_mean = (integral_first_s + integral_second_s) / out.span_s;

    const double half_mean_first = integral_first_s /
                                   (static_cast<double>(mid - lo) / 1e9);
    const double half_mean_second = integral_second_s /
                                    (static_cast<double>(hi - mid) / 1e9);
    if (out.in_flight_mean > 0.0) {
        out.occupancy_drift = (half_mean_second - half_mean_first) / out.in_flight_mean;
    }

    int completions_first = 0;
    int completions_second = 0;
    for (int id = 0; id < num_inputs; ++id) {
        if (done[id] > lo && done[id] <= mid) {
            ++completions_first;
        } else if (done[id] > mid && done[id] <= hi) {
            ++completions_second;
        }
    }
    out.completions = completions_first + completions_second;
    if (completions_first < 2 || completions_second < 2) {
        out.skip_reason =
            "fewer than 2 completions in each half of the steady interval; too few "
            "messages to establish a rate (raise --num_inputs)";
        return out;
    }
    const double rate_first = static_cast<double>(completions_first) /
                              (static_cast<double>(mid - lo) / 1e9);
    const double rate_second = static_cast<double>(completions_second) /
                               (static_cast<double>(hi - mid) / 1e9);
    out.rate_drift = 2.0 * (rate_second - rate_first) / (rate_second + rate_first);
    if (std::abs(out.rate_drift) > 0.25) {
        out.skip_reason =
            "completion rate is not stationary across the measured interval; steady "
            "state not reached (raise --num_inputs)";
    }
    return out;
}

// ============================================================================
// DataProduct Class
// ============================================================================

// A DataProduct represents a single unit of data produced by the workflow, 
// including its metadata and memory usage. A node can use and/or generate
// more than one DataProduct.
class DataProduct {
public:
    std::string name;           // Full name from JSON
    std::string type;           // Name of the type
    std::string instance;       // Optional instance name
    uint64_t estimated_memory_usage_bytes;  // Parsed from JSON
    int message_id;             // Links to originating input message
    int thread_id;              // Algorithm-specific thread ID for tracing
    std::int64_t birth_ns;

#if USE_TRACE > 0
    TraceCollector* trace_collector;
    TraceLevel trace_level;     // Runtime control: MINIMAL vs FULL

    DataProduct(const std::string& name_, const std::string& type_,
                const std::string& instance_, uint64_t mem_bytes, int msg_id,
                int tid,  // Algorithm-specific thread ID
                TraceCollector* tc = nullptr,
                TraceLevel level = TraceLevel::MINIMAL)
        : name(name_), type(type_), instance(instance_),
          estimated_memory_usage_bytes(mem_bytes), message_id(msg_id),
          thread_id(tid), birth_ns(bench_now_ns()), trace_collector(tc),
          trace_level(level)
    {
        MemoryGauge::on_construct(estimated_memory_usage_bytes);

        // Only emit alloc events in FULL mode
        if (trace_collector && trace_level == TraceLevel::FULL) {
            trace_collector->record_instant_event(
                "DataProduct_alloc:" + name + ":" + std::to_string(estimated_memory_usage_bytes),
                thread_id  // Use algorithm-specific tid
            );
        }
    }

    ~DataProduct() {
        // Only emit free events in FULL mode
        if (trace_collector && trace_level == TraceLevel::FULL) {
            trace_collector->record_instant_event(
                "DataProduct_free:" + name + ":" + std::to_string(estimated_memory_usage_bytes),
                thread_id
            );
        }
    }
#else
    // Simplified constructor for USE_TRACE == 0
    DataProduct(const std::string& name_, const std::string& type_,
                const std::string& instance_, uint64_t mem_bytes, int msg_id, int tid,
                void* tc = nullptr, TraceLevel level = TraceLevel::MINIMAL)
        : name(name_), type(type_), instance(instance_),
          estimated_memory_usage_bytes(mem_bytes), message_id(msg_id),
          thread_id(tid), birth_ns(bench_now_ns())
    {
        MemoryGauge::on_construct(estimated_memory_usage_bytes);
    }
#endif
};

// ============================================================================
// InputMessage Struct
// ============================================================================

// Nodes that have inputs that are not generated by another node receive them 
// as InputMessage instances. The map is a super-set of all DataProducts not
// generated internally in the workflow.
struct InputMessage {
    int message_id;
    std::map<std::string, std::shared_ptr<DataProduct>> external_inputs;
};

// ============================================================================
// DataProductDeleter Functor
// ============================================================================

// The memory associated with a DataProduct is recycled when there are no more
// references to it and it has been deleted.
class DataProductDeleter {
    void* input_generator_ptr;

public:
    template<typename NodeType>
    DataProductDeleter(NodeType* gen)
        : input_generator_ptr(static_cast<void*>(gen)) {}

    void operator()(DataProduct* dp) {
        uint64_t freed_bytes = dp->estimated_memory_usage_bytes;
        const int message_id = dp->message_id;
        const std::int64_t birth_ns = dp->birth_ns;
        delete dp;  // Destructor emits trace event
        const std::int64_t death_ns = bench_now_ns();
        MemoryGauge::on_destroy(freed_bytes, death_ns - birth_ns);
        MessageClock::on_product_death(message_id, death_ns);

        if (input_generator_ptr) {
            // We know it's a multifunction_node<int64_t, ...>
            auto* gen = static_cast<oneapi::tbb::flow::multifunction_node<int64_t, std::tuple<std::map<std::string, std::shared_ptr<DataProduct>>>>*>(input_generator_ptr);
            gen->try_put(static_cast<int64_t>(freed_bytes));
        }
        bench_latency().complete(message_id);
    }
};

// ============================================================================
// Helper Functions
// ============================================================================

// Parse memory size strings like "1.5 MB", "632.4 KB", "17 B"
inline uint64_t parse_memory_size(const std::string& size_str) {
    std::regex pattern(R"(([\d.]+)\s*([A-Za-z]+))");
    std::smatch match;

    if (!std::regex_match(size_str, match, pattern)) {
        throw std::runtime_error("Invalid memory size format: " + size_str);
    }

    double value = std::stod(match[1].str());
    std::string unit = match[2].str();

    // Convert to uppercase
    for (auto& c : unit) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    // Convert to bytes
    if (unit == "B") {
        return static_cast<uint64_t>(value);
    } else if (unit == "KB") {
        return static_cast<uint64_t>(value * 1024);
    } else if (unit == "MB") {
        return static_cast<uint64_t>(value * 1024 * 1024);
    } else if (unit == "GB") {
        return static_cast<uint64_t>(value * 1024ULL * 1024 * 1024);
    } else {
        throw std::runtime_error("Unknown memory unit: " + unit);
    }
}

// Parse execution time strings like "0.493973 s", "2.64797e-05 s"
inline double parse_execution_time(const std::string& time_str) {
    // Remove trailing " s"
    std::string num_str = time_str;
    size_t pos = num_str.find(" s");
    if (pos != std::string::npos) {
        num_str = num_str.substr(0, pos);
    }
    return std::stod(num_str);
}

// ============================================================================
// WorkflowConfig Struct
// ============================================================================

struct WorkflowConfig {
    std::string json_path;
    int num_threads = 4;
    double gb_per_thread = 2.0;
    int num_inputs = 100;
    int num_trials = 1;
    TraceLevel trace_level = TraceLevel::OFF;
    TracingMode tracing_mode = TracingMode::LAZY;
    bool allow_unbound_window = false;

    static int parse_int_flag(const std::string& key, const std::string& value) {
        const long parsed = bench_parse_long(key, value);
        if (parsed < INT_MIN || parsed > INT_MAX) {
            bench_die("--" + key + "=" + value + " does not fit in an int");
        }
        return static_cast<int>(parsed);
    }

    static WorkflowConfig parse_args(int argc, char* argv[]) {
        WorkflowConfig config;

        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];

            // Check for --flag=value format
            size_t eq_pos = arg.find('=');
            std::string flag, value;

            if (eq_pos != std::string::npos) {
                flag = arg.substr(0, eq_pos);
                value = arg.substr(eq_pos + 1);
            } else if (i + 1 < argc) {
                // --flag value format
                flag = arg;
                value = argv[++i];
            } else {
                std::cerr << "Warning: Ignoring incomplete flag: " << arg << "\n";
                continue;
            }

            // Parse flags
            if (flag == "--json") {
                config.json_path = value;
            } else if (flag == "--num_threads") {
                config.num_threads = parse_int_flag("num_threads", value);
            } else if (flag == "--gb_per_thread") {
                config.gb_per_thread = bench_parse_double("gb_per_thread", value);
            } else if (flag == "--num_inputs") {
                config.num_inputs = parse_int_flag("num_inputs", value);
            } else if (flag == "--num_trials") {
                config.num_trials = parse_int_flag("num_trials", value);
            } else if (flag == "--allow_unbound_window") {
                config.allow_unbound_window = parse_int_flag("allow_unbound_window", value) != 0;
            } else if (flag == "--trace") {
                if (value == "off") {
                    config.trace_level = TraceLevel::OFF;
                } else if (value == "minimal") {
                    config.trace_level = TraceLevel::MINIMAL;
                } else if (value == "full") {
                    config.trace_level = TraceLevel::FULL;
                } else {
                    std::cerr << "Unknown trace level: " << value
                              << " (expected: off, minimal, full)\n";
                    std::exit(1);
                }
            } else if (flag == "--tracing-mode") {
                if (value == "off") {
                    config.tracing_mode = TracingMode::OFF;
                } else if (value == "eager") {
                    config.tracing_mode = TracingMode::EAGER;
                } else if (value == "lazy") {
                    config.tracing_mode = TracingMode::LAZY;
                } else {
                    std::cerr << "Unknown tracing mode: " << value
                              << " (expected: off, eager, lazy)\n";
                    std::exit(1);
                }
            } else {
                std::cerr << "Warning: Unknown flag: " << flag << "\n";
            }
        }

        // Validate required arguments
        if (config.json_path.empty()) {
            throw std::runtime_error("Missing required argument: --json=<path>");
        }

        return config;
    }
};

// ============================================================================
// WorkflowMetadata Structures
// ============================================================================

struct AlgorithmInfo {
    std::string name;
    double execution_time_seconds;
    std::vector<std::string> produces;
    std::vector<std::string> consumes;
    std::vector<std::string> resource_usage;
};

struct DataProductInfo {
    std::string name;
    std::string type;
    std::string instance;
    uint64_t memory_usage_bytes;
};

struct WorkflowMetadata {
    std::map<std::string, AlgorithmInfo> algorithms;
    std::map<std::string, DataProductInfo> data_products;
    std::vector<std::string> limited_resources;
    uint64_t total_workflow_memory;
    double critical_path_seconds = 0.0;

    std::set<std::string> external_inputs;  // Products with no producer
    std::map<std::string, std::string> product_to_producer;
    std::map<std::string, std::vector<std::string>> product_to_consumers;
    std::vector<std::string> topological_order;
    std::map<std::string, int> algorithm_to_tid;  // Map algorithm name to thread ID for tracing

    void analyze() {
        // 1. Build product_to_producer mapping
        for (const auto& [algo_name, algo_info] : algorithms) {
            for (const auto& product_name : algo_info.produces) {
                product_to_producer[product_name] = algo_name;
            }
        }

        // 2. Identify external inputs (products with no producer)
        for (const auto& [algo_name, algo_info] : algorithms) {
            for (const auto& product_name : algo_info.consumes) {
                if (product_to_producer.find(product_name) == product_to_producer.end()) {
                    external_inputs.insert(product_name);
                }
            }
        }

        // 3. Build product_to_consumers mapping
        for (const auto& [algo_name, algo_info] : algorithms) {
            for (const auto& product_name : algo_info.consumes) {
                product_to_consumers[product_name].push_back(algo_name);
            }
        }

        // 4. Compute topological order (Kahn's algorithm)
        std::map<std::string, int> in_degree;
        for (const auto& [algo_name, _] : algorithms) {
            in_degree[algo_name] = 0;
        }

        for (const auto& [algo_name, algo_info] : algorithms) {
            for (const auto& consumed : algo_info.consumes) {
                if (product_to_producer.count(consumed)) {
                    in_degree[algo_name]++;
                }
            }
        }

        std::queue<std::string> queue;
        for (const auto& [algo_name, deg] : in_degree) {
            if (deg == 0) queue.push(algo_name);
        }

        while (!queue.empty()) {
            std::string current = queue.front();
            queue.pop();
            topological_order.push_back(current);

            for (const auto& produced : algorithms.at(current).produces) {
                for (const auto& consumer : product_to_consumers[produced]) {
                    if (--in_degree[consumer] == 0) {
                        queue.push(consumer);
                    }
                }
            }
        }

        if (topological_order.size() != algorithms.size()) {
            throw std::runtime_error("Cycle detected in workflow graph!");
        }

        // 5. Calculate total workflow memory
        total_workflow_memory = 0;
        for (const auto& [_, prod_info] : data_products) {
            total_workflow_memory += prod_info.memory_usage_bytes;
        }

        // 6. Longest path through the DAG, weighted by declared execution time.
        std::map<std::string, double> finish_seconds;
        for (const auto& algo_name : topological_order) {
            const AlgorithmInfo& algo_info = algorithms.at(algo_name);
            double ready_at = 0.0;
            for (const auto& consumed : algo_info.consumes) {
                auto producer = product_to_producer.find(consumed);
                if (producer == product_to_producer.end()) continue;  // external input
                ready_at = std::max(ready_at, finish_seconds.at(producer->second));
            }
            const double finish = ready_at + algo_info.execution_time_seconds;
            finish_seconds[algo_name] = finish;
            critical_path_seconds = std::max(critical_path_seconds, finish);
        }
    }

    // Load from JSON - will be implemented with nlohmann/json
    static WorkflowMetadata load_from_json(const std::string& path);
};

// The number of DataProduct objects ONE message creates
inline int products_per_message(const WorkflowMetadata& metadata) {
    std::size_t n = metadata.external_inputs.size();
    for (const auto& [_, algo_info] : metadata.algorithms) {
        n += algo_info.produces.size();
    }
    return static_cast<int>(n);
}

#endif // TBB_test_perf_workflow_runner_H
