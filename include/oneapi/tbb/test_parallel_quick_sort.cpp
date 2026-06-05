#include "parallel_partition.h"
#include "tick_count.h"
#include "parallel_sort.h"
#include <vector>
#include <random>
#include <algorithm>
#include <iostream>
#include <limits>

#if TEST_DPL
#include <oneapi/dpl/algorithm>
#include <oneapi/dpl/execution>
#endif

int main() {
    constexpr std::size_t n = 10000000;
    std::vector<int> v(n);

    std::mt19937 rng(0);
    std::uniform_int_distribution<int> dist(0, std::numeric_limits<int>::max());

    std::generate(v.begin(), v.end(), [&] { return dist(rng); });

    using namespace tbb::detail::d1;

    std::vector<int> v_copy = v;
    std::vector<int> v_copy2 = v;
#if TEST_DPL
    std::vector<int> v_copy3 = v;
#endif

    tbb::tick_count start_parallel = tbb::tick_count::now();

    parallel_qsort(v.begin(), v.end(), std::less<int>{});

    tbb::tick_count finish_parallel = tbb::tick_count::now();

    tbb::tick_count start_serial = tbb::tick_count::now();

    std::sort(v_copy.begin(), v_copy.end(), std::less<int>{});

    tbb::tick_count finish_serial = tbb::tick_count::now();

    tbb::tick_count start_psort = tbb::tick_count::now();

    tbb::parallel_sort(v_copy2.begin(), v_copy2.end(), std::less<int>{});

    tbb::tick_count finish_psort = tbb::tick_count::now();

#if TEST_DPL
    tbb::tick_count start_dpl = tbb::tick_count::now();

    oneapi::dpl::sort(oneapi::dpl::execution::par, v_copy2.begin(), v_copy2.end(), std::less<int>{});

    tbb::tick_count finish_dpl = tbb::tick_count::now();
#endif

    std::cout << "Elapsed time (parallel): " << (finish_parallel - start_parallel).seconds() << std::endl;
    std::cout << "Elapsed time (TBB): " << (finish_psort - start_psort).seconds() << std::endl;
#if TEST_DPL
    std::cout << "Elapsed time (DPL): " << (finish_dpl - start_dpl).seconds() << std::endl;
#endif
    std::cout << "Elapsed time (serial) " << (finish_serial - start_serial).seconds() << std::endl;

    std::cout << "parallel sorted: " << std::is_sorted(v.begin(), v.end()) << std::endl;
    std::cout << "matches serial:  " << (v == v_copy) << std::endl;
}
