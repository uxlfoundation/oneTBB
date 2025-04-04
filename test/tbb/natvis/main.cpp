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