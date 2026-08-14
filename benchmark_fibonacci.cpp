#define TBB_PREVIEW_TASK_GROUP_EXTENSIONS 1
#include <oneapi/tbb/task_group.h>
#include <oneapi/tbb/tick_count.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

using fibonacci_int_type = std::uint64_t;

fibonacci_int_type serial_fibonacci(std::size_t n) {
    if (n < 2) return fibonacci_int_type(n);
    return serial_fibonacci(n - 1) + serial_fibonacci(n - 2);
}

tbb::task_handle parallel_fibonacci(tbb::task_group& tg, std::size_t n, fibonacci_int_type* placeholder) {
    static constexpr std::size_t serial_cutoff = 10;
    tbb::task_handle bypass_task;

    if (n <= serial_cutoff) {
        *placeholder = serial_fibonacci(n);
    } else {
        fibonacci_int_type* prev_placeholder = new fibonacci_int_type(0);
        fibonacci_int_type* prev_prev_placeholder = new fibonacci_int_type(0);

        tbb::task_handle prev_task = tg.defer([&tg, n, prev_placeholder] {
            return parallel_fibonacci(tg, n - 1, prev_placeholder);
        });

        tbb::task_handle prev_prev_task = tg.defer([&tg, n, prev_prev_placeholder] {
            return parallel_fibonacci(tg, n - 2, prev_prev_placeholder);
        });

        tbb::task_handle merge_task = tg.defer([=] {
            *placeholder = *prev_placeholder + *prev_prev_placeholder;
            delete prev_placeholder;
            delete prev_prev_placeholder;
        });

        tbb::task_group::set_task_order(prev_task, merge_task);
        tbb::task_group::set_task_order(prev_prev_task, merge_task);
        tbb::task_group::transfer_this_task_completion_to(merge_task);

        tg.run(std::move(prev_prev_task));
        tg.run(std::move(merge_task));
        bypass_task = std::move(prev_task);
    }

    return bypass_task;
}

int main() {
    const std::size_t n = 40;
    const int warmup_runs = 3;
    const int timed_runs = 15;

    fibonacci_int_type result = 0;

    auto run_once = [&] {
        result = 0;
        tbb::task_group tg;
        tg.run_and_wait([&tg, n, &result] {
            return parallel_fibonacci(tg, n, &result);
        });
    };

    // Warmup: spin up worker threads and warm caches/allocator so the timed
    // region does not pay one-time TBB thread-pool startup costs.
    for (int i = 0; i < warmup_runs; ++i) {
        run_once();
    }

    std::vector<double> samples;
    samples.reserve(timed_runs);
    for (int i = 0; i < timed_runs; ++i) {
        tbb::tick_count start = tbb::tick_count::now();
        run_once();
        tbb::tick_count finish = tbb::tick_count::now();
        samples.push_back((finish - start).seconds());
    }

    std::sort(samples.begin(), samples.end());
    const double min_time = samples.front();
    const double median_time = samples[samples.size() / 2];

    double sum = 0.0;
    for (double s : samples) sum += s;
    const double mean_time = sum / samples.size();

    double variance = 0.0;
    for (double s : samples) variance += (s - mean_time) * (s - mean_time);
    variance /= samples.size();
    const double stddev = std::sqrt(variance);
    const double cv = mean_time > 0.0 ? stddev / mean_time : 0.0;

    std::cout << "Nth Fibonacci Number: " << result << std::endl;
    std::cout << "runs=" << timed_runs
              << " min=" << min_time
              << " median=" << median_time
              << " mean=" << mean_time
              << " stddev=" << stddev
              << " cv=" << cv << std::endl;
}
