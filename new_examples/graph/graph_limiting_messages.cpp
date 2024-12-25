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
#include <any>
#include <atomic>
#include <iostream>
#include <memory>

struct stats {
  std::string s;
  double time;
  int totalAllocations;
  int remainingMsgs;
  int maxConcurrentMsgs;
  int remainingWorkTasks;
  int maxConcurrentWorkTasks;
  static void dump_header();
  void dump();
};
stats make_stats(const std::string& str, const tbb::tick_count& t0);

class Msg {
public:
  virtual ~Msg() = default;
  virtual int get_id() const = 0;
};
using MsgPtr = std::shared_ptr<Msg>;

void resetCounters();
void dumpCounters(const std::string& s);
bool is_msg(int id);
MsgPtr make_msg(int id);
MsgPtr recycle_token_as_msg(const MsgPtr& m, int id);
static void doWork(const std::string& s, const MsgPtr& m);

stats concurrencyAsControl() {
  tbb::tick_count t0 = tbb::tick_count::now();
  tbb::flow::graph g;

  int id = 0;
  tbb::flow::input_node
  input{g, [&id](tbb::flow_control& fc) -> MsgPtr {
      int next_id = id++;
      if (!is_msg(next_id)) {
        fc.stop();
        return nullptr;
      } else {
        return make_msg(id);
      }
    }};
  tbb::flow::function_node<MsgPtr, int, tbb::flow::rejecting>
  limited_to_3_node{g, 3, [](const MsgPtr& m) {
      doWork("L3", m);
      return 0;
    }
  };
  tbb::flow::make_edge(input, limited_to_3_node);

  input.activate();
  g.wait_for_all();
  return make_stats("concurrencyAsControl", t0);
}

stats limiterNodeAsControl() {
  tbb::tick_count t0 = tbb::tick_count::now();
  tbb::flow::graph g;

  int id = 0;
  tbb::flow::input_node
  input{g, [&id](tbb::flow_control& fc) -> MsgPtr {
      int next_id = id++;
      if (!is_msg(next_id)) {
        fc.stop();
        return nullptr;
      } else {
        return make_msg(id);
      }
    }};
  tbb::flow::limiter_node<MsgPtr> limiter{g, 3};
  tbb::flow::function_node unlimited_node_1{g,
                                          tbb::flow::unlimited,
    [] (const MsgPtr& m) {
      doWork("U1", m);
      return m;
    }
  };
  tbb::flow::function_node unlimited_node_2{g,
                                          tbb::flow::unlimited,
    [] (const MsgPtr& m) {
      doWork("U2", m);
      return m;
    }
  };

  tbb::flow::join_node<std::tuple<MsgPtr, MsgPtr>, tbb::flow::key_matching<int>>
  join{ g, [](const MsgPtr& p) { return p->get_id(); },
           [](const MsgPtr& p) { return p->get_id(); }};

  tbb::flow::function_node
  unlimited_node_3{g, tbb::flow::unlimited,
  [] (const std::tuple<MsgPtr, MsgPtr>& m) {
      doWork("U3", std::get<0>(m));
      return tbb::flow::continue_msg{};
  }};

  tbb::flow::make_edge(input, limiter);
  tbb::flow::make_edge(limiter, unlimited_node_1);
  tbb::flow::make_edge(limiter, unlimited_node_2);
  tbb::flow::make_edge(unlimited_node_1, tbb::flow::input_port<0>(join));
  tbb::flow::make_edge(unlimited_node_2, tbb::flow::input_port<1>(join));
  tbb::flow::make_edge(join, unlimited_node_3);
  tbb::flow::make_edge(unlimited_node_3, limiter.decrementer());

  input.activate();
  g.wait_for_all();
  return make_stats("limiterNodeAsControl", t0);
}

stats tokensAsControl() {
  tbb::tick_count t0 = tbb::tick_count::now();

  using token_t = MsgPtr;
  tbb::flow::graph g;

  int id = 0;
  tbb::flow::input_node
  input{g, [&id](tbb::flow_control& fc) {
      int next_id = id++;
      if (!is_msg(next_id)) {
        fc.stop();
        return -1;
      } else {
        return next_id;
      }
    }};

  tbb::flow::buffer_node<MsgPtr> token_buffer{g};
  tbb::flow::join_node<std::tuple<int, token_t>,
                       tbb::flow::reserving> token_join{g};

  tbb::flow::function_node<std::tuple<int,token_t>, MsgPtr, tbb::flow::lightweight>
  reuse_node{g, tbb::flow::unlimited,
  [] (const  std::tuple<int, token_t>& m) -> MsgPtr {
    return recycle_token_as_msg(std::get<1>(m), std::get<0>(m));
  }};

tbb::flow::function_node unlimited_node_1{g,
                                          tbb::flow::unlimited,
    [] (const MsgPtr& m) {
      doWork("U1", m);
      return m;
    }
  };
  tbb::flow::function_node unlimited_node_2{g,
                                          tbb::flow::unlimited,
    [] (const MsgPtr& m) {
      doWork("U2", m);
      return m;
    }
  };

  tbb::flow::join_node<std::tuple<MsgPtr, MsgPtr>, tbb::flow::tag_matching>
  join{ g, [](const MsgPtr& p) -> tbb::flow::tag_value { return p->get_id(); },
           [](const MsgPtr& p) -> tbb::flow::tag_value { return p->get_id(); }};

  tbb::flow::function_node
  unlimited_node_3{g, tbb::flow::unlimited,
  [] (const std::tuple<MsgPtr, MsgPtr>& t) {
      auto m = std::get<0>(t);
      doWork("U3", m);
      return m;
  }};

  tbb::flow::make_edge(input, tbb::flow::input_port<0>(token_join));
  tbb::flow::make_edge(token_buffer, tbb::flow::input_port<1>(token_join));
  tbb::flow::make_edge(token_join, reuse_node);
  tbb::flow::make_edge(reuse_node, unlimited_node_1);
  tbb::flow::make_edge(reuse_node, unlimited_node_2);
  tbb::flow::make_edge(unlimited_node_1, tbb::flow::input_port<0>(join));
  tbb::flow::make_edge(unlimited_node_2, tbb::flow::input_port<1>(join));
  tbb::flow::make_edge(join, unlimited_node_3);
  tbb::flow::make_edge(unlimited_node_3, token_buffer);

  for (int i = 0; i < 3; ++i)
    token_buffer.try_put(make_msg(-1));

  input.activate();
  g.wait_for_all();
  return make_stats("tokenbufferAsControl", t0);
}

static void warmupTBB();

int main(int argc, char *argv[]) {
  warmupTBB();
  std::printf("\n");
  auto c_stats = concurrencyAsControl();
  std::printf("\n");
  auto l_stats = limiterNodeAsControl();
  std::printf("\n");
  auto t_stats = tokensAsControl();
  std::printf("\n");
  stats::dump_header();
  c_stats.dump();
  l_stats.dump();
  t_stats.dump();
  return 0;
}

int numMsgsToSend = 12;
std::atomic<int> trackedMsgAllocations = 0;
std::atomic<int> trackedMsgCount = 0;
std::atomic<int> maxTrackedMsgs = 0;
std::atomic<int> workTaskCount = 0;
std::atomic<int> maxWorkTasks = 0;

stats make_stats(const std::string& str, const tbb::tick_count& t0) {
  tbb::tick_count t1 = tbb::tick_count::now();
  std::printf("\n");
  stats s = { str, (t1-t0).seconds(),
              trackedMsgAllocations,
              trackedMsgCount, maxTrackedMsgs,
              workTaskCount, maxWorkTasks };
  trackedMsgAllocations = 0;
  trackedMsgCount = 0;
  maxTrackedMsgs = 0;
  workTaskCount = 0;
  maxWorkTasks = 0;
  return s;
}

void increment(std::atomic<int>& c, std::atomic<int>& m) {
  int cnt = ++c;
  int maxValue = m;
  while (cnt > maxValue) {
    m.compare_exchange_strong(maxValue, cnt);
  }
}

void decrement(std::atomic<int>& c) { --c; }

class TrackedMsg : public Msg {
   int id;

public:
   TrackedMsg() : id(-1) { }
   TrackedMsg(int i) : id(i) { increment(trackedMsgCount,maxTrackedMsgs); }
   TrackedMsg(const TrackedMsg& b) : id(b.id) { increment(trackedMsgCount,maxTrackedMsgs); }
   ~TrackedMsg() override { decrement(trackedMsgCount); }
   int get_id() const override { return id; }
   int set_id(int i) { return id = i; }
};

bool is_msg(int id) {
  return id < numMsgsToSend;
}

MsgPtr make_msg(int id) {
  ++trackedMsgAllocations;
  return std::make_shared<TrackedMsg>(id);
}

MsgPtr recycle_token_as_msg(const MsgPtr& m, int id) {
  std::dynamic_pointer_cast<TrackedMsg>(m)->set_id(id);
  return m;
}

static void doWork(const std::string& s, const MsgPtr& m) {
  increment(workTaskCount,maxWorkTasks);
  int w = m->get_id()%4;
  std::printf("<%s:%d>", s.c_str(), m->get_id());
  tbb::tick_count t0 = tbb::tick_count::now();
  while ((tbb::tick_count::now() - t0).seconds() < 0.01*w);
  std::printf("</%s:%d>", s.c_str(), m->get_id());
  decrement(workTaskCount);
}

void stats::dump_header() {
  std::printf("\n%-25s %-15s %-15s %-15s %-15s\n",
              "example", "time", "allocs",
              "max Msgs", "max Tasks");
  std::printf("%-25s %-15s %-15s %-15s %-15s\n",
              "-------", "----", "------",
              "--------", "---------");
}

void stats::dump() {
  std::printf("%-25s %-15f %-15d %-15d %-15d\n",
              s.c_str(), time,
              totalAllocations,
              maxConcurrentMsgs,
              maxConcurrentWorkTasks);
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
