// Throughput cost of head-of-line blocking in tbb::flow::resource_limiter.
//
// Same graph as repro_resource_limiter_head_of_line.cpp, fed by a broadcast_node:
//   genie_node needs {genie}, both_node needs {root, genie}, root_node needs {root}.
//
// A request is ranked by the timestamp taken when its node body starts, and the three
// bodies are independent spawned tasks, so edge order fixes only the spawn order.
// lag_node is what makes both_node's root request reliably older than root_node's, the
// way the sleep between try_puts does in the reproducer.

#define TBB_PREVIEW_FLOW_GRAPH_RESOURCE_LIMITING 1

#include <oneapi/tbb/flow_graph.h>

#include <chrono>
#include <cstdio>

constexpr int genie_work_us = 1000;
constexpr int both_work_us  = 100;
constexpr int root_work_us  = 700;

constexpr int messages = 1000;

// Below the 800 us/message the root limiter needs, so the graph still stays saturated.
constexpr int pause_us = 200;

// Must exceed task pick-up jitter, otherwise the two root requests can swap order.
constexpr int lag_us = 200;

enum mode { burst, paused, single };

void work_us(int us) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::microseconds(us);
    while (std::chrono::steady_clock::now() < deadline) {}
}

long long run(mode m) {
    using namespace oneapi::tbb::flow;

    using node_type = resource_limited_node<int, std::tuple<>>;
    using ports_type = typename node_type::output_ports_type;

    resource_limiter<int> root_limiter{1};
    resource_limiter<int> genie_limiter{2};

    graph g;

    broadcast_node<int> source(g);

    node_type genie_node(g, unlimited, std::tie(genie_limiter),
        [](int, ports_type&, int) { work_us(genie_work_us); });

    node_type both_node(g, unlimited, std::tie(root_limiter, genie_limiter),
        [](int, ports_type&, int, int) { work_us(both_work_us); });

    node_type root_node(g, unlimited, std::tie(root_limiter),
        [](int, ports_type&, int) { work_us(root_work_us); });

    function_node<int, int> lag_node(g, unlimited,
        [](int i) { work_us(lag_us); return i; });

    make_edge(source, genie_node);
    make_edge(source, both_node);
    make_edge(source, lag_node);
    make_edge(lag_node, root_node);

    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < messages; ++i) {
        source.try_put(i);
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

    std::printf("messages: %d, pause: %d us\n\n", messages, pause_us);
    for (int i = 0; i < 3; ++i) {
        long long us = run(modes[i]);
        std::printf("%s : %8lld us total, %7.1f us/message\n",
                    names[i], us, double(us) / messages);
    }
    return 0;
}
