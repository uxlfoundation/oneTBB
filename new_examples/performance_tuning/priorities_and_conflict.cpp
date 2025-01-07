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


#include <thread>
#include <tbb/tbb.h>

void printArrival(tbb::task_arena::priority priority);

using counter_t = std::atomic<int>;
counter_t counter(0);
void waitUntil(int N, counter_t& c) {
  ++c;
  while (c != N);
}

void explicitArenaWithPriority(tbb::task_arena::priority priority) {
  tbb::task_arena a{tbb::info::default_concurrency(), 1, priority};
  a.execute([=]() {
    tbb::parallel_for(0,
                      2*tbb::info::default_concurrency(),
                      [=](int) { printArrival(priority); });
  });
}

void runTwoThreads(tbb::task_arena::priority priority0,
                   tbb::task_arena::priority priority1) {
  std::thread t0([=]() {
    waitUntil(2, counter);
    explicitArenaWithPriority(priority0);
  });
  std::thread t1([=]() {
    waitUntil(2, counter);
    explicitArenaWithPriority(priority1);
  });
  t0.join();
  t1.join();
}

#include <cstdio>

int main() {
  counter = 0;
  std::printf("\n\nrunTwoThreads with low (.) and high (|)\n");
  runTwoThreads(tbb::task_arena::priority::low, tbb::task_arena::priority::high);

  counter = 0;
  std::printf("\n\nrunTwoThreads with low (.) and normal (:)\n");
  runTwoThreads(tbb::task_arena::priority::low, tbb::task_arena::priority::normal);

  counter = 0;
  std::printf("\n\nrunTwoThreads with normal (:) and high (|)\n");
  runTwoThreads(tbb::task_arena::priority::normal, tbb::task_arena::priority::high);
  std::printf("\n");
  return 0;
}

void printArrival(tbb::task_arena::priority priority) {
  switch (priority) {
    case tbb::task_arena::priority::low:
      std::printf(".");
      break;
    case tbb::task_arena::priority::normal:
      std::printf(":");
      break;
    case tbb::task_arena::priority::high:
      std::printf("|");
      break;
    default:
      break;
  }
  std::fflush(stdout);
  tbb::tick_count t0 = tbb::tick_count::now();
  while ((tbb::tick_count::now() - t0).seconds() < 0.01);
}
