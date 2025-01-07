/*
    Copyright (c) 2024 Intel Corporation

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

#include <iostream>
#include <memory>
#include <vector>
#include <tbb/tbb.h>

void doWork(double sec) {
  tbb::tick_count t0 = tbb::tick_count::now();
  while ((tbb::tick_count::now() - t0).seconds() <= sec);
}

tbb::filter<int,int> makeMiddleFilters(int num_filters, tbb::filter_mode m, double usec,
                                       int imbalanced_filter = -1, double imbalance_factor = 1) {
  double w = (imbalanced_filter == num_filters) ? usec*imbalance_factor : usec;

  auto f = tbb::make_filter<int, int>(m,
                                      [=](int i) -> int {
                                        doWork(w);
                                        return i;
                                      });
  if (num_filters > 1)
    return f & makeMiddleFilters(num_filters-1, m, usec,
                                 imbalanced_filter, imbalance_factor);
  else
    return f;
}

tbb::filter<void,void> buildFilterChain(std::atomic<int>& counter, std::vector<int>& data,
                                        int num_filters, tbb::filter_mode m, double usec,
                                        double imbalance_factor = 1) {
  counter = data.size() - 1;
  tbb::filter<int, int> middle{};
  tbb::filter<int,void> end{};
  int imbalanced_filter = (imbalance_factor == 1) ? -1 : num_filters - 3;

  if (num_filters > 1) {
    tbb::filter<void,int> start =
      tbb::make_filter<void, int>(m,
                                  [&counter, &data, usec](tbb::flow_control& fc) -> int {
                                    int i = counter--;
                                    if (i > 0) {
                                      doWork(usec);
                                      return data[i];
                                    } else {
                                      fc.stop();
                                      return 0;
                                    }
                                  });


      tbb::filter<int, void> end =
        tbb::make_filter<int, void>(m,
                                    [=](int i) {
                                      doWork(usec);
                                    });

      if (num_filters > 2) {
        tbb::filter<int, int> middle = makeMiddleFilters(num_filters-2, m, usec, imbalanced_filter, imbalance_factor);
        return start & middle & end;
      } else {
        return start & end;
      }
  } else {
    return tbb::make_filter<void, void>(m,
                                        [&](tbb::flow_control& fc) {
                                          int i = counter--;
                                          if (i > 0) {
                                            doWork(usec);
                                          } else {
                                            fc.stop();
                                          }
                                        });
  }
}

double runSerial(std::vector<int>& data, int num_filters, double usec, int num_items,
                 int imbalanced_filter = -1, double imbalance_factor = 1) {
  tbb::tick_count t0 = tbb::tick_count::now();
  for (int i = 0; i < num_items; ++i) {
    for (int j = 0; j < num_filters; ++j) {
      if (j == imbalanced_filter) {
        doWork(usec*imbalance_factor);
      } else {
        doWork(usec);
      }
    }
  }
  double total_time = (tbb::tick_count::now() - t0).seconds();
  return total_time;
}

std::vector<int> makeVector(int N);
static void warmupTBB();

std::vector<tbb::filter_mode> modes = {tbb::filter_mode::serial_in_order,
                                        tbb::filter_mode::serial_out_of_order,
                                        tbb::filter_mode::parallel };
std::vector<std::string> mode_name = {"serial_in_order",
                                      "serial_out_of_order",
                                      "parallel"};



void runEightFiltersOneTokenBalanced(int N, std::vector<int>& data) {
  std::vector<double> spin_times = {1e-7, 1e-6, 1e-5, 1e-4};
  int num_filters = 8;

  std::cout << "\nVarying the mode and spin_time for 8 filters:\n";

  // Balanced serial time
  std::vector<double> serial_time;
  for (double spin_time : spin_times) {
    serial_time.push_back(runSerial(data, num_filters, spin_time, N));
  }

  std::cout << "\nmode";
  for (double spin_time : spin_times)
    std::cout << ", " << spin_time;
  std::cout << std::endl;

  for (int m = 0; m < modes.size(); ++m) {
    std::atomic<int> counter(0);
    std::cout << mode_name[m];
    for (int st = 0; st < spin_times.size(); ++st) {
      double spin_time = spin_times[st];

      auto chain =
        buildFilterChain(counter, data, num_filters, modes[m], spin_time);

      tbb::tick_count t0 = tbb::tick_count::now();
      warmupTBB();
      tbb::parallel_pipeline(1, chain);
      double t = (tbb::tick_count::now() - t0).seconds();
      std::cout << ", " << serial_time[st]/t;
    }
    std::cout << std::endl;
  }
}

void runEightTokensIncreasingFilters(int N, std::vector<int>& data) {
  int max_threads = tbb::this_task_arena::max_concurrency();
  double spin_time = 0.0001;

  std::cout << "\nVarying number of filters with 100us spin and 8 tokens:\n";

  // Balanced serial time
  std::vector<double> serial_time;
  for (int num_filters = 1; num_filters <= max_threads; ++num_filters) {
    serial_time.push_back(runSerial(data, num_filters, spin_time, N));
  }

  std::cout << "\nmode";
  for (int num_filters = 1; num_filters <= max_threads; ++num_filters)
    std::cout << ", " << num_filters;
  std::cout << std::endl;

  for (int m = 0; m < modes.size(); ++m) {
    std::atomic<int> counter(0);
    std::cout << mode_name[m];
    for (int num_filters = 1; num_filters <= max_threads; ++num_filters) {
      auto chain =
        buildFilterChain(counter, data, num_filters, modes[m], spin_time);

      tbb::tick_count t0 = tbb::tick_count::now();
      warmupTBB();
      tbb::parallel_pipeline(max_threads, chain);
      double t = (tbb::tick_count::now() - t0).seconds();
      std::cout << ", " << serial_time[num_filters-1]/t;
    }
    std::cout << std::endl;
  }
}

void runEightFiltersIncreasingTokens(int N, std::vector<int>& data) {
  int max_threads = tbb::this_task_arena::max_concurrency();
  int num_filters = 8;
  double spin_time = 0.0001;

  std::cout << "\nVarying number of tokens with 8 filters and 100us spin:\n";

  double serial_time = runSerial(data, num_filters, spin_time, N);

  std::cout << "\nmode";
  for (int num_tokens = 1; num_tokens <= 2*max_threads; ++num_tokens)
    std::cout << ", " << num_tokens;
  std::cout << std::endl;

  for (int m = 0; m < modes.size(); ++m) {
    std::atomic<int> counter(0);
    std::cout << mode_name[m];
    for (int num_tokens = 1; num_tokens <= 2*max_threads; ++num_tokens) {
      auto chain =
        buildFilterChain(counter, data, num_filters, modes[m], spin_time);

      tbb::tick_count t0 = tbb::tick_count::now();
      warmupTBB();
      tbb::parallel_pipeline(num_tokens, chain);
      double t = (tbb::tick_count::now() - t0).seconds();
      std::cout << ", " << serial_time/t;
    }
    std::cout << std::endl;
  }
}

void runEightFiltersImbalanced(int N, std::vector<int>& data) {
  std::vector<double> imbalance = {0.1, 0.5, 0.75, 1.5, 2, 10};
  int num_filters = 8;
  int num_tokens = 8;
  double spin_time = 0.0001;

  std::cout << "\nVarying imbalance of 1 of 8 filters:\n";

  // Imbalanced serial time
  std::vector<double> serial_time;
  for (double imb : imbalance) {
    serial_time.push_back(runSerial(data, num_filters, spin_time, N, imb));
  }

  std::cout << "\nmode";
  for (double imb : imbalance)
    std::cout << ", " << imb;
  std::cout << std::endl;

  for (int m = 0; m < modes.size(); ++m) {
    std::atomic<int> counter(0);
    std::cout << mode_name[m];
    for (int imb = 0; imb < imbalance.size(); ++imb) {
      double imbalance_factor = imbalance[imb];
      auto chain =
        buildFilterChain(counter, data, num_filters, modes[m], spin_time, imbalance_factor);

      tbb::tick_count t0 = tbb::tick_count::now();
      warmupTBB();
      tbb::parallel_pipeline(num_tokens, chain);
      double t = (tbb::tick_count::now() - t0).seconds();
      std::cout << ", " << serial_time[imb]/t;
    }
    std::cout << std::endl;
  }
}

int main() {
  // use the most performance codes
  // only a single NUMA node
  // and only 1 thread per core
  tbb::task_arena::constraints c;
  c.set_numa_id(tbb::info::numa_nodes()[0]);
  c.set_core_type(tbb::info::core_types().front());
  c.set_max_threads_per_core(1);
  c.set_max_concurrency(std::min(8, tbb::info::default_concurrency(c)));
  tbb::task_arena a(c);

  std::cout << "Using an arena with " << a.max_concurrency() << " slots\n";

  a.execute([&]() {
    int N = 8000;
    std::vector<int> data = makeVector(N);
    //runEightFiltersOneTokenBalanced(N, data);
    //runEightTokensIncreasingFilters(N, data);
    //runEightFiltersIncreasingTokens(N, data);
    runEightFiltersImbalanced(N, data);
  });

  return 0;
}

std::vector<int> makeVector(int N) {
   std::vector<int> v;
   v.reserve(N);
   for (int i = 0; i < N; ++i) {
     v.emplace_back(i);
   }
   return v;
}

static void warmupTBB() {
  // This is a simple loop that should get workers started.
  // oneTBB creates workers lazily on first use of the library
  // so this hides the startup time when looking at trivial
  // examples that do little real work.
  tbb::parallel_for(0, tbb::this_task_arena::max_concurrency(),
    [=](int) {
      tbb::tick_count t0 = tbb::tick_count::now();
      while ((tbb::tick_count::now() - t0).seconds() < 0.01);
    }
  );
}
