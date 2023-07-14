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
#include "common/concurrent.h"

#include "tbb/info.h"

#include <vector>
#include <stdexcept>

auto get_numa_nodes() {
    return tbb::info::numa_nodes();
}

std::size_t get_numa_node_concurrency(tbb::numa_node_id numa_id) {
    return tbb::info::default_concurrency(numa_id);
}

template <typename Client, typename Concurrency>
int run_composition(duration_logger& stat, std::size_t data_size, 
    std::size_t numa_nodes_num, const std::vector<Concurrency>& concurrencies) 
{
    if (data_size < numa_nodes_num) {
        throw
            std::invalid_argument {"Data size must be larger than number of NUMA domains."};
    }

    const int repetitions = 1;
    stat.iterations = repetitions;

    std::vector<float> numbers(data_size, 1);

    stat.start();

    for (int iter = 0; iter < repetitions; ++iter) {
        Client numa_client{ numa_nodes_num };
        numa_client.bulk_execute(0, static_cast<int>(numa_nodes_num), [&] (int i) {
            int start = i * (data_size / numa_nodes_num);
            int end = start + (data_size / numa_nodes_num);
            if (i == static_cast<int>(numa_nodes_num)-1) {
                end += data_size % numa_nodes_num;
            }

            Client inner_client{ concurrencies[i] };
            inner_client.bulk_execute(start, end, [&numbers] (int i) {
                numbers[i] = numbers[i] * numbers[i];
            });
        });
    }

    stat.stop();

    int check = check_sequence(numbers, data_size, /*expected*/1, "sequence 1");
    if (!check) {
        stat.dump(Client::name());
    }

    return check;
}


int run_numa_composition(duration_logger& stat, std::size_t data_size) {
    auto numa_nodes = get_numa_nodes();
    if (numa_nodes[0] == tbb::task_arena::automatic) {
        std::cerr << "oneTBB info namespace feature is unavailable. Make sure TBBBind is available" << std::endl;
        return 1;
    }

    std::vector<std::size_t> numa_constraints_omp(numa_nodes.size());
    std::vector<tbb::task_arena::constraints> numa_constraints_tbb(numa_nodes.size());

    for (std::size_t i = 0; i < numa_nodes.size(); ++i) {
        numa_constraints_omp[i] = get_numa_node_concurrency(numa_nodes[i]);
        numa_constraints_tbb[i] = tbb::task_arena::constraints{numa_nodes[i]};
    }

    int result =
       run_composition<tcm::omp_client>(stat, data_size, numa_nodes.size(), numa_constraints_omp)
    || run_composition<tcm::tbb_client>(stat, data_size, numa_nodes.size(), numa_constraints_tbb);

    return result;
}

int main(int argc, char** argv) {
    argument_parser parser{argc, argv};
    
    std::vector<std::size_t> sizes{10'000'000, 100'000'000, 1'000'000'000};
    if (parser.work_sizes.empty() == false) {
        sizes = parser.work_sizes;
    }

    const std::size_t hw_concurrency = std::thread::hardware_concurrency();

    duration_logger statistics{"constrained", hw_concurrency, parser.print_to_stdout};
    statistics.max_concurrency = hw_concurrency;

    int result = 0;
    for (auto data_size : sizes) {
        statistics.data_size = data_size;
        result += run_numa_composition(statistics, data_size);
    }
    return result;
}
