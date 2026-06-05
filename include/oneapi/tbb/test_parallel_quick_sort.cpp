#include "parallel_partition.h"
#include "tick_count.h"
#include <vector>
#include <random>
#include <algorithm>
#include <iostream>

int main() {
    constexpr std::size_t n = 10000000;
    std::vector<int> v(n);

    std::mt19937 rng(0);
    std::uniform_int_distribution<int> dist(0, 100);

    std::generate(v.begin(), v.end(), [&] { return dist(rng); });

    using namespace tbb::detail::d1;

    std::vector<int> v_copy = v;

    tbb::tick_count start_parallel = tbb::tick_count::now();

    parallel_qsort(v.begin(), v.end(), std::less<int>{});

    tbb::tick_count finish_parallel = tbb::tick_count::now();

    std::sort(v.begin(), v.end(), std::less<int>{});

    tbb::tick_count finish_serial = tbb::tick_count::now();
    
    std::cout << "Elapsed time (parallel): " << (finish_parallel - start_parallel).seconds() << std::endl;
    std::cout << "Elapsed time (serial) " << (finish_serial - finish_parallel).seconds() << std::endl;

    std::cout << std::is_sorted(v.begin(), v.end()) << std::endl;
}
