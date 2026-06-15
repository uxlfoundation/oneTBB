/*
    rwbench — reader/writer lock microbenchmark.

    Design follows the BRAVO paper's rwbench (Dice & Kogan, USENIX ATC 2019):
    each thread loops { acquire(read|write) -> work-in-CS -> release -> think } and
    we measure aggregate throughput (ops/s). The same templated code path runs every
    mutex type, so comparisons are apples-to-apples.

    The three axes that matter for an RW lock are all swept here:
      - write ratio        (write_permille): how often a thread takes the writer lock.
      - thread count        (threads):       the scaling curve.
      - critical-section /   (cs_work,        the contention knob: a lock that wins at
        think-time ratio      think_work)     CS=0 can lose at CS=long, so vary both.

    Two mutex types are built into one binary:
      - tbb::rw_mutex                         (baseline)
      - BRAVO_rw_mutex<tbb::rw_mutex>         (the lock under test)

    BRAVO note: bias starts OFF and only turns on after a slow-path reader flips it,
    so a warm-up phase is run and discarded before each measured window.

    Output: CSV to stdout, one row per (mutex, threads, write_permille, cs_work,
    think_work) configuration, suitable for the existing charts/ pipeline.

    Env knobs (all optional):
      BENCH_THREADS        comma list of thread counts   (default "1,2,4,8,<hw>")
      BENCH_WRITE_PERMILLE comma list, writes per 1000   (default "0,1,10,100")
      BENCH_CS_WORK        spin loops inside the CS       (default 16)
      BENCH_THINK_WORK     spin loops outside the CS      (default 64)
      BENCH_SECONDS        measured seconds per config    (default 2)
      BENCH_WARMUP_MS      discarded warm-up per config   (default 300)
*/

#include <oneapi/tbb/rw_mutex.h>
#include <oneapi/tbb/bravo_rw_mutex.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

// ---- env / parsing helpers ------------------------------------------------

std::size_t env_size(const char* name, std::size_t def) {
    const char* v = std::getenv(name);
    if (!v || !*v) return def;
    return static_cast<std::size_t>(std::strtoull(v, nullptr, 10));
}

std::vector<std::size_t> env_list(const char* name, std::vector<std::size_t> def) {
    const char* v = std::getenv(name);
    if (!v || !*v) return def;
    std::vector<std::size_t> out;
    const char* p = v;
    while (*p) {
        char* end = nullptr;
        unsigned long long x = std::strtoull(p, &end, 10);
        if (end == p) break;
        out.push_back(static_cast<std::size_t>(x));
        p = end;
        while (*p == ',' || *p == ' ') ++p;
    }
    return out.empty() ? def : out;
}

// xorshift64*: cheap thread-local RNG, no shared state.
struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed ? seed : 0x9E3779B97F4A7C15ull) {}
    std::uint64_t next() {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return s * 0x2545F4914F6CDD1Dull;
    }
};

// Bounded spin "work" that the optimizer cannot elide.
inline void busy(std::size_t loops) {
    volatile std::uint64_t sink = 0;
    for (std::size_t i = 0; i < loops; ++i) sink += i * 2654435761u;
    (void)sink;
}

// Per-thread op counter on its own cache line (no false sharing on the counter).
struct alignas(64) PaddedCounter {
    std::uint64_t value = 0;
    char pad[64 - sizeof(std::uint64_t)];
};

struct Cfg {
    std::size_t threads;
    std::size_t write_permille;
    std::size_t cs_work;
    std::size_t think_work;
    std::size_t seconds;
    std::size_t warmup_ms;
};

// ---- the measured loop (identical for every mutex type) -------------------

template <class M>
void worker(M& mutex, const Cfg& cfg, std::size_t id,
            std::atomic<bool>& go, std::atomic<bool>& stop,
            std::atomic<bool>& measure, PaddedCounter& ops) {
    Rng rng(0x100000001b3ull ^ (id * 2654435761u));

    while (!go.load(std::memory_order_acquire)) std::this_thread::yield();

    bool counting = false;
    while (!stop.load(std::memory_order_relaxed)) {
        const bool is_write = (rng.next() % 1000) < cfg.write_permille;
        {
            typename M::scoped_lock lock(mutex, /*write=*/is_write);
            busy(cfg.cs_work);
        }
        busy(cfg.think_work);

        // Start counting only once the measured window opens (warm-up discarded).
        if (!counting) {
            counting = measure.load(std::memory_order_relaxed);
            if (!counting) continue;
        }
        ++ops.value;
    }
}

template <class M>
double run_one(const char* mutex_name, const Cfg& cfg) {
    M mutex;
    std::vector<PaddedCounter> ops(cfg.threads);
    std::atomic<bool> go{false}, stop{false}, measure{false};

    std::vector<std::thread> ts;
    ts.reserve(cfg.threads);
    for (std::size_t i = 0; i < cfg.threads; ++i)
        ts.emplace_back(worker<M>, std::ref(mutex), std::cref(cfg), i,
                        std::ref(go), std::ref(stop), std::ref(measure), std::ref(ops[i]));

    using clock = std::chrono::steady_clock;
    go.store(true, std::memory_order_release);

    // Warm-up (discarded).
    std::this_thread::sleep_for(std::chrono::milliseconds(cfg.warmup_ms));

    // Open the measured window.
    measure.store(true, std::memory_order_relaxed);
    auto t0 = clock::now();
    std::this_thread::sleep_for(std::chrono::seconds(cfg.seconds));
    auto t1 = clock::now();

    stop.store(true, std::memory_order_relaxed);
    for (auto& t : ts) t.join();

    std::uint64_t total = 0;
    for (auto& c : ops) total += c.value;

    const double secs = std::chrono::duration<double>(t1 - t0).count();
    const double ops_per_s = secs > 0.0 ? total / secs : 0.0;

    std::cout << mutex_name << ','
              << cfg.threads << ','
              << cfg.write_permille << ','
              << cfg.cs_work << ','
              << cfg.think_work << ','
              << secs << ','
              << total << ','
              << ops_per_s << '\n'
              << std::flush;

    return ops_per_s;
}

} // namespace

int main() {
    const std::size_t hw = std::max<std::size_t>(1, std::thread::hardware_concurrency());

    const std::vector<std::size_t> thread_counts =
        env_list("BENCH_THREADS", {1, 2, 4, 8, hw});
    const std::vector<std::size_t> write_permilles =
        env_list("BENCH_WRITE_PERMILLE", {0, 1, 10, 100});

    Cfg base{};
    base.cs_work    = env_size("BENCH_CS_WORK", 16);
    base.think_work = env_size("BENCH_THINK_WORK", 64);
    base.seconds    = env_size("BENCH_SECONDS", 2);
    base.warmup_ms  = env_size("BENCH_WARMUP_MS", 300);

    // CSV header.
    std::cout << "mutex,threads,write_permille,cs_work,think_work,seconds,ops,ops_per_s\n";

    using Bravo = tbb::detail::d1::BRAVO_rw_mutex<tbb::rw_mutex>;

    for (std::size_t wp : write_permilles) {
        for (std::size_t p : thread_counts) {
            Cfg cfg = base;
            cfg.threads = p;
            cfg.write_permille = wp;
            run_one<tbb::rw_mutex>("rw_mutex", cfg);
            run_one<Bravo>("bravo_rw_mutex", cfg);
        }
    }

    return 0;
}
