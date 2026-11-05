// Many producers, many consumers, with a skewed submission rate.
//
// A few "heavy" producers submit most of the work while the remaining ones submit very little. With
// a plain task_group the heavy producers' deques become the hot spots: thieves pile onto those two
// slots and ignore the nearly empty ones. Aggregation turns each heavy backlog into a single stolen
// head that the thief then fans out across its own deque, so the distribution spreads instead of
// funnelling through the original owners.
//
// The producers are TBB workers rather than std::threads, because the task tree lives in TLS and a
// producer exiting with an un-grabbed head would leave that head pointing into unmapped storage.

#ifdef USE_FOREST
#include "oneapi/tbb/aggregating_task_group_super_new.h"
#else
#include "oneapi/tbb/aggregating_task_group_mega_new.h"
#endif
#include "oneapi/tbb/blocked_range.h"
#include "oneapi/tbb/parallel_for.h"
#include "oneapi/tbb/task_group.h"
#include "oneapi/tbb/task_arena.h"
#include "oneapi/tbb/tick_count.h"

#include <algorithm>
#include <cstdio>
#include <numeric>
#include <vector>

namespace {

void cpu_work(std::size_t grain) {
    for (volatile std::size_t i = 0; i < grain; ++i) {}
}

template <typename TaskGroup>
void run_task_group(tbb::task_arena& arena, const std::vector<int>& tasks_per_producer, std::size_t grain) {
    const int num_producers = static_cast<int>(tasks_per_producer.size());

    arena.execute([&] {
        TaskGroup tg;
        // Grainsize 1 with simple_partitioner keeps one producer per task, so the skew is preserved
        tbb::parallel_for(tbb::blocked_range<int>(0, num_producers, 1),
            [&](const tbb::blocked_range<int>& range) {
                for (int p = range.begin(); p != range.end(); ++p) {
                    const int count = tasks_per_producer[p];
                    for (int i = 0; i < count; ++i) {
                        tg.run([grain] { cpu_work(grain); });
                    }
                }
            }, tbb::simple_partitioner{});
        tg.wait();
    });
}

// Reference point: the same skewed work distributed by recursive splitting instead of through the
// heavy producers' deques. The outer loop keeps the per-producer imbalance
void run_parallel_for(tbb::task_arena& arena, const std::vector<int>& tasks_per_producer, std::size_t grain) {
    const int num_producers = static_cast<int>(tasks_per_producer.size());

    arena.execute([&] {
        tbb::parallel_for(tbb::blocked_range<int>(0, num_producers, 1),
            [&](const tbb::blocked_range<int>& outer) {
                for (int p = outer.begin(); p != outer.end(); ++p) {
                    tbb::parallel_for(tbb::blocked_range<int>(0, tasks_per_producer[p], 1),
                        [grain](const tbb::blocked_range<int>& inner) {
                            for (int i = inner.begin(); i != inner.end(); ++i) cpu_work(grain);
                        }, tbb::simple_partitioner{});
                }
            }, tbb::simple_partitioner{});
    });
}

template <typename Body>
double measure(Body body, int reps) {
    body();

    std::vector<double> times;
    times.reserve(reps);
    for (int r = 0; r < reps; ++r) {
        auto start = tbb::tick_count::now();
        body();
        times.push_back((tbb::tick_count::now() - start).seconds());
    }
    std::sort(times.begin(), times.end());
    return times[times.size() / 2];
}

// The heavy producers share heavy_share of the work, the light ones split the remainder
std::vector<int> make_distribution(int num_producers, int num_heavy, int total_tasks, double heavy_share) {
    std::vector<int> tasks_per_producer(num_producers, 0);

    const int heavy_total = static_cast<int>(total_tasks * heavy_share);
    const int num_light = num_producers - num_heavy;

    for (int p = 0; p < num_heavy; ++p) {
        tasks_per_producer[p] = heavy_total / num_heavy;
    }
    for (int p = num_heavy; p < num_producers; ++p) {
        tasks_per_producer[p] = num_light > 0 ? (total_tasks - heavy_total) / num_light : 0;
    }
    return tasks_per_producer;
}

} // namespace

int main() {
    constexpr int total_tasks = 200000;
    constexpr int num_heavy = 2;
    constexpr double heavy_share = 0.9;
    constexpr int reps = 7;

    tbb::task_arena arena;
    arena.initialize();

    // Leave at least half the arena free to consume
    const int num_producers = std::max(num_heavy + 1, arena.max_concurrency() / 4);

    std::vector<int> tasks_per_producer =
        make_distribution(num_producers, num_heavy, total_tasks, heavy_share);
    const int actual_total = std::accumulate(tasks_per_producer.begin(), tasks_per_producer.end(), 0);

    std::printf("%d producers (%d heavy) in an arena of %d, %d tasks, median of %d reps\n",
                num_producers, num_heavy, arena.max_concurrency(), actual_total, reps);
    std::printf("per-producer counts:");
    for (int count : tasks_per_producer) std::printf(" %d", count);
    std::printf("\n\n");

    std::printf("%8s %14s %14s %14s %10s %10s\n",
                "grain", "parallel_for", "task_group", "aggregating", "tg/agg", "pfor/agg");

    for (std::size_t grain : {std::size_t(0), std::size_t(50), std::size_t(500), std::size_t(5000)}) {
        double pfor       = measure([&] { run_parallel_for(arena, tasks_per_producer, grain); }, reps);
        double baseline   = measure([&] { run_task_group<tbb::task_group>(arena, tasks_per_producer, grain); }, reps);
        double aggregated = measure([&] { run_task_group<tbb::aggregating_task_group>(arena, tasks_per_producer, grain); }, reps);

        std::printf("%8zu %14.6f %14.6f %14.6f %9.2fx %9.2fx\n",
                    grain, pfor, baseline, aggregated,
                    baseline / aggregated, pfor / aggregated);
    }

    return 0;
}
