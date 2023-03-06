/*
    Copyright (c) 2021-2023 Intel Corporation
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

struct benchmark_data_holder {
    benchmark_data_holder(std::size_t size) : work_size(size), A(work_size, 1.5f), B(work_size, 42.42f), C(work_size, 0.f)
    {}

    std::size_t work_size;
    std::vector<float> A;
    std::vector<float> B;
    std::vector<float> C;
    float mult = 2.42f;
};

struct composable_parallel_for {
    void operator() (benchmark_data_holder& holder) {
        tbb::parallel_for(tbb::blocked_range<std::size_t>(0, holder.work_size), [&] (tbb::blocked_range<std::size_t>& r) {
            #pragma omp parallel for
            for (auto it = r.begin(); it != r.end(); ++it) {
                holder.C[it] += holder.mult * holder.A[it] + holder.B[it];
                holder.C[it] *= std::sin(holder.C[it]) * std::tan(holder.C[it]) * holder.mult;
            }
        });
    }

    static const char* name() {
        return "tbb::parallel_for + openMP";
    }
};

struct two_tbb_parallel_for {
    void operator() (benchmark_data_holder& holder) {
        tbb::parallel_for(tbb::blocked_range<std::size_t>(0, holder.work_size), [&] (tbb::blocked_range<std::size_t>& r) {
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
        return "tbb::parallel_for + tbb::parallel_for";
    }
};

struct tbb_arena_mix {
    void operator() (benchmark_data_holder& holder) {
        std::size_t num_threads = tbb::this_task_arena::max_concurrency(); // TODO init TBB to ensure valid max_concurrency
        int max_arenas = 10 * num_threads;

        // tbb::global_control control(tbb::global_control::max_allowed_parallelism, 1);

        std::vector<tbb::task_arena> arenas(max_arenas);
        std::vector<std::thread> thread_pool;

        std::atomic<std::size_t> thread_barrier{0};
        auto thread_func = [&] (int start, int end) {
            ++thread_barrier;
            while (thread_barrier < num_threads) {}
            for (int i = start; i < end; ++i) {
                arenas[i].execute([&] { 
                    tbb::parallel_for(tbb::blocked_range<std::size_t>(0, holder.work_size / max_arenas), [&] (tbb::blocked_range<std::size_t>& r) {
                        for (auto it = r.begin(); it != r.end(); ++it) {
                            holder.C[it] += holder.mult * holder.A[it] + holder.B[it];
                            holder.C[it] *= std::sin(holder.C[it]) * std::tan(holder.C[it]) * holder.mult;
                        }
                    });
                });
            }
        };

        for (std::size_t i = 0; i < num_threads; ++i) {
            thread_pool.emplace_back(thread_func, i * (max_arenas / num_threads), (i + 1) * (max_arenas / num_threads));
        }

        for (auto&& thread : thread_pool) {
            thread.join();
        }
    }

    static const char* name() {
        return "tbb::arena mix";
    }
};

struct serial_parallel_for {
    void operator() (benchmark_data_holder& holder) {
        tbb::parallel_for(tbb::blocked_range<std::size_t>(0, holder.work_size), [&] (tbb::blocked_range<std::size_t>& r) {
            for (auto it = r.begin(); it != r.end(); ++it) {
                holder.C[it] += holder.mult * holder.A[it] + holder.B[it];
                holder.C[it] *= std::sin(holder.C[it]) * std::tan(holder.C[it]) * holder.mult;
            }
        });
    }

    static const char* name() {
        return "tbb::parallel_for + serial";
    }
};

struct composable_task_group {
    void operator() (benchmark_data_holder& holder) {
        tbb::task_group tg;

        std::size_t num_tbb_threads = tbb::this_task_arena::max_concurrency() / 4;
        std::size_t proportion = holder.work_size / num_tbb_threads;

        for (std::size_t i = 0; i < num_tbb_threads; ++i) {
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
        return "task_group + openMP";
    }
};

struct serial_task_group {
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
        return "task_group + serial";
    }
};

template <typename Benchmark>
void measure_performance(std::size_t size, Benchmark benchmark) {
    benchmark_data_holder holder(size);
    std::vector<std::chrono::steady_clock::rep> times;
    for (int i = 0; i < 20; ++i) {

        auto t1 = std::chrono::steady_clock::now();

        benchmark(holder);

        auto t2 = std::chrono::steady_clock::now();
        times.push_back(std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count());
    }


    std::cout << Benchmark::name() << " - time = " << std::accumulate(times.begin(), times.end(), 0) / times.size() << "ms" << std::endl;
}

int main() {
    std::size_t size = 100000000;
    measure_performance(size, serial_parallel_for{});
    measure_performance(size, two_tbb_parallel_for{});
    measure_performance(size, composable_parallel_for{});
    measure_performance(size, serial_task_group{});
    measure_performance(size, composable_task_group{});
    measure_performance(size, tbb_arena_mix{});
}
