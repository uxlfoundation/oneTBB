/*
   Copyright (C) 2023 Intel Corporation

   SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

*/

#include "tbb/parallel_for.h"
#include "tbb/task_group.h"
#include "tbb/task_arena.h"
#include "tbb/global_control.h"
#include "omp.h"
#include <chrono>
#include <iostream>
#include <vector>
#include <numeric>
#include <cmath>
#include <thread>
#include <string>
#include <stdexcept>

/**
 * Benchmarking utilities
 */
struct measurement_t {
using time_point_t = std::chrono::steady_clock::time_point;
    time_point_t begin;
    time_point_t end;
};

struct measurement_stats_t {
    std::size_t num_measurements;
    std::chrono::steady_clock::rep min;
    std::chrono::steady_clock::rep mean;
    std::chrono::steady_clock::rep median;
    std::chrono::steady_clock::rep max;
    // TODO: Add MAD calculations
};

struct measurements_t {
    measurements_t(unsigned repetitions) { m_measurements.reserve(repetitions); }
    void add(measurement_t m) { m_measurements.push_back(m); }
    measurement_stats_t compute_statistics() {
        std::vector<std::chrono::steady_clock::rep> durations = calc_durations(m_measurements);
        std::sort(durations.begin(), durations.end());
        std::chrono::steady_clock::rep total = std::accumulate(durations.cbegin(),
                                                               durations.cend(), 0);
        auto const& min = durations[0];
        auto const& avg = static_cast<std::chrono::steady_clock::rep>(total / durations.size());
        auto const& median = calc_median(durations);
        auto const& max = durations[durations.size() - 1];

        return {durations.size(), min, avg, median, max};
    }
private:
    std::vector<measurement_t> m_measurements;

    std::vector<std::chrono::steady_clock::rep> calc_durations(std::vector<measurement_t> const& v) {
        std::vector<std::chrono::steady_clock::rep> durations(v.size());
        for (std::size_t i = 0; i < m_measurements.size(); ++i) {
            const measurement_t& m = m_measurements[i];
            std::chrono::steady_clock::rep duration =
                std::chrono::duration_cast<std::chrono::milliseconds>(m.end - m.begin).count();
            durations[i] = duration;
        }
        return durations;
    }

    std::chrono::steady_clock::rep calc_median(std::vector<std::chrono::steady_clock::rep> const& v) {
        std::size_t mid = v.size() / 2;
        std::chrono::steady_clock::rep m = v[mid];
        if (v.size() % 2 == 0)
            m = (m + v[mid - 1]) / 2;
        return m;
    }
};

struct benchmark_data_holder {
    benchmark_data_holder(std::size_t size)
        : work_size(size), A(work_size, 1.5f), B(work_size, 42.42f), C(work_size, 0.f)
    {}

    std::size_t work_size;
    std::vector<float> A;
    std::vector<float> B;
    std::vector<float> C;
    float mult = 2.42f;
};

template <typename Benchmark>
void measure_performance(std::size_t size, unsigned repetitions, Benchmark benchmark) {
    benchmark_data_holder holder(size);
    measurements_t measurements(repetitions);
    for (unsigned i = 0; i < repetitions; ++i) {
        auto begin = std::chrono::steady_clock::now();

        benchmark(holder);

        auto end = std::chrono::steady_clock::now();
        measurements.add({begin, end});
    }

    measurement_stats_t s = measurements.compute_statistics();
    std::cout << Benchmark::name() << ": min=" << s.min << "ms, mean=" << s.mean
    << "ms, median="
    << s.median << "ms, max=" << s.max << "ms" << std::endl;
}

/**
 * Compositions
 */
struct single_tbb_loop {
    void operator() (benchmark_data_holder& holder) {
        tbb::parallel_for(
            tbb::blocked_range<std::size_t>(0, holder.work_size),
            [&] (tbb::blocked_range<std::size_t>& r) {
                for (auto it = r.begin(); it != r.end(); ++it) {
                    holder.C[it] += holder.mult * holder.A[it] + holder.B[it];
                    holder.C[it] *= std::sin(holder.C[it]) * std::tan(holder.C[it]) * holder.mult;
                }
            });
    }

    static const char* name() {
        return "tbb loop";
    }
};

struct single_task_group {
    void operator() (benchmark_data_holder& holder) {
        tbb::task_group tg;

        std::size_t num_tbb_threads = tbb::this_task_arena::max_concurrency() / 4;
        std::size_t proportion = holder.work_size / num_tbb_threads;

        for (std::size_t i = 0; i < num_tbb_threads; ++i) {
            tg.run([&, i] {
                for (auto it = proportion * i; it != proportion * (i + 1); ++it) {
                    holder.C[it] += holder.mult * holder.A[it] + holder.B[it];
                    holder.C[it] *= std::sin(holder.C[it]) * std::tan(holder.C[it]) * holder.mult;
                }
            });
        }
        tg.wait();
    }

    static const char* name() {
        return "implicit arena tbb task group";
    }
};

struct concurrent_tbb_loops_in_separate_arenas {
    void operator() (benchmark_data_holder& holder) {
        const std::size_t num_threads = unsigned(tbb::this_task_arena::max_concurrency());

        const unsigned per_thread_arenas = 10;
        const unsigned max_arenas = per_thread_arenas * num_threads;

        std::vector<tbb::task_arena> arenas(max_arenas);
        std::vector<std::thread> thread_pool;

        std::atomic<std::size_t> thread_barrier{0};
        auto thread_func = [&] (unsigned start, unsigned end) {
            ++thread_barrier;
            while (thread_barrier < num_threads) {}
            for (unsigned i = start; i < end; ++i) {
                arenas[i].execute([&] {
                    tbb::parallel_for(
                        tbb::blocked_range<std::size_t>(0, holder.work_size / max_arenas),
                        [&] (tbb::blocked_range<std::size_t>& r) {
                            for (auto it = r.begin(); it != r.end(); ++it) {
                                holder.C[it] += holder.mult * holder.A[it] + holder.B[it];
                                holder.C[it] *= std::sin(holder.C[it]) * std::tan(holder.C[it]) * holder.mult;
                            }
                        });
                });
            }
        };

        for (std::size_t i = 0; i < num_threads; ++i) {
            thread_pool.emplace_back(thread_func, i * per_thread_arenas, (i + 1) * per_thread_arenas);
        }

        for (auto&& thread : thread_pool) {
            thread.join();
        }
    }

    static const char* name() {
        return "concurrent tbb loops within explicit arenas";
    }
};

struct nested_over_subrange_tbb_loop_in_separate_arenas {
    void operator() (benchmark_data_holder& holder) {
        tbb::parallel_for(
            tbb::blocked_range<std::size_t>(0, holder.work_size),
            [&] (tbb::blocked_range<std::size_t>& r) {
                tbb::task_arena arena;
                arena.execute([&] {
                    tbb::parallel_for(r.begin(), r.end(), [&] (std::size_t it) {
                        holder.C[it] += holder.mult * holder.A[it] + holder.B[it];
                        holder.C[it] *= std::sin(holder.C[it]) * std::tan(holder.C[it]) * holder.mult;
                    });
                });
            });
    }

    static const char* name() {
        return "explicit arena over subrange tbb loop nested within implicit arena tbb loop";
    }
};

struct nested_over_subrange_omp_loop_within_tbb_loop {
    void operator() (benchmark_data_holder& holder) {
        tbb::parallel_for(
            tbb::blocked_range<std::size_t>(0, holder.work_size),
            [&] (tbb::blocked_range<std::size_t>& r) {
                #pragma omp parallel for
                for (auto it = r.begin(); it != r.end(); ++it) {
                    holder.C[it] += holder.mult * holder.A[it] + holder.B[it];
                    holder.C[it] *= std::sin(holder.C[it]) * std::tan(holder.C[it]) * holder.mult;
                }
            });
    }

    static const char* name() {
        return "over subrange omp loop nested within implicit arena tbb loop";
    }
};

struct nested_per_iter_omp_loop_within_tbb_loop {
    nested_per_iter_omp_loop_within_tbb_loop(std::size_t num_tbb_iters, std::size_t num_omp_iters)
        : m_num_tbb_iters(num_tbb_iters), m_num_omp_iters(num_omp_iters)
    {}

    void operator() (benchmark_data_holder& holder) {
        tbb::parallel_for(
            tbb::blocked_range<std::size_t>(0, m_num_tbb_iters),
            [&] (tbb::blocked_range<std::size_t>& r) {
                for (auto it = r.begin(); it != r.end(); ++it) {
                    #pragma omp parallel for
                    for (std::size_t j = 0; j < m_num_omp_iters; ++j) {
                        const std::size_t offset = it * m_num_omp_iters + j;
                        holder.C[offset] += holder.mult * holder.A[offset] + holder.B[offset];
                        holder.C[offset] *=
                            std::sin(holder.C[offset]) * std::tan(holder.C[offset]) * holder.mult;
                    }
                }
            }
        );
    }

    static const char* name() {
        return "per iteration omp loop nested within implicit arena tbb loop";
    }
private:
    const std::size_t m_num_tbb_iters;
    const std::size_t m_num_omp_iters;
};

struct nested_over_subrange_omp_loop_within_task_group {
    nested_over_subrange_omp_loop_within_task_group(std::size_t num_tbb_iters,
                                                    std::size_t num_omp_iters)
        : m_num_tbb_iters(num_tbb_iters), m_num_omp_iters(num_omp_iters)
    {}

    void operator() (benchmark_data_holder& holder) {
        tbb::task_group tg;

        const std::size_t proportion = m_num_omp_iters;

        for (std::size_t i = 0; i < m_num_tbb_iters; ++i) {
            tg.run([&, i] {
                #pragma omp parallel for
                for (auto it = proportion * i; it != proportion * (i + 1); ++it) {
                    holder.C[it] += holder.mult * holder.A[it] + holder.B[it];
                    holder.C[it] *= std::sin(holder.C[it]) * std::tan(holder.C[it]) * holder.mult;
                }
            });
        }
        tg.wait();
    }

    static const char* name() {
        return "omp loop nested within implicit arena tbb task group";
    }

private:
    const std::size_t m_num_tbb_iters;
    const std::size_t m_num_omp_iters;
};

int main(int argc, char* argv[]) {
    const int num_tbb_threads = tbb::this_task_arena::max_concurrency();
    if (num_tbb_threads <= 0)
        throw std::out_of_range("Negative number of TBB threads: " + std::to_string(num_tbb_threads));

    const unsigned num_threads = unsigned(num_tbb_threads);
    std::cout << "Number of threads: " << num_threads << std::endl;

    std::size_t env_size = 10'000'000 * num_threads;
    // TODO: Reuse argument_parser from utils.h
    if (argc > 1)
        env_size = std::stoull(argv[1]);
    const std::size_t size = env_size;
    std::cout << "Workload size: " << size << std::endl;
    std::cout << "Per thread work size: " << size / num_threads << std::endl;

    unsigned env_repetitions = 20;
    if (argc > 2)
        env_repetitions = std::stoul(argv[2]);
    const unsigned repetitions = env_repetitions;
    std::cout << "Repetitions of each workload: " << repetitions << std::endl;

    // Single oneTBB runtime, without compositions
    measure_performance(size, repetitions, single_tbb_loop{});
    measure_performance(size, repetitions, single_task_group{});

    // Compositions of single oneTBB runtime
    measure_performance(size, repetitions, concurrent_tbb_loops_in_separate_arenas{});
    measure_performance(size, repetitions, nested_over_subrange_tbb_loop_in_separate_arenas{});

    // Compositions of oneTBB with OpenMP
    omp_set_num_threads(num_threads);
    const std::size_t num_omp_threads = unsigned(omp_get_max_threads());
    if (num_threads != num_omp_threads) {
        throw std::logic_error(
            "Unable to set number of OpenMP threads to " + std::to_string(num_threads) +
            ", OpenMP still wants to use " + std::to_string(num_omp_threads) + " instead"
        );
    }
    measure_performance(size, repetitions, nested_over_subrange_omp_loop_within_tbb_loop{});
    {
        const std::size_t iters_per_tbb_thread = 10;
        const std::size_t num_tbb_iters = iters_per_tbb_thread * num_tbb_threads;
        const std::size_t num_omp_iters = size / num_tbb_iters;

        nested_per_iter_omp_loop_within_tbb_loop benchmark(num_tbb_iters, num_omp_iters);
        measure_performance(size, repetitions, benchmark);
    }
    {
        // Only two tasks per outer thread to have larger inner loops
        const std::size_t num_tbb_iters = 2 * num_threads;
        const std::size_t num_omp_iters = size / num_tbb_iters;
        nested_over_subrange_omp_loop_within_task_group bench(num_tbb_iters, num_omp_iters);
        measure_performance(size, repetitions, bench);
    }
}
