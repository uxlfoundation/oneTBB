/*
    Copyright (C) 2023 Intel Corporation

    This software and the related documents are Intel copyrighted materials, and your use of them is
    governed by the express license under which they were provided to you ("License"). Unless the
    License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
    transmit this software or the related documents without Intel's prior written permission.

    This software and the related documents are provided as is, with no express or implied
    warranties, other than those that are expressly stated in the License.
*/

#include "common/utils.h"
#include "common/executors.h"

#include <vector>

template <typename UpperRange, typename UpperPartition, typename C0, typename C1>
int run_composition(duration_logger& stat, std::size_t data_size, const C0& client0, C1& client1) {
    std::string chain_name = client0.name() + "-" + client1.name();
    stat.concurrency_chain = std::to_string(client0.max_concurrency()) + "-" + std::to_string(client1.max_concurrency());

    const int repetitions = 100;
    stat.iterations = repetitions;

    std::vector<float> numbers(data_size, 1);
    stat.start();

    for (int i = 0; i < repetitions; ++i) {
        client0.bulk_execute(UpperRange{0, static_cast<int>(data_size)}, [&client1, &numbers](auto& range) {
            client1.bulk_execute(range.begin(), range.end(), [&numbers] (int i ) {
                numbers[i] = numbers[i] * numbers[i];
            });
        }, UpperPartition{});
    }

    stat.stop();

    int check = check_sequence(numbers, data_size, /*expected*/1, "sequence 1");
    if (!check) {
        stat.dump(chain_name);
    }

    return check;
}

int main(int argc, char** argv) {
    argument_parser parser{argc, argv};
    
    std::vector<std::size_t> sizes{10'000'000, 100'000'000, 1'000'000'000};
    if (parser.work_sizes.empty() == false) {
        sizes = parser.work_sizes;
    }

    const std::size_t hw_concurrency = std::thread::hardware_concurrency();
    std::size_t small_p = hw_concurrency / 4;
    if (small_p < 1) {
        small_p = 1;
    } else if (small_p > 4) {
        small_p = 4;
    }

    int result = 0;
    duration_logger statistics{"perfect_nested", hw_concurrency, parser.print_to_stdout};
    statistics.max_concurrency = hw_concurrency;

    for (auto data_size : sizes) {
        statistics.data_size = data_size;

        tcm::serial_client serial_client{ };
        tcm::tbb_client tbb_client_outer{ small_p };
        tcm::tbb_client tbb_client_inner{ hw_concurrency };
        tcm::omp_client omp_client{ hw_concurrency / small_p };
        result |= run_composition<tbb::blocked_range<int>, tbb::static_partitioner>(statistics, data_size, tbb_client_outer, omp_client);
        result |= run_composition<tbb::blocked_range<int>, tbb::static_partitioner>(statistics, data_size, tbb_client_outer, serial_client);
        result |= run_composition<tbb::blocked_range<int>, tbb::static_partitioner>(statistics, data_size, tbb_client_outer, tbb_client_inner);
    }
    return result;
}
