/*
    Copyright (c) 2025 Intel Corporation

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

#include <iostream>
#include "oneapi/tbb/task_arena.h"
#include "oneapi/tbb/parallel_for.h"
#include "oneapi/tbb/concurrent_hash_map.h"
#include "oneapi/tbb/concurrent_vector.h"
#include "oneapi/tbb/enumerable_thread_specific.h"

int main() {
    ////tbb::concurrent_hash_map<int, int> hashMap;
    tbb::concurrent_vector<float> concVec;
    tbb::enumerable_thread_specific<float> tls(0);
    std::vector<int> dataStream(5);
    // Use tbb::parallel_for to insert data into the hash map concurrently
    tbb::parallel_for(tbb::blocked_range<size_t>(0, dataStream.size()),
        [&](const tbb::blocked_range<size_t>& range) {
            for (size_t i = range.begin(); i != range.end(); ++i) {
                concVec.push_back(dataStream[i]);
                tls.local() += dataStream[i];
            }
        }
    );
    // Combine the results from all threads
    float total = tls.combine(std::plus<float>());

    // Print the result
    std::cout << "Total: " << total << std::endl;
	return 0;
}