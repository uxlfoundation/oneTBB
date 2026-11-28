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

#ifndef WAVEFRONT_DEPENDENCY_GRAPH
#define WAVEFRONT_DEPENDENCY_GRAPH


#include <tbb/flow_graph.h>

#include <atomic>
#include <vector>
#include <numeric>
#include <chrono>
#include <iostream>

#include "utils.h"

using Node = tbb::flow::continue_node<tbb::flow::continue_msg>;
using NodePtr = std::shared_ptr<Node>;
using block = std::pair<std::size_t, std::size_t>;

NodePtr createNode(tbb::flow::graph& g, block b, std::size_t block_size, 
                   std::string& A, std::string& B, 
                   std::vector<std::vector<int>>& F) {
    return std::make_shared<Node>(g,
        [b, block_size, &A, &B, &F] (const tbb::flow::continue_msg& msg) {
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

            return msg;
        });
}

void addEdges(std::vector<NodePtr>& nodes, int r, int c, std::size_t block_size, std::size_t num_blocks) {
    NodePtr np = nodes[r * num_blocks + c];
    if (c + 1 < num_blocks && r != c) {
        tbb::flow::make_edge(*np, *nodes[r * num_blocks + c + 1]);
    }
    if (r + 1 < num_blocks) {
        tbb::flow::make_edge(*np, *nodes[(r + 1) * num_blocks + c]);
    }
}


std::pair<std::size_t, std::size_t> measure_dependency_graph(std::string A, std::string B,
                                                           std::size_t ntrial, std::size_t block_size)
{
    std::vector<unsigned long> times;
    std::size_t result;

    for (std::size_t i = 0; i < ntrial; ++i) {
        std::vector<std::vector<int>> F(A.size() + 1, std::vector<int>(B.size() + 1, 0));

        auto t1 = std::chrono::steady_clock::now();

        {
            std::size_t num_blocks = std::max(A.size(), B.size()) / block_size + 1;

            std::vector<NodePtr> nodes(num_blocks * num_blocks);
            tbb::flow::graph g;
            for (int r = num_blocks - 1; r >= 0; --r) {
                for (int c = r; c >= 0; --c) {
                    nodes[r * num_blocks + c] = createNode(g, block{r, c}, block_size, A, B, F);
                    addEdges(nodes, r, c, block_size, num_blocks);
                }
            }
            nodes[0]->try_put(tbb::flow::continue_msg{});
            g.wait_for_all();
        }

        auto t2 = std::chrono::steady_clock::now();

        auto time = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
        times.push_back(time);

        result = F[A.size()][B.size()];
    }

    return {result, std::accumulate(times.begin(), times.end(), 0) / times.size()};
}

#endif /* WAVEFRONT_DEPENDENCY_GRAPH */
