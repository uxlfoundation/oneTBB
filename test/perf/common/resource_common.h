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


#ifndef TBB_test_perf_resource_common_H
#define TBB_test_perf_resource_common_H


// USE_LIMITER_NODE selects which of the two implementations is measured. It is
// the only switch, and it alone determines the variant (VARIANT_NAME below):
//
//   0 = jn_impl: the join-based implementation -- buffer_node + reserving
//       join_node + multifunction_node, with the body handing resources back
//       explicitly. Uses no resource-limiting code. The preview macro is allowed
//       but not needed: the feature only adds templates this variant never
//       instantiates. JN_BODY_POLICY (below) is a setting of this variant.
//   1 = limiter_impl: resource_limiter / resource_limited_node, as implemented
//       by whichever oneTBB tree is on the include path -- master, or the branch
//       under test. VARIANT_TREE (below) is where a build says which.
#ifndef USE_LIMITER_NODE
#define USE_LIMITER_NODE 0
#endif

#if USE_LIMITER_NODE != 0 && USE_LIMITER_NODE != 1
#error "USE_LIMITER_NODE must be 0 (jn_impl) or 1 (limiter_impl)."
#endif

#if USE_LIMITER_NODE == 1 && !defined(TBB_PREVIEW_FLOW_GRAPH_RESOURCE_LIMITING)
#  error "USE_LIMITER_NODE==1 needs -DTBB_PREVIEW_FLOW_GRAPH_RESOURCE_LIMITING=1 from the build."
#endif

#include <algorithm>  // std::min, for bench_resolved_threads; std::sort, for BenchLatency
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdint>    // std::int64_t, for BenchLatency's nanosecond stamps
#include <cstdio>     // std::snprintf, for bench_num and bench_json_num
#include <cstdlib>
#include <cmath>
#include <limits>     // quiet_NaN, for an empty latency distribution
#include <atomic>
#include <condition_variable>  // BenchWatchdog waits, it does not poll
#include <functional>          // the watchdog's workload-state hook
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "oneapi/tbb/flow_graph.h"
#include "oneapi/tbb/global_control.h"
#include "oneapi/tbb/task_arena.h"
using namespace oneapi::tbb::flow;

#if USE_LIMITER_NODE == 1 && !defined(TBB_HAS_FLOW_GRAPH_RESOURCE_LIMITING)
#  error "the oneTBB tree on the include path has no resource-limiting feature \
(TBB_HAS_FLOW_GRAPH_RESOURCE_LIMITING undefined). Wrong tree, or a tree older \
than the feature."
#endif

// ============================================================================
// Work model -- busy-wait, not sleep_for
// ============================================================================

#if !(defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)) || \
    defined(_M_ARM64EC)
#  error "resource_common.h supports x86 (32- or 64-bit) only"
#endif

#if defined(_MSC_VER)
#  include <intrin.h>
#endif

inline void bench_pause() {
#if defined(_MSC_VER) && !defined(__clang__)
    _mm_pause();
#else
    // clang-cl defines _MSC_VER but does have the builtin, so it lands here.
    __builtin_ia32_pause();
#endif
}

// Spin until `us` microseconds of *monotonic* time have elapsed.
inline void busy_wait_us(double us) {
    if (!(us > 0.0)) return;
    using clock = std::chrono::steady_clock;
    const clock::time_point start = clock::now();
    const std::chrono::duration<double, std::micro> target(us);
    while (std::chrono::duration<double, std::micro>(clock::now() - start) < target) {
        bench_pause();
    }
}

// VARIANT_NAME is derived, not supplied: with two variants, USE_LIMITER_NODE
// already says which one this is. It names the variant in the banner and the run
// record, and keeps the two variants' trace files from overwriting each other.
#ifdef VARIANT_NAME
#  error "VARIANT_NAME is derived from USE_LIMITER_NODE; do not pass it from the build."
#endif
#if USE_LIMITER_NODE == 0
#  define VARIANT_NAME "jn_impl"
#else
#  define VARIANT_NAME "limiter_impl"
#endif

// VARIANT_TREE optionally labels the oneTBB tree a binary was built against --
// typically a branch name or a commit -- so that runs of the same variant built
// from different trees can be told apart in the record. Nothing checks it.
#ifndef VARIANT_TREE
#define VARIANT_TREE "unset"
#endif
#ifndef BENCH_COMPILER
#define BENCH_COMPILER "unknown"
#endif

#define BENCH_STRINGIFY_(x) #x
#define BENCH_STRINGIFY(x) BENCH_STRINGIFY_(x)

// USE_TRACE is a compile-time tracing *capability* gate:
//   0 = tracing disabled  (no tracing code compiled in; clean overhead baseline)
//   1 = tracing capable   (runtime --trace= selects off / eager / lazy)
// The eager-vs-lazy decision is made at run time (see TracingMode), not here.
#ifndef USE_TRACE
#define USE_TRACE 1
#endif

// ----------------------------------------------------------------------------
// JN_BODY_POLICY -- the buffering policy of the jn_impl body node.
// This is a slightly misleading name, since this policy is for the 
// multifunction_node inside of the composite_node used for the join based
// implementation. The join_node itself in the composite_node implementation 
// is always reserving. But the multifunction_node body can be queueing or
// rejecting. When it's rejecting, it may avoid the eager resource consumption 
// issue. So being able to test either policy lets us evaluate the impact of
// the multifunction_node's buffering strategy on resource consumption.
// ----------------------------------------------------------------------------
#if USE_LIMITER_NODE == 0
#  ifndef JN_BODY_POLICY
#    define JN_BODY_POLICY queueing
#  endif
using jn_body_policy = oneapi::tbb::flow::JN_BODY_POLICY;
static_assert(std::is_same_v<jn_body_policy, oneapi::tbb::flow::queueing> ||
                  std::is_same_v<jn_body_policy, oneapi::tbb::flow::rejecting>,
              "JN_BODY_POLICY must be queueing or rejecting");
#endif

// The body policy as a string, defined for both variants so the run record has
// one shape: "n/a" where there is no multifunction_node body to have a policy.
#if USE_LIMITER_NODE == 0
#  define BENCH_JN_BODY_POLICY_NAME BENCH_STRINGIFY(JN_BODY_POLICY)
#else
#  define BENCH_JN_BODY_POLICY_NAME "n/a"
#endif

enum class TracingMode { OFF, EAGER, LAZY };

// ============================================================================
// Argument parsing
// ============================================================================

[[noreturn]] inline void bench_die(const std::string& message) {
    std::cerr << "FATAL: " << message << std::endl;
    std::exit(2);
}

inline std::string bench_num(double value) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%g", value);
    return std::string(buf);
}

inline long bench_parse_long(const std::string& key, const std::string& text) {
    if (text.empty()) {
        bench_die("--" + key + " needs a value (got an empty string)");
    }
    errno = 0;
    char* end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (end != text.c_str() + text.size()) {
        bench_die("--" + key + "=" + text + " is not an integer");
    }
    if (errno == ERANGE) {
        bench_die("--" + key + "=" + text + " is out of range");
    }
    return value;
}

inline double bench_parse_double(const std::string& key, const std::string& text) {
    if (text.empty()) {
        bench_die("--" + key + " needs a value (got an empty string)");
    }
    errno = 0;
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end != text.c_str() + text.size()) {
        bench_die("--" + key + "=" + text + " is not a number");
    }
    if (errno == ERANGE) {
        bench_die("--" + key + "=" + text + " is out of range");
    }
    return value;
}

inline const char* trace_mode_name(TracingMode mode) {
    switch (mode) {
    case TracingMode::EAGER: return "eager";
    case TracingMode::LAZY:  return "lazy";
    case TracingMode::OFF:   return "off";
    }
    return "unknown";
}

// ============================================================================
// The thread knob: --max_threads=<n>, 0 = all cores
// ============================================================================
inline int bench_resolved_threads(int max_threads) {
    if (max_threads > 0) return max_threads;
    const int arena = oneapi::tbb::this_task_arena::max_concurrency();
    const int allowed = static_cast<int>(oneapi::tbb::global_control::active_value(
        oneapi::tbb::global_control::max_allowed_parallelism));
    return std::min(arena, allowed);
}

// Installs a parallelism cap, or nothing at all when max_threads <= 0.
inline std::unique_ptr<oneapi::tbb::global_control> bench_thread_limit(int max_threads) {
    if (max_threads <= 0) return nullptr;
    return std::make_unique<oneapi::tbb::global_control>(
        oneapi::tbb::global_control::max_allowed_parallelism,
        static_cast<std::size_t>(max_threads));
}

#if USE_TRACE > 0
#include "trace_collector.h"
#include <sstream>
#endif

// ============================================================================
// jn_impl: the join-based implementation
// ============================================================================

#if USE_LIMITER_NODE == 0

// buffer_node -> reserving join_node -> multifunction_node, presented as a single
// node with one message port and (tuple_size - 1) resource ports.
template <typename InputTuple, typename BodyPolicy = oneapi::tbb::flow::queueing>
class resource_composite_node
    : public oneapi::tbb::flow::composite_node<InputTuple, InputTuple> {
    using base_type = oneapi::tbb::flow::composite_node<InputTuple, InputTuple>;
    using mfn_type = multifunction_node<InputTuple, InputTuple, BodyPolicy>;

    static constexpr std::size_t tuple_size = std::tuple_size<InputTuple>::value;
    static_assert(tuple_size >= 2,
                  "resource_composite_node needs element 0 (the message) plus at "
                  "least one resource handle");

    // a buffer for the first type in the tuple
    buffer_node<std::tuple_element_t<0, InputTuple>> input_buffer;
    join_node<InputTuple, reserving> input_join;
    mfn_type body_node;

    // External port 0 is the message buffer; external ports 1..N-1 are the join's
    // resource inputs. Outputs map one-to-one onto the body's ports.
    template <std::size_t... Is>
    void wire_external_ports(std::index_sequence<Is...>) {
        base_type::set_external_ports(
            std::tie(input_buffer, input_port<Is + 1>(input_join)...),
            std::tie(output_port<0>(body_node), output_port<Is + 1>(body_node)...));
    }

public:
    using input_type = InputTuple;
    using input_ports_type = typename base_type::input_ports_type;
    // The body's ports, which is what a body lambda's second parameter is. Named
    // body_ports_type rather than output_ports_type because the base class
    // already means something different by that name (std::tuple<sender<T>&...>),
    // and shadowing it made `node.output_ports()` and `node_t::output_ports_type`
    // disagree.
    using body_ports_type = typename mfn_type::output_ports_type;
    using body_policy_type = BodyPolicy;

    // we assume that the body does the work of sending back the resources
    // we don't do this automatically
    template <typename BodyFunc>
    resource_composite_node(oneapi::tbb::flow::graph& g, size_t concurrency_limit,
                            BodyFunc body_func)
        : base_type(g),
          input_buffer(g),
          input_join(g),
          body_node(g, concurrency_limit, body_func) {
        // Connect the internal nodes: buffer -> join -> body
        make_edge(input_buffer, input_port<0>(input_join));
        make_edge(input_join, body_node);
        wire_external_ports(std::make_index_sequence<tuple_size - 1>{});
    }
};

template <typename InputTuple>
using jn_composite_node = resource_composite_node<InputTuple, jn_body_policy>;

#else  // limiter_impl

template <typename T>
using limiter_type = oneapi::tbb::flow::resource_limiter<T>;

#endif

// ============================================================================
// The machine-readable run record
// ============================================================================

inline std::string bench_json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            // Windows paths and workload names are the only realistic content;
            // anything below 0x20 would still be invalid JSON, so escape it.
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c) & 0xffu);
                out += buf;
            } else {
                out += c;
            }
        }
    }
    return out;
}

inline std::string bench_json_num(double value) {
    if (!std::isfinite(value)) return "null";  // JSON has no NaN or Infinity
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.12g", value);
    return std::string(buf);
}

class BenchRecord {
public:
    void set(const std::string& key, const std::string& value) {
        put(key, "\"" + bench_json_escape(value) + "\"");
    }
    void set(const std::string& key, const char* value) {
        set(key, std::string(value == nullptr ? "" : value));
    }
    void set(const std::string& key, bool value) { put(key, value ? "true" : "false"); }
    void set(const std::string& key, double value) { put(key, bench_json_num(value)); }
    template <typename T, std::enable_if_t<std::is_integral_v<T> &&
                                           !std::is_same_v<T, bool>, int> = 0>
    void set(const std::string& key, T value) { put(key, std::to_string(value)); }

    std::string json() const {
        std::string out = "{";
        for (std::size_t i = 0; i < fields_.size(); ++i) {
            if (i) out += ",";
            out += "\"" + bench_json_escape(fields_[i].first) + "\":" + fields_[i].second;
        }
        return out + "}";
    }

private:
    void put(const std::string& key, std::string value) {
        for (auto& f : fields_) {
            if (f.first == key) { f.second = std::move(value); return; }
        }
        fields_.emplace_back(key, std::move(value));
    }
    std::vector<std::pair<std::string, std::string>> fields_;
};

inline BenchRecord& bench_record() {
    static BenchRecord instance;
    return instance;
}

#define BENCH_RECORD_SENTINEL "BENCH_RECORD_JSON "

// Prints what was compiled: the variant, the tree it was built against, and the
// setting that tells two builds of the same variant apart -- the body policy for
// jn_impl, the resource-limiting feature version for limiter_impl. Call as the
// first statement of main().
inline void print_variant_banner(const std::string& workload) {
#ifdef TBB_HAS_FLOW_GRAPH_RESOURCE_LIMITING
    const int limiter_version = TBB_HAS_FLOW_GRAPH_RESOURCE_LIMITING;
#else
    const int limiter_version = 0;  // a jn_impl build without the preview macro
#endif

    std::cout << "=== build provenance ===\n";
    std::cout << "  workload:        " << workload << "\n";
    std::cout << "  variant:         " << VARIANT_NAME << "\n";
    std::cout << "  tree:            " << VARIANT_TREE << "\n";
    std::cout << "  compiler:        " << BENCH_COMPILER << "\n";
    std::cout << "  USE_TRACE:       " << USE_TRACE << "\n";
#if USE_LIMITER_NODE == 0
    std::cout << "  jn body policy:  " << BENCH_JN_BODY_POLICY_NAME << "\n";
#else
    std::cout << "  limiter version: " << limiter_version << "\n";
#endif
    std::cout << "========================\n";

    // The provenance half of the machine-readable record (§4.2). Set here rather
    // than in bench_emit_record so there is exactly one source for the human line
    // and the machine field, and so a workload cannot forget one of them. The
    // `workload` value is the banner's own -- which is the CMake target's
    // workload name, the join key §8's loader needs.
    //
    // Schema 2: the variant is jn_impl or limiter_impl, and the per-variant
    // setting is in jn_body_policy / resource_limiting_version, both present in
    // every record. Schema 1 carried macro_id, upstream_sha, patch and
    // limiter_header_sha256 instead.
    bench_record().set("schema", 2);
    bench_record().set("workload", workload);
    bench_record().set("variant", VARIANT_NAME);
    bench_record().set("tree", VARIANT_TREE);
    bench_record().set("use_trace", USE_TRACE);
    bench_record().set("jn_body_policy", BENCH_JN_BODY_POLICY_NAME);
    bench_record().set("resource_limiting_version", limiter_version);
}

// Prints config resolved from the command line and machine.
inline void print_run_model(int max_threads) {
    const int resolved = bench_resolved_threads(max_threads);
    std::cout << "=== run model ===\n";
    // Named for the mechanism, not for the duration, because the mechanism is the
    // §3 correctness fix: a sleeping worker leaves its slot idle and quantizes to
    // the ~15.6 ms Windows timer tick, and neither is true of a spin. A run whose
    // stdout lacks this line came from a binary predating the fix.
    std::cout << "  work model:      busy-wait spin (steady_clock + pause)\n";
    std::cout << "  max_threads:     " << max_threads
              << (max_threads > 0 ? " (capped)" : " (0 = all cores)") << "\n";
    std::cout << "  threads:         " << resolved << "\n";
    std::cout << "  arrival:         n/a (no message arrival model)\n";
    std::cout << "=================\n";

    // The run-model half of the machine-readable record (§4.2). `threads` is the
    // resolved count and `max_threads` the requested one, and they differ at every
    // --max_threads=0 point and at P=24 (global_control binds downward only
    // against the implicit arena's fixed slot count), so the record carries both.
    bench_record().set("max_threads", max_threads);
    bench_record().set("threads", resolved);
    bench_record().set("arrival", "n/a");
    bench_record().set("arrival_prime", 0);
    bench_record().set("arrival_interval_ms", 0.0);
    bench_record().set("arrival_rate", -1.0);
}

// a trivial counting resource for testing purposes
struct counting_resource {
    std::atomic<std::size_t> counter;
    counting_resource() : counter(0) {}
    void use() {
        ++counter;
    }
};

// ----------------------------------------------------------------------------
// Support for per-message latency determination
// ----------------------------------------------------------------------------
//

inline std::int64_t bench_now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

struct alignas(64) BenchLatencySlot {
    std::atomic<std::int64_t> start_ns;
    std::atomic<int> arrived;
};

class BenchLatency {
public:
    static constexpr std::int64_t kUnstamped = -1;

    void configure(int ids, int fanin, std::size_t samples) {
        if (ids <= 0 || fanin <= 0 || samples == 0) {
            bench_die("BenchLatency::configure(ids=" + std::to_string(ids) +
                      ", fanin=" + std::to_string(fanin) + ", samples=" +
                      std::to_string(samples) + "): every argument must be positive. "
                      "A zero here would silently disable the primary metric of the "
                      "rate arms.");
        }
        ids_ = ids;
        fanin_ = fanin;
        slots_ = std::vector<BenchLatencySlot>(static_cast<std::size_t>(ids));
        for (BenchLatencySlot& s : slots_) {
            s.start_ns.store(kUnstamped, std::memory_order_relaxed);
            s.arrived.store(0, std::memory_order_relaxed);
        }
        samples_.assign(samples, 0);
        next_.store(0, std::memory_order_relaxed);
        dropped_.store(0, std::memory_order_relaxed);
        unstamped_.store(0, std::memory_order_relaxed);
        enabled_ = true;
    }

    bool enabled() const { return enabled_; }
    int fanin() const { return fanin_; }
    std::size_t capacity() const { return samples_.size(); }

    void stamp(int id) {
        if (!enabled_ || id < 0 || id >= ids_) return;
        BenchLatencySlot& s = slots_[static_cast<std::size_t>(id)];
        s.arrived.store(0, std::memory_order_relaxed);
        s.start_ns.store(bench_now_ns(), std::memory_order_relaxed);
    }

    void complete(int id) {
        if (!enabled_ || id < 0 || id >= ids_) return;
        BenchLatencySlot& s = slots_[static_cast<std::size_t>(id)];
        if (s.arrived.fetch_add(1, std::memory_order_acq_rel) + 1 != fanin_) return;
        const std::int64_t t0 = s.start_ns.load(std::memory_order_relaxed);
        if (t0 == kUnstamped) {
            unstamped_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        const std::int64_t ns = bench_now_ns() - t0;

        const std::size_t idx = next_.fetch_add(1, std::memory_order_relaxed);
        if (idx >= samples_.size()) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        samples_[idx] = ns;
    }

    // --- readers: SERIAL, after the graph is drained ------------------------
    std::size_t sample_count() const {
        return std::min(next_.load(std::memory_order_acquire), samples_.size());
    }
    std::size_t dropped() const { return dropped_.load(std::memory_order_relaxed); }
    std::size_t unstamped() const { return unstamped_.load(std::memory_order_relaxed); }

    // The recorded samples in ms, in COLLECTION order
    std::vector<double> raw_ms() const {
        std::vector<double> out;
        const std::size_t n = sample_count();
        out.reserve(n);
        for (std::size_t k = 0; k < n; ++k) out.push_back(1e-6 * static_cast<double>(samples_[k]));
        return out;
    }

    // Sorted copy of the recorded samples, in ms. What every quantile is read from.
    std::vector<double> sorted_ms() const {
        std::vector<double> out = raw_ms();
        std::sort(out.begin(), out.end());
        return out;
    }

    // The residual fan-in count, over every id, from the LAST execution. This is
    // how a wrong `fanin` is caught, and it costs nothing on the hot path.
    std::pair<int, int> arrival_residual() const {
        int lo = INT_MAX, hi = 0;
        for (const BenchLatencySlot& s : slots_) {
            const int v = s.arrived.load(std::memory_order_relaxed);
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        if (slots_.empty()) return {0, 0};
        return {lo, hi};
    }

    // Forensics for a hung run: which ids are in flight and for how long. An id is
    // in flight when it has been stamped and fewer than fanin_ terminals have
    // reported it.
    void dump_pending(std::ostream& os, std::size_t max_ids = 8) const {
        if (!enabled_) return;
        const std::int64_t now = bench_now_ns();
        std::size_t pending = 0, shown = 0;
        for (int id = 0; id < ids_; ++id) {
            const BenchLatencySlot& s = slots_[static_cast<std::size_t>(id)];
            const std::int64_t t0 = s.start_ns.load(std::memory_order_relaxed);
            const int arrived = s.arrived.load(std::memory_order_relaxed);
            if (t0 == kUnstamped || arrived >= fanin_) continue;
            ++pending;
            if (shown < max_ids) {
                ++shown;
                os << "    id " << id << ": in flight " << (now - t0) / 1000000
                   << " ms, " << arrived << " of " << fanin_ << " terminals arrived"
                   << std::endl;
            }
        }
        os << "    in flight: " << pending << " of " << ids_ << " ids";
        if (pending > shown) os << " (" << (pending - shown) << " more not shown)";
        os << std::endl;
    }

private:
    alignas(64) std::vector<BenchLatencySlot> slots_;
    std::vector<std::int64_t> samples_;
    int ids_ = 0;
    int fanin_ = 0;
    bool enabled_ = false;
    alignas(64) std::atomic<std::size_t> next_{0};
    std::atomic<std::size_t> dropped_{0};
    std::atomic<std::size_t> unstamped_{0};
};

inline BenchLatency& bench_latency() {
    static BenchLatency instance;
    return instance;
}

// Nearest-rank quantile on an already-sorted vector.
inline double bench_quantile_ms(const std::vector<double>& sorted, double q) {
    if (sorted.empty()) return std::numeric_limits<double>::quiet_NaN();
    std::size_t idx = static_cast<std::size_t>(q * static_cast<double>(sorted.size()));
    if (idx >= sorted.size()) idx = sorted.size() - 1;
    return sorted[idx];
}

inline void validate_latency(std::size_t expected_samples, const char* benchmark_name) {
    const BenchLatency& lat = bench_latency();
    if (!lat.enabled()) {
        std::cerr << "Error: " << benchmark_name << " never called "
                     "bench_latency().configure(); latency is the primary metric of "
                     "both rate arms and this run has none." << std::endl;
        return;
    }
    if (lat.sample_count() != expected_samples) {
        std::cerr << "Error: " << benchmark_name << " recorded " << lat.sample_count()
                  << " latency samples, expected " << expected_samples << "." << std::endl;
    }
    if (lat.dropped() != 0) {
        std::cerr << "Error: " << benchmark_name << " dropped " << lat.dropped()
                  << " latency samples: the capacity passed to configure() ("
                  << lat.capacity() << ") is smaller than the run produces."
                  << std::endl;
    }
    if (lat.unstamped() != 0) {
        std::cerr << "Error: " << benchmark_name << " saw " << lat.unstamped()
                  << " terminal arrivals for messages that were never stamped."
                  << std::endl;
    }
    const std::pair<int, int> residual = lat.arrival_residual();
    if (residual.first != lat.fanin() || residual.second != lat.fanin()) {
        std::cerr << "Error: " << benchmark_name << " terminal fan-in is wrong: "
                  << "configure() was told fanin=" << lat.fanin()
                  << " but the last execution's ids received between "
                  << residual.first << " and " << residual.second
                  << " terminal arrivals each. A fanin below the true leaf count "
                     "records each message's latency on its FIRST leaf, so every "
                     "reported quantile is the minimum over leaves rather than the "
                     "maximum -- and nothing else in this harness can tell."
                  << std::endl;
    }
}

// Writes the one machine-readable line and flushes. Call as the last statement of
// main(), after print_results / validate_resource_usage / validate_arrival_fidelity.
inline void bench_emit_record() {
    const BenchLatency& lat = bench_latency();
    const std::vector<double> ms = lat.sorted_ms();
    double mean = std::numeric_limits<double>::quiet_NaN();
    if (!ms.empty()) {
        double sum = 0.0;
        for (double v : ms) sum += v;
        mean = sum / static_cast<double>(ms.size());
    }
    bench_record().set("lat_configured", lat.enabled());
    bench_record().set("lat_fanin", lat.fanin());
    bench_record().set("lat_fanin_observed_lo", lat.arrival_residual().first);
    bench_record().set("lat_fanin_observed_hi", lat.arrival_residual().second);
    bench_record().set("lat_samples", ms.size());
    bench_record().set("lat_dropped", lat.dropped());
    bench_record().set("lat_unstamped", lat.unstamped());
    bench_record().set("lat_mean_ms", mean);
    bench_record().set("lat_p50_ms", bench_quantile_ms(ms, 0.50));
    bench_record().set("lat_p95_ms", bench_quantile_ms(ms, 0.95));
    bench_record().set("lat_p99_ms", bench_quantile_ms(ms, 0.99));
    bench_record().set("lat_max_ms", ms.empty() ? std::numeric_limits<double>::quiet_NaN()
                                                : ms.back());

    // WHERE the maximum fell, as a fraction of the way through the run in
    // collection order: 0.0 = the very first sample completed, 1.0 = the last.
    {
        const std::vector<double> raw = lat.raw_ms();
        double frac = std::numeric_limits<double>::quiet_NaN();
        if (raw.size() > 1) {
            const std::size_t argmax =
                static_cast<std::size_t>(std::max_element(raw.begin(), raw.end()) -
                                         raw.begin());
            frac = static_cast<double>(argmax) / static_cast<double>(raw.size() - 1);
        } else if (raw.size() == 1) {
            frac = 0.0;
        }
        bench_record().set("lat_max_position", frac);
    }

    std::cout << BENCH_RECORD_SENTINEL << bench_record().json() << "\n";
    if (const char* raw = std::getenv("BENCH_LATENCY_RAW")) {
        if (raw[0] == '1' && raw[1] == '\0') {
            const std::vector<double> unsorted = lat.raw_ms();
            std::cout << "BENCH_LATENCY_RAW_MS " << unsorted.size();
            for (double v : unsorted) std::cout << " " << bench_json_num(v);
            std::cout << "\n";
        }
    }
    std::cout.flush();
}

// To kill stuck runs
constexpr int BENCH_EXIT_WATCHDOG = 86;

inline std::function<void()>& bench_watchdog_hook() {
    static std::function<void()> hook;
    return hook;
}

class BenchWatchdog {
public:
    BenchWatchdog() {
        double seconds = 300.0;
        if (const char* v = std::getenv("BENCH_WATCHDOG_S")) {
            const double d = std::atof(v);
            if (d >= 0.0) seconds = d;
        }
        limit_s_ = seconds;
        if (!(seconds > 0.0)) return;  // explicitly disabled
        thread_ = std::thread([this] { this->run(); });
    }

    ~BenchWatchdog() {
        if (!thread_.joinable()) return;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            done_ = true;
        }
        cv_.notify_all();
        thread_.join();
    }

    BenchWatchdog(const BenchWatchdog&) = delete;
    BenchWatchdog& operator=(const BenchWatchdog&) = delete;

    double limit_s() const { return limit_s_; }

private:
    void run() {
        std::unique_lock<std::mutex> lock(mtx_);
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::duration<double>(limit_s_);
        if (cv_.wait_until(lock, deadline, [this] { return done_; })) {
            return;  // the run finished; nothing to report
        }
        lock.unlock();

        std::cerr << "Error: BENCH WATCHDOG fired after " << limit_s_
                  << " s -- the run did not finish and is being killed from inside "
                     "the process so that it leaves evidence." << std::endl;
        std::cerr << "  latency samples: " << bench_latency().sample_count() << std::endl;
        bench_latency().dump_pending(std::cerr);
        if (bench_watchdog_hook()) {
            std::cerr << "  --- workload state (racy read; the graph is still live) ---"
                      << std::endl;
            bench_watchdog_hook()();
        }

        bench_record().set("watchdog", true);
        bench_record().set("watchdog_limit_s", limit_s_);
        bench_emit_record();

        std::fflush(nullptr);
        std::cerr.flush();
        std::_Exit(BENCH_EXIT_WATCHDOG);
    }

    std::mutex mtx_;
    std::condition_variable cv_;
    std::thread thread_;
    bool done_ = false;
    double limit_s_ = 0.0;
};

#endif // TBB_test_perf_resource_common_H
