// C++17
#include <oneapi/tbb/parallel_reduce.h>
#include <oneapi/tbb/blocked_range.h>
#include <vector>
#include <set>

int main() {
    std::vector<std::set<int>> sets;

    oneapi::tbb::parallel_reduce(oneapi::tbb::blocked_range<size_t>(0, sets.size()),
                                    std::set<int>{}, // identity element - empty set
                                    [&](const oneapi::tbb::blocked_range<size_t>& range, std::set<int>&& value) {
                                        for (size_t i = range.begin(); i < range.end(); ++i) {
                                            // Having value as a non-const rvalue reference allows to efficiently
                                            // transfer nodes from sets[i] without copying/moving the data
                                            value.merge(std::move(sets[i]));
                                        }
                                        return value;
                                    },
                                    [&](std::set<int>&& x, std::set<int>&& y) {
                                        x.merge(std::move(y));
                                        return x;
                                    }
                                    );
}
