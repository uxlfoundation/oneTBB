import tbb;
#include <iostream>

int main() {
    std::cout << TBB_runtime_interface_version() << std::endl;
    std::cout << TBB_runtime_version() << std::endl;

    tbb::parallel_for(tbb::blocked_range<int>{0, 20},
        [](const tbb::blocked_range<int>& range) {
            for (int i = range.begin(); i < range.end(); ++i) {
                std::cout << "Body\n";
            }
        });

    oneapi::tbb::blocked_range<int> range{0, 100};
        // }, tbb::auto_partitioner{}); // Fails to compile

    // tbb::detail::d1::parallel_for(0, 1, [](int) { std::cout << "1\n"; }); // Fails to compile
}
