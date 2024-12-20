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
#include <tbb/tbb.h>

void f();

void serializedCallsToRun(tbb::task_group& g,
                          int depth) {
  int n = 1<<depth;
  for (int i = 1; i < n; ++i) {
    g.run([]() { f(); });
  }
}

void serialCalls(int depth) {
  tbb::task_group g;
  serializedCallsToRun(g, depth);
  g.wait();
}

void treeCallsToRun(tbb::task_group& g, int depth) {
  if (depth>1) {
    g.run([&g, depth]() {
      treeCallsToRun(g, depth-1);
    });
    g.run([&, depth]() {
      treeCallsToRun(g, depth-1);
    });
  }
  f();
}

void treeCalls(int depth) {
  tbb::task_group g;
  treeCallsToRun(g, depth);
  g.wait();
}

std::atomic<int> count(0);
static void warmupTBB();

int main() {
  std::printf("serial\n");
  count = 0;
  warmupTBB();
  auto ts0 = tbb::tick_count::now();
  serialCalls(20);
  auto ts1 = tbb::tick_count::now();
  int serial_count = count;
  double ts_sec = (ts1-ts0).seconds();
  std::cout << "Serial count=" << serial_count << " in " << ts_sec << " s\n";

  std::printf("tree\n");
  count = 0;
  warmupTBB();
  auto tt0 = tbb::tick_count::now();
  treeCalls(20);
  auto tt1 = tbb::tick_count::now();
  int tree_count = count;

  double tt_sec = (tt1-tt0).seconds();
  std::cout << "Tree count=" << tree_count << " in " << tt_sec << " s\n";

  std::cout << "Serial / Tree = " << ts_sec/tt_sec << "\n";
  return 0;
}

void f() {
  ++count;
}

static void warmupTBB() {
  // This is a simple loop that should get workers started.
  // oneTBB creates workers lazily on first use of the library
  // so this hides the startup time when looking at trivial
  // examples that do little real work.
  tbb::parallel_for(0, tbb::info::default_concurrency(),
   [=](int) {
      tbb::tick_count t0 = tbb::tick_count::now();
      while ((tbb::tick_count::now() - t0).seconds() < 0.01);
    }
  );
}
