// Throughput cost of head-of-line blocking in tbb::flow::resource_limiter.
//
// Same graph as repro_resource_limiter_head_of_line.cpp:
//   genie_node needs {genie}, both_node needs {root, genie}, root_node needs {root}.
//
// A request is ranked by the timestamp taken when its node body starts, so each message is
// fed the way repro_resource_limiter_head_of_line.cpp does it: wait until genie is provably
// held, then put to both_node, then lag before putting to root_node.

#define TBB_PREVIEW_FLOW_GRAPH_RESOURCE_LIMITING 1

#include <oneapi/tbb/flow_graph.h>

#include <atomic>
#include <chrono>
#include <cstdio>

constexpr int genie_work_us = 1000;
constexpr int both_work_us  = 100;
constexpr int root_work_us  = 700;

constexpr int messages = 1000;

// Below the 800 us/message the root limiter needs, so the graph still stays saturated.
constexpr int pause_us = 200;

constexpr int lag_us = 200;

enum mode { burst, paused, single };

void work_us(int us) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::microseconds(us);
    while (std::chrono::steady_clock::now() < deadline) {}
}

std::atomic<int> genie_started{0};

long long run(mode m) {
    using namespace oneapi::tbb::flow;

    using node_type = resource_limited_node<int, std::tuple<>>;
    using ports_type = typename node_type::output_ports_type;

    resource_limiter<int> root_limiter{1};
    resource_limiter<int> genie_limiter{2};

    graph g;

    node_type genie_node(g, unlimited, std::tie(genie_limiter),
        [](int, ports_type&, int) {
            genie_started.fetch_add(1);
            work_us(genie_work_us);
        });

    node_type both_node(g, unlimited, std::tie(root_limiter, genie_limiter),
        [](int, ports_type&, int, int) { work_us(both_work_us); });

    node_type root_node(g, unlimited, std::tie(root_limiter),
        [](int, ports_type&, int) { work_us(root_work_us); });

    genie_started.store(0);
    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < messages; ++i) {
        genie_node.try_put(i);
        while (genie_started.load() <= i) {}
        both_node.try_put(i);
        work_us(lag_us);
        root_node.try_put(i);
        if (m == paused) work_us(pause_us);
        if (m == single) g.wait_for_all();
    }
    g.wait_for_all();

    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now() - start).count();
}

int main() {
    const char* names[] = {"burst ", "paused", "single"};
    const mode modes[]  = {burst, paused, single};

    std::printf("messages: %d, pause: %d us, lag: %d us\n\n", messages, pause_us, lag_us);
    for (int i = 0; i < 3; ++i) {
        long long us = run(modes[i]);
        std::printf("%s : %8lld us total, %7.1f us/message\n",
                    names[i], us, double(us) / messages);
    }
    return 0;
}
