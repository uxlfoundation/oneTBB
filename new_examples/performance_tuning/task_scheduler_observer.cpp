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
#include <sstream>
#include <thread>
#include <tbb/tbb.h>

// these are placeholder for where we would put OS-specific types and calls
using affinity_mask_t = std::string;
void set_thread_affinity( int tid, const affinity_mask_t& mask ) {
  std::ostringstream buffer;
  buffer << std::this_thread::get_id()
         << " -> (" << tid
         << ", " << mask << ")\n";
  std::cout << buffer.str();
}
void restore_thread_affinity() {
  std::ostringstream buffer;
  buffer <<  std::this_thread::get_id()
         << " -> (restored)\n";
  std::cout << buffer.str();
}

// observer class
class PinningObserver : public tbb::task_scheduler_observer {
public:
    // HW affinity mask to be used for threads in an arena
    affinity_mask_t m_mask;
    PinningObserver( oneapi::tbb::task_arena &a, const affinity_mask_t& mask )
        : tbb::task_scheduler_observer(a), m_mask(mask) {
        observe(true); // activate the observer
    }
    void on_scheduler_entry( bool worker ) override {
        set_thread_affinity(
            tbb::this_task_arena::current_thread_index(), m_mask);
    }
    void on_scheduler_exit( bool worker ) override {
        restore_thread_affinity();
    }
};

int N = 1000;
double w = 0.01;
double f(double v);

using counter_t = std::atomic<int>;
counter_t counter(0);
void waitUntil(int N, counter_t& c) {
  ++c;
  while (c != N);
}

void observeTwoArenas() {
  int P = tbb::info::default_concurrency();

  // two arenas, each with half the hw threads
  tbb::task_arena a0(P/2);
  tbb::task_arena a1(P/2);

  PinningObserver obs0(a0, "mask_zero");
  PinningObserver obs1(a1, "mask_one");

  // Execute consecutive loops
  std::cout << "Execute a0 loop\n";
  a0.execute([] {
    tbb::parallel_for(0, N, [](int j) { f(w); });
  });
  std::cout << "Execute a1 loop\n";
  a1.execute([] {
    tbb::parallel_for(0, N, [](int j) { f(w); });
  });

  // Execute concurrent loops
  std::cout << "Execute a0 and a1 concurrently\n";
  std::thread t0([&]() {
    waitUntil(2, counter);
    a0.execute([] {
      tbb::parallel_for(0, N, [](int j) { f(w); });
    });
  });
  std::thread t1([&]() {
    waitUntil(2, counter);
    a1.execute([] {
      tbb::parallel_for(0, N, [](int j) { f(w); });
    });
  });
  t0.join();
  t1.join();
}

int main() {
  observeTwoArenas();
  return 0;
}

double f(double v) {
  tbb::tick_count t0 = tbb::tick_count::now();
  while ((tbb::tick_count::now() - t0).seconds() < 0.01);
  return 2*v;
}
