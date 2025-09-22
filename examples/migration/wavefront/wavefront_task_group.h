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

#ifndef WAVEFRONT_TASK_GROUP_H
#define WAVEFRONT_TASK_GROUP_H

#define TBB_PREVIEW_TASK_GROUP_EXTENSIONS 1
#include <tbb/task_group.h>

#include <atomic>
#include <vector>
#include <numeric>
#include <chrono>
#include <iostream>

#include "utils.h"

class cell_computation {
    using block = std::pair<size_t,size_t>;
public:
    cell_computation(block b_, std::string& A_, std::string& B_, std::size_t counters_size_,
                     std::atomic<int>** counters_, std::size_t block_size_,
                     std::vector<std::vector<int>>& F_, tbb::task_group& tg_)
        : b(b_), A{A_}, B{B_}, counters_size(counters_size_)
        , counters{counters_}, block_size{block_size_}, F(F_), tg(tg_)
    {}

    tbb::task_handle operator()() const {
        // Extract bounds on block
        size_t b_i = b.first;
        size_t b_j = b.second;
        size_t x_l = block_size * b_i + 1;
        size_t x_u = std::min(x_l + block_size, A.size() + 1);
        size_t y_l = block_size * b_j + 1;
        size_t y_u = std::min(y_l + block_size, B.size() + 1);

        for (std::size_t i = x_l; i < x_u; ++i) {
            for (std::size_t j = y_l; j < y_u; ++j) {
                if (A[i - 1] == B[j - 1]) {
                    F[i][j] = F[i - 1][j - 1] + 1;
                } else {
                    F[i][j] = std::max(F[i - 1][j], F[i][j - 1]);
                }
            }
        }

        int bypass_east = 0;
        int bypass_south = 0;

        if (try_release_dependant(b_i, b_j + 1)) {
            bypass_east = 1;
        }
        if (try_release_dependant(b_i + 1, b_j)) {
            if (bypass_east == 0) {
                bypass_south = 1;
            } else {
                tg.run(cell_computation(block{b_i + 1, b_j}, A, B, counters_size,
                       counters, block_size, F, tg));
            }
        }

        if (bypass_east > 0 || bypass_south > 0) {
            return tg.defer(cell_computation(block{b_i + bypass_south, b_j+ bypass_east},
                                             A, B, counters_size, counters, block_size, F, tg));
        }

        return tbb::task_handle{};
    }
private:
    bool try_release_dependant(std::size_t i, std::size_t j) const {
        if (i < counters_size && j < counters_size && --counters[i][j] == 0) {
            return true;
        }
        return false;
    }

    block b;
    std::string& A;
    std::string& B;
    std::size_t counters_size;
    std::atomic<int>** counters;
    std::size_t block_size;
    std::vector<std::vector<int>>& F;
    tbb::task_group& tg;
};

std::pair<std::size_t, std::size_t> measure_task_group(std::string A, std::string B,
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

        tbb::task_group tg;
        tg.run_and_wait(cell_computation({0, 0}, A, B, counters_size,
                         counters, block_size, F, tg));

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

#endif /* WAVEFRONT_TASK_GROUP_H */
