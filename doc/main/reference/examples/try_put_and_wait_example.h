#define TBB_PREVIEW_FLOW_GRAPH_TRY_PUT_AND_WAIT 1
#include <oneapi/tbb/flow_graph.h>
#include <oneapi/tbb/parallel_for.h>

struct f1_body;
struct f2_body;
struct f3_body;
struct f4_body;

int main() {
    using namespace oneapi::tbb;

    flow::graph g;
    flow::broadcast_node<int> start_node(g);

    flow::function_node<int, int> f1(g, flow::unlimited, f1_body{});
    flow::function_node<int, int> f2(g, flow::unlimited, f2_body{});
    flow::function_node<int, int> f3(g, flow::unlimited, f3_body{});

    flow::join_node<std::tuple<int, int>> join(g);

    flow::function_node<std::tuple<int, int>, int> f4(g, flow::serial, f4_body{});

    flow::make_edge(start_node, f1);
    flow::make_edge(f1, f2);

    flow::make_edge(start_node, f3);

    flow::make_edge(f2, flow::input_port<0>(join));
    flow::make_edge(f3, flow::input_port<1>(join));

    flow::make_edge(join, f4);

    // Submit work into the graph
    parallel_for(0, 100, [&](int input) {
        start_node.try_put_and_wait(input);

        // Post processing the result of input
    });
}
