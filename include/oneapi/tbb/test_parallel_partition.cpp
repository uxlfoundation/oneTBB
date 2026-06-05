#include "tick_count.h"
#include "parallel_partition.h"
#include <vector>
#include <iostream>
#include <random>
#include <algorithm>

int main() {
    constexpr std::size_t n = 100000000;
    std::vector<int> v(n);

    std::mt19937 rng(0);
    std::uniform_int_distribution<int> dist(0, 100);

    std::generate(v.begin(), v.end(), [&] { return dist(rng); });

    // for (int i : v) {
    //     std::cout << i << " ";
    // }
    // std::cout << std::endl;

    auto comp_with_pivot = [&v](int x) { return x < v.front(); };

    using namespace tbb::detail::d1;

    std::vector<int> v_copy = v;

    oneapi::tbb::tick_count start_parallel = oneapi::tbb::tick_count::now();

    auto it1 = parallel_partition(v.begin(), v.end(), std::less<int>{});

    oneapi::tbb::tick_count finish_parallel = oneapi::tbb::tick_count::now();

    auto it2 = std::partition(std::next(v.begin()), v.end(), comp_with_pivot);

    oneapi::tbb::tick_count finish_serial = oneapi::tbb::tick_count::now();

    std::cout << "Elapsed time (parallel): " << (finish_parallel - start_parallel).seconds() << std::endl;
    std::cout << "Elapsed time (serial) " << (finish_serial - finish_parallel).seconds() << std::endl;

    std::cout << "Validation" << std::endl;

    std::cout << "Partition index check ";

    if ((it1 - v.begin()) == (it2 - v_copy.begin())) {
        std::cout << "passed" << std::endl;
    } else {
        std::cout << "failed" << std::endl;
        std::cout << (it1 - v.begin()) << " " << (it2 - v_copy.begin()) << std::endl;
    }

    std::cout << "Partition value check ";
    if (*it1 == *it2) {
        std::cout << "passed" << std::endl;
    } else {
        std::cout << "failed" << std::endl;
    }

    std::cout << "is_partitioned check ";
    if (std::is_partitioned(std::next(v.begin()), v.end(), comp_with_pivot)) {
        std::cout << "passed" << std::endl;
    } else {
        std::cout << "failed" << std::endl;
    }
}