#define TBB_PREVIEW_FLOW_GRAPH_FEATURES 1
#include <oneapi/tbb/flow_graph.h>

int main() {
    using namespace oneapi::tbb::flow;

    graph g;
    broadcast_node<int> input(g);

    function_node doubler(g, unlimited, [](const int& i) { return 2 * i; });
    function_node squarer(g, unlimited, [](const int& i) { return i * i; });
    function_node cuber(g, unlimited, [](const int& i) { return i * i * i; });

    buffer_node<int> buffer(g);

    auto handlers = make_node_set(doubler, squarer, cuber);
    make_edges(input, handlers);
    make_edges(handlers, buffer);

    for (int i = 1; i <= 10; ++i) {
        input.try_put(i);
    }
    g.wait_for_all();
}
