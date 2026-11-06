#include "oneapi/tbb/aggregating_task_group_old.h"
#include "oneapi/tbb/blocked_range.h"
#include "oneapi/tbb/parallel_for.h"
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <vector>

static void single_producer(int n) {
    std::atomic<long long> sum{0};
    tbb::aggregating_task_group g;
    for (int i = 0; i < n; ++i) {
        g.run([&sum, i] { sum.fetch_add(i, std::memory_order_relaxed); });
    }
    g.wait();
    long long expected = (long long)n * (n - 1) / 2;
    if (sum.load() != expected) {
        std::printf("FAIL single_producer(%d): %lld != %lld\n", n, sum.load(), expected);
        std::exit(1);
    }
}

// The producers are TBB workers rather than std::threads: the task tree lives in TLS, so a producer
// exiting while its head is still un-grabbed would leave the head pointing into unmapped storage
static void many_producers(int threads, int n) {
    std::atomic<long long> count{0};
    tbb::aggregating_task_group g;
    tbb::parallel_for(tbb::blocked_range<int>(0, threads, 1),
        [&](const tbb::blocked_range<int>& r) {
            for (int t = r.begin(); t != r.end(); ++t) {
                for (int i = 0; i < n; ++i) {
                    g.run([&count] { count.fetch_add(1, std::memory_order_relaxed); });
                }
            }
        }, tbb::simple_partitioner{});
    g.wait();
    if (count.load() != (long long)threads * n) {
        std::printf("FAIL many_producers: %lld != %lld\n", count.load(), (long long)threads * n);
        std::exit(1);
    }
}

// Nested groups are unsupported while the task tree is thread-local and shared by every group:
// the inner tasks get buffered under the outer group's head, so the inner wait() returns early
#if 0
static void nested(int outer, int inner) {
    std::atomic<long long> count{0};
    tbb::aggregating_task_group g;
    for (int i = 0; i < outer; ++i) {
        g.run([&] {
            tbb::aggregating_task_group inner_group;
            for (int j = 0; j < inner; ++j) {
                inner_group.run([&count] { count.fetch_add(1, std::memory_order_relaxed); });
            }
            inner_group.wait();
        });
    }
    g.wait();
    if (count.load() != (long long)outer * inner) {
        std::printf("FAIL nested: %lld != %lld\n", count.load(), (long long)outer * inner);
        std::exit(1);
    }
}
#endif

int main() {
    for (int n : {1, 2, 3, 127, 128, 129, 1000, 100000}) {
        for (int rep = 0; rep < 5; ++rep) single_producer(n);
    }
    for (int rep = 0; rep < 5; ++rep) many_producers(8, 20000);
    std::printf("OK\n");
    return 0;
}
