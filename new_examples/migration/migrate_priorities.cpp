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

#include <tbb/tbb.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>

void doWork(double sec);
void taskFunction(const std::string& msg, int id);
static void warmupTBB();
void waitUntil(int N);

#if TBB_VERSION_MAJOR > 2020
auto P = tbb::info::default_concurrency();

void enqueueSeveralTasks(int num_iterations) {
  std::printf("Using oneTBB\n");

  tbb::task_arena low_arena{P, 0, tbb::task_arena::priority::low};
  tbb::task_arena normal_arena{P, 0, tbb::task_arena::priority::normal};
  tbb::task_arena high_arena{P, 0, tbb::task_arena::priority::high};

  for (int i = 0; i < num_iterations; ++i) {
    low_arena.enqueue([i]() { taskFunction("L", i); });
    normal_arena.enqueue([i]() { taskFunction("N", i); });
    high_arena.enqueue([i]() { taskFunction("H", i); });
  }
  doWork(1.0);
}

void runParallelForWithHighPriority() {
  std::printf("Using oneTBB\n");

  std::thread t0([]() {
    waitUntil(2);
    tbb::task_arena normal_arena{P, 0, tbb::task_arena::priority::normal};
    normal_arena.execute([]() {
      tbb::parallel_for(0, 10, [](int i) {
        std::printf("N");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      });
    });
  });
  std::thread t1([]() {
    waitUntil(2);
    tbb::task_arena high_arena{P, 0, tbb::task_arena::priority::high};
    high_arena.execute([]() {
      tbb::parallel_for(0, 10, [](int i) { std::printf("H"); });
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    });
  });
  t0.join();
  t1.join();
  std::printf("\n");
}
#else
#warning Using tbb::task directly
auto P = tbb::task_scheduler_init::default_num_threads();

class MyTask : public tbb::task {
public:

  MyTask(const char *m, int i) : msg(m), messageId(i) { }

  tbb::task *execute() override {
    taskFunction(msg, messageId);
    return NULL;
  }

private:
  std::string msg;
  int messageId;
};

void enqueueSeveralTasks(int num_iterations) {
  std::printf("Using older TBB\n");

  for (int i = 0; i < num_iterations; ++i) {
    tbb::task::enqueue(*new( tbb::task::allocate_root() )
                         MyTask( "L", i ), tbb::priority_low);
    tbb::task::enqueue(*new( tbb::task::allocate_root() )
                         MyTask( "N", i ), tbb::priority_normal);
    tbb::task::enqueue(*new( tbb::task::allocate_root() )
                         MyTask( "H", i ), tbb::priority_high);
  }
  doWork(1.0);
}

void runParallelForWithHighPriority() {
  std::printf("Using older TBB\n");

  std::thread t0([]() {
    waitUntil(2);
    tbb::task_group_context tgc;
    tgc.set_priority(tbb::priority_normal);
    tbb::parallel_for(0, 10,
                      [](int i) {
                          std::printf("N");
                          std::this_thread::sleep_for(std::chrono::milliseconds(10));
                      }, tgc);
  });
  std::thread t1([]() {
    waitUntil(2);
    tbb::task_group_context tgc;
    tgc.set_priority(tbb::priority_high);
    tbb::parallel_for(0, 10,
                      [](int i) {
                          std::printf("H");
                          std::this_thread::sleep_for(std::chrono::milliseconds(10));
                      }, tgc);
  });
  t0.join();
  t1.join();
  std::printf("\n");
}
#endif

void doWork(double sec) {
  tbb::tick_count t0 = tbb::tick_count::now();
  while ((tbb::tick_count::now() - t0).seconds() < sec);
}

#include <sstream>

tbb::enumerable_thread_specific<std::stringstream> sout;

void printTrace() {
  for (auto& s : sout) {
    std::cout << s.str() << std::endl;
  }
}

void taskFunction(const std::string& msg, int id) {
  sout.local() << msg.c_str() << ":" << id << " ";
  doWork(0.01);
}

std::atomic<int> count_up(0);

void waitUntil(int N) {
  ++count_up;
  while (count_up != N);
}

int main() {
  warmupTBB();
  enqueueSeveralTasks(10*P);
  printTrace();
  runParallelForWithHighPriority();
  return 0;
}

static void warmupTBB() {
  // This is a simple loop that should get workers started.
  // oneTBB creates workers lazily on first use of the library
  // so this hides the startup time when looking at trivial
  // examples that do little real work.
  tbb::parallel_for(0, P,
   [=](int) {
      tbb::tick_count t0 = tbb::tick_count::now();
      while ((tbb::tick_count::now() - t0).seconds() < 0.01);
    }
  );
}
