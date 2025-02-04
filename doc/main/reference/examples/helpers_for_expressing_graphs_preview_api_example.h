#define TBB_PREVIEW_FLOW_GRAPH_FEATURES 1
#include <oneapi/tbb/flow_graph.h>

int main() {
    using namespace oneapi::tbb::flow;

    graph g;

    function_node doubler(g, unlimited, [](const int& v) { return 2 * v; });
    function_node squarer(g, unlimited, [](const int& v) { return v * v; });
    function_node cuber(g, unlimited, [](const int& v) { return v * v * v; });

    auto handlers = make_node_set(doubler, squarer, cuber);

    broadcast_node input(precedes(handlers));
    join_node join(follows(handlers));

    int sum = 0;
    function_node summer(follows(join), serial,
                            [&](const std::tuple<int, int, int>& v) {
                                int sub_sum = std::get<0>(v) + std::get<1>(v) + std::get<2>(v);
                                sum += sub_sum;
                                return sub_sum;
                            });

    for (int i = 1; i <= 10; ++i) {
        input.try_put(i);
    }
    g.wait_for_all();
}
