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

const int default_P = tbb::info::default_concurrency();
using counter_t = std::atomic<int>;
void waitUntil(int N, counter_t& c);
void noteParticipation(int offset);
void dumpParticipation();

void doWork(int offset, double seconds) {
  noteParticipation(offset);
  tbb::tick_count t0 = tbb::tick_count::now();
  while ((tbb::tick_count::now() - t0).seconds() < seconds);
}

counter_t counter1(0), counter2(0);

void arenaGlobalControlExplicitArena(int p, int offset) {
  tbb::global_control gc(tbb::global_control::max_allowed_parallelism, p);

  // we use waitUntil to force overlap of the gc lifetimes
  waitUntil(2, counter1);
  tbb::task_arena a{2*tbb::info::default_concurrency()};

  a.execute([=]() {
    tbb::parallel_for(0,
                      10*tbb::info::default_concurrency(),
                      [=](int) { doWork(offset, 0.01); });
  });

  // we prevent either gc from being destroyed until both are done
  waitUntil(2, counter2);
}

void runTwoThreads(int p0, int p1) {
  std::thread t0([=]() { arenaGlobalControlExplicitArena(p0, 1); });
  std::thread t1([=]() { arenaGlobalControlExplicitArena(p1, 10000); });
  t0.join();
  t1.join();
}

int main() {
  runTwoThreads(default_P/2, 2*default_P);
  dumpParticipation();
  return 0;
}

#include <atomic>
#include <iostream>
#include <vector>

std::atomic<int> next_tid;
tbb::enumerable_thread_specific<int> my_tid(-1);
std::vector<std::atomic<int>> tid_participation(2*default_P + 1);

void noteParticipation(int inc) {
  auto& t = my_tid.local();
  if (t == -1) {
    t = next_tid++;
  }
  tid_participation[t] += inc;
}

void clearParticipation() {
  next_tid = 0;
  my_tid.clear();
  for (auto& p : tid_participation)
    p = 0;
}

void dumpParticipation() {
  int sum = tid_participation[0];
  std::cout << "[" << tid_participation[0];
  for (int i = 1; i < tid_participation.size(); ++i) {
    sum += tid_participation[i];
    std::cout << ", " << tid_participation[i];
  }
  std::cout << "]\n"
            << "sum == " << sum << "\n"
            << "expected sum == " << 10*default_P + 10*default_P*10000 << "\n";
  clearParticipation();
  counter1 = 0; counter2 = 0;
}

void waitUntil(int N, counter_t& c) {
  ++c;
  while (c != N);
}
