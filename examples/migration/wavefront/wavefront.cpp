/*
    Copyright (c) 2024 Intel Corporation

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include "wavefront_task_group.h"
#include "wavefront_parallel_for_each.h"
#include "wavefront_graph.h"
#include "common/utility/utility.hpp"
#include "utils.h"

#include <iostream>
#include <atomic>
#include <vector>
#include <chrono>

std::pair<std::size_t, std::size_t>  measure_single_thread(std::string A, std::string B,
                                                           std::size_t ntrial)
{
    std::vector<std::size_t> times;

    std::size_t result;
    for (std::size_t i = 0; i < ntrial; ++i) {
        std::vector<std::vector<int>> F(A.size() + 1, std::vector<int>(B.size() + 1, 0));

        auto t1 = std::chrono::steady_clock::now();
        for (std::size_t i = 1; i <= A.size(); ++i) {
            for (std::size_t j = 1; j <= B.size(); ++j) {
                if (A[i - 1] == B[j - 1]) {
                    F[i][j] = F[i - 1][j - 1] + 1;
                } else {
                    F[i][j] = std::max(F[i - 1][j], F[i][j - 1]);
                }
            }
        }
        auto t2 = std::chrono::steady_clock::now();

        auto time = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
        times.push_back(time);
        result = F[A.size()][B.size()];
    }

    return {result, std::accumulate(times.begin(), times.end(), 0) / times.size()};
}

int main (int argc, char **argv) {
    std::size_t length = 5000;
    std::size_t ntrial = 20;
    std::size_t block_size = 32;

    utility::parse_cli_arguments(
        argc,
        argv,
        utility::cli_argument_pack()
            //"-h" option for displaying help is present implicitly
            .positional_arg(length, "lenght", "length of the strings")
            .positional_arg(ntrial, "ntrial", "number of times to run each algorithm")
            .positional_arg(block_size, "block_size", "size of the blocks that\
                                        will be processed in parallel"));

    std::string A = generate_random_string(length);
    std::string B = generate_random_string(length);


    std::vector<double> a_ser_ref;
    auto serial_result = measure_single_thread(A, B, ntrial);
    std::cout << "CLS serial avg time = "
              << serial_result.second
              << " ms" << std::endl;

    auto task_group_result = measure_task_group(A, B, ntrial, block_size);
    std::cout << "CLS task_group, time = "
            << task_group_result.second
            << " ms" << std::endl;

    if (serial_result.first != task_group_result.first) {
        std::cerr << "Parallel computation with task_group failed!" << std::endl;
    }

    auto p_for_each_result = measure_parallel_for_each(A, B, ntrial, block_size);
    std::cout << "CLS parallel_for_each, time = "
            << p_for_each_result.second
            << " ms" << std::endl;

    if (serial_result.first != p_for_each_result.first) {
        std::cerr << "Parallel computation with parallel_for_each failed!" << std::endl;
    }

    auto dependency_graph_result = measure_dependency_graph(A, B, ntrial, block_size);
    std::cout << "CLS dependency graph, time = "
            << dependency_graph_result.second
            << " ms" << std::endl;

    if (serial_result.first != dependency_graph_result.first) {
        std::cerr << "Parallel computation with dependency graph failed!" << std::endl;
    }

  return 0;
}
