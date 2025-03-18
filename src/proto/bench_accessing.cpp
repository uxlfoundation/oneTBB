// g++ bench_accessing.cpp -I../../include -lbenchmark -fopenmp && ./a.out

// Copyright (C) 2025 Anton Malakhov
//
// SPDX-License-Identifier: Apache-2.0

#include <benchmark/benchmark.h>
#include <vector>
#include <memory>
#include <mutex>
#include <random>
#include "synchronizer.h"

using std::size_t;
using namespace tbb;
constexpr size_t big_sz   = 100000000l;
constexpr size_t iterations = big_sz;
constexpr size_t short_sz = 1000;
auto bdsti = {0., 1000., 10000., double(big_sz-1)};
auto bdstw = {1., 1., .2, 0.};
std::piecewise_linear_distribution<> big_dst{bdsti.begin(), bdsti.end(), bdstw.begin()};
auto sdsti = {0., double(short_sz-1)};
auto sdstw = {0., 1.};
std::piecewise_linear_distribution<> short_dst{sdsti.begin(), sdsti.end(), sdstw.begin()};
std::discrete_distribution<> operations_dst({80, 10, 10});
std::random_device g_rnd{};
thread_local std::mt19937 rng{g_rnd()};
using myops_t = std::vector<std::tuple<char, short, size_t>>;

static void accessing_stl(benchmark::State& state) {
  using mydata_t = std::pair<std::mutex, size_t>;
  using myvector_t = std::vector<std::shared_ptr<mydata_t>>;
  myvector_t big_array; myops_t op_array;
  static thread_local myvector_t short_cache;
  op_array.resize(iterations);
  big_array.resize(big_sz);

  #pragma omp parallel for
  for(size_t i = 0; i < iterations; i++)
    op_array[i] = {char(operations_dst(rng)), short(short_dst(rng)), size_t(big_dst(rng))};

  #pragma omp parallel for
  for(size_t i = 0; i < big_sz; i++)
    big_array[i] = std::make_shared<mydata_t>();

  #pragma omp parallel
  {
    short_cache.resize(short_sz);
    for(auto &c : short_cache)
      c = big_array[size_t(big_dst(rng))];
  }
  for (auto _ : state) {
    size_t acc = 0;
    #pragma omp parallel for schedule(nonmonotonic:dynamic, 2000) private(acc)
    for(size_t i = 0; i < iterations; i++) {
      switch(std::get<0>(op_array[i])) {
        case(0): { // чтение из элемента кэша
          auto *p = short_cache[std::get<1>(op_array[i])].get();
          {
            std::unique_lock l(p->first);
            acc += p->second;
          }
          break; }
        case(1): { // запись в элемент кэша
          auto *p = short_cache[std::get<1>(op_array[i])].get();
          {
            std::unique_lock l(p->first);
            p->second = acc; acc = 0;
          }
          break; }
        case(2): { // замена элемента кэша с чтением
          auto &r = short_cache[std::get<1>(op_array[i])];
          r = big_array[std::get<2>(op_array[i])];
          {
            std::unique_lock l(r->first);
            acc = r->second;
          }
          break; }
      }
      benchmark::DoNotOptimize(acc);
    }
  }
}
// Register the function as a benchmark
BENCHMARK(accessing_stl)->Unit(benchmark::kMillisecond)->UseRealTime();

static void accessing_seqlock(benchmark::State& state) {
  versioned_synchronizer::preserve_state ownership = access::preserve();
  versioned_synchronizer::concurrent_state lock = access::exclusive();
  versioned_synchronizer init; init.init_lock(ownership);

  using mydata_t = std::pair<versioned_synchronizer, size_t>;
  using myvector_t = std::vector<mydata_t>;
  myvector_t big_array; myops_t op_array;
  static thread_local std::vector<mydata_t*> short_cache;
  op_array.resize(iterations);
  big_array.resize(big_sz, mydata_t{init, 1});

  #pragma omp parallel for
  for(size_t i = 0; i < iterations; i++)
    op_array[i] = {char(operations_dst(rng)), short(short_dst(rng)), size_t(big_dst(rng))};

  #pragma omp parallel
  {
    short_cache.resize(short_sz);
    for(auto &p : short_cache) {
      p = &big_array[size_t(big_dst(rng))];
      p->first.blocking_try_lock(ownership);
    }
  }
  for (auto _ : state) {
    size_t acc = 0;
    #pragma omp parallel for schedule(nonmonotonic:dynamic, 2000) private(acc)
    for(size_t i = 0; i < iterations; i++) {
      switch(std::get<0>(op_array[i])) {
        case(0): { // чтение из элемента кэша
          auto *p = short_cache[std::get<1>(op_array[i])];
          size_t res, v0;
          do {
            v0 = p->first.get_version();
            res = p->second;
          } while(v0 != size_t(p->first.get_version()));
          acc += res;
          break; }
        case(1): { // запись в элемент кэша
          auto *p = short_cache[std::get<1>(op_array[i])];
          if(p->first.blocking_try_lock(lock) == access::acquired) {
            p->second = acc; acc = 0;
            p->first.unlock(lock);
          }
          break; }
        case(2): { // замена элемента кэша с чтением
          auto *&p = short_cache[std::get<1>(op_array[i])];
          p->first.unlock(ownership);
          p = &big_array[std::get<2>(op_array[i])];
          if(p->first.blocking_try_lock(lock) == access::acquired) {
            acc = p->second;
            p->first.move_to(ownership, lock);
          }
          break; }
      }
      benchmark::DoNotOptimize(acc);
    }
  }
}
// Register the function as a benchmark
BENCHMARK(accessing_seqlock)->Unit(benchmark::kMillisecond)->UseRealTime();

BENCHMARK_MAIN();
