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

#ifndef WAVEFRONT_PARALLEL_FOR_EACH
#define WAVEFRONT_PARALLEL_FOR_EACH


#include <tbb/parallel_for_each.h>

#include <atomic>
#include <vector>
#include <numeric>
#include <chrono>
#include <iostream>

#include "utils.h"


std::pair<std::size_t, std::size_t> measure_parallel_for_each(std::string A, std::string B,
                                                           std::size_t ntrial, std::size_t block_size)
{
    std::vector<unsigned long> times;
    std::size_t result;

    for (std::size_t i = 0; i < ntrial; ++i) {
        std::vector<std::vector<int>> F(A.size() + 1, std::vector<int>(B.size() + 1, 0));

        auto t1 = std::chrono::steady_clock::now();

        std::size_t counters_size = std::max(A.size(), B.size()) / block_size + 1;
        std::atomic<int>** counters = new std::atomic<int>*[counters_size];

        for (std::size_t k = 0; k < counters_size; ++k) {
            counters[k] = new std::atomic<int>[counters_size]{};
        }

        for (int k = 0; k < counters_size; ++k) {
            for (int l = 0; l < counters_size; ++l) {
                if (k == 1 || l == 1) {
                    counters[k][l].store(1, std::memory_order_relaxed);
                } else {
                    counters[k][l].store(2, std::memory_order_relaxed);
                }
            }

        }

        using block = std::pair<size_t,size_t>;
        block origin(0, 0);
        oneapi::tbb::parallel_for_each(&origin, &origin + 1, [&] (const block& b, oneapi::tbb::feeder<block>& feeder) {
            // Extract bounds on block
            size_t b_i = b.first;
            size_t b_j = b.second;
            size_t x_l = block_size * b_i + 1;
            size_t x_u = std::min(x_l + block_size, A.size() + 1);
            size_t y_l = block_size * b_j + 1;
            size_t y_u = std::min(y_l + block_size, B.size() + 1);

            // Process the block
            for (std::size_t i = x_l; i < x_u; ++i) {
                for (std::size_t j = y_l; j < y_u; ++j) {
                    if (A[i - 1] == B[j - 1]) {
                        F[i][j] = F[i - 1][j - 1] + 1;
                    } else {
                        F[i][j] = std::max(F[i - 1][j], F[i][j - 1]);
                    }
                }
            }

            if (b_j + 1 < counters_size && --counters[b_i][b_j + 1] == 0) {
                feeder.add(block(b_i, b_j + 1));
            }
            if (b_i + 1 < counters_size && --counters[b_i + 1][b_j] == 0) {
                feeder.add(block(b_i + 1, b_j));
            }
        });

        for (std::size_t k = 0; k < counters_size; ++k) {
            delete[] counters[k];
        }

        delete[] counters;

        auto t2 = std::chrono::steady_clock::now();

        auto time = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
        times.push_back(time);

        result = F[A.size()][B.size()];
    }

    return {result, std::accumulate(times.begin(), times.end(), 0) / times.size()};
}

#endif /* WAVEFRONT_PARALLEL_FOR_EACH */
