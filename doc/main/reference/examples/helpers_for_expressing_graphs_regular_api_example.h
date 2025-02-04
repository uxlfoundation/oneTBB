#include <oneapi/tbb/flow_graph.h>

int main() {
    using namespace oneapi::tbb::flow;

    graph g;

    broadcast_node<int> input(g);

    function_node doubler(g, unlimited, [](const int& v) { return 2 * v; });
    function_node squarer(g, unlimited, [](const int& v) { return v * v; });
    function_node cuber(g, unlimited, [](const int& v) { return v * v * v; });

    join_node<std::tuple<int, int, int>> join(g);

    int sum = 0;
    function_node summer(g, serial, [&](const std::tuple<int, int, int>& v) {
        int sub_sum = std::get<0>(v) + std::get<1>(v) + std::get<2>(v);
        sum += sub_sum;
        return sub_sum;
    });

    make_edge(input, doubler);
    make_edge(input, squarer);
    make_edge(input, cuber);
    make_edge(doubler, std::get<0>(join.input_ports()));
    make_edge(squarer, std::get<1>(join.input_ports()));
    make_edge(cuber, std::get<2>(join.input_ports()));
    make_edge(join, summer);

    for (int i = 1; i <= 10; ++i) {
        input.try_put(i);
    }
    g.wait_for_all();
}
