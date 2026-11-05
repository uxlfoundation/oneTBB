// Single producer, many consumers.
//
// One thread submits every task while the rest of the arena consumes them. With a plain task_group
// this concentrates all steal attempts on one deque, which is the contention aggregation targets.
//
// The producer is the thread that entered the arena, so it stays alive until wait() returns. Plain
// std::thread producers cannot be used while the task tree lives in TLS: a producer exiting with an
// un-grabbed head leaves that head pointing into unmapped storage.

#ifdef USE_FOREST
#include "oneapi/tbb/aggregating_task_group_mega_new.h"
#else
#include "oneapi/tbb/aggregating_task_group_super_new.h"
#endif
#include "oneapi/tbb/blocked_range.h"
#include "oneapi/tbb/parallel_for.h"
#include "oneapi/tbb/task_group.h"
#include "oneapi/tbb/task_arena.h"
#include "oneapi/tbb/tick_count.h"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace {

void cpu_work(std::size_t grain) {
    for (volatile std::size_t i = 0; i < grain; ++i) {}
}

template <typename TaskGroup>
void run_task_group(tbb::task_arena& arena, int num_tasks, std::size_t grain) {
    arena.execute([&] {
        TaskGroup tg;
        for (int i = 0; i < num_tasks; ++i) {
            tg.run([grain] { cpu_work(grain); });
        }
        tg.wait();
    });
}

// Reference point: the same work expressed the idiomatic way, distributed by recursive splitting
// instead of through one producer's deque. Grainsize 1 keeps one body invocation per index
void run_parallel_for(tbb::task_arena& arena, int num_tasks, std::size_t grain) {
    arena.execute([&] {
        tbb::parallel_for(tbb::blocked_range<int>(0, num_tasks, 1),
            [grain](const tbb::blocked_range<int>& range) {
                for (int i = range.begin(); i != range.end(); ++i) cpu_work(grain);
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

} // namespace

int main() {
    constexpr int num_tasks = 1000000;
    constexpr int reps = 7;

    tbb::task_arena arena;
    arena.initialize();

    std::printf("single producer / %d consumers, %d tasks, median of %d reps\n\n",
                arena.max_concurrency(), num_tasks, reps);
    std::printf("%8s %14s %14s %14s %10s %10s\n",
                "grain", "parallel_for", "task_group", "aggregating", "tg/agg", "pfor/agg");

    // Short bodies leave scheduling overhead dominant, long ones drown it out
    for (std::size_t grain : {std::size_t(0), std::size_t(50), std::size_t(500), std::size_t(5000)}) {
        double pfor       = measure([&] { run_parallel_for(arena, num_tasks, grain); }, reps);
        double baseline   = measure([&] { run_task_group<tbb::task_group>(arena, num_tasks, grain); }, reps);
        double aggregated = measure([&] { run_task_group<tbb::aggregating_task_group>(arena, num_tasks, grain); }, reps);

        std::printf("%8zu %14.6f %14.6f %14.6f %9.2fx %9.2fx\n",
                    grain, pfor, baseline, aggregated,
                    baseline / aggregated, pfor / aggregated);
    }

    return 0;
}
