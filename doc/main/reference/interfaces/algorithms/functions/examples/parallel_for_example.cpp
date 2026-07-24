/*
    Copyright (c) 2026 UXL Foundation Contributors

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

/*begin_parallel_for_average_example*/

#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/blocked_range.h>
#include <vector>

struct Average {
    const std::vector<float>& input;
    std::vector<float>&       output;

    void operator()(const tbb::blocked_range<int>& range) const {
        for (int i = range.begin(); i < range.end(); ++i) {
            output[i] = (input[i - 1] + input[i] + input[i + 1]) * (1/3.f);
        }
    }
};

void ParallelAverage(const std::vector<float>& input, std::vector<float>& output) {
    tbb::parallel_for(tbb::blocked_range<int>{1, n}, Average{input, output});
}
/*end_parallel_for_average_example*/

/*begin_parallel_for_merge_example*/
#include <oneapi/tbb/parallel_for.h>
#include <algorithm>

template <typename Iterator>
struct ParallelMergeRange {
    static constexpr std::size_t grainsize = 1000;

    Iterator begin1, end1; // [begin1, end1) is the 1st sequence to be merged
    Iterator begin2, end2; // [begin2, end2) is the 2nd sequence to be merged
    Iterator out;

    bool empty() const {
        return (end1 - begin1) + (end2 - begin2) == 0;
    }
    
    bool is_divisible() const {
        return std::min(end1 - begin1, end2 - begin2) > grainsize;
    }

    ParallelMergeRange(ParallelMergeRange& r, tbb::split) {
        if (r.end1 - r.begin1 < r.end2 - r.begin2) {
            using std::swap;
            swap(r.begin1, r.begin2);
            swap(r.end1, r.end2);
        }

        Iterator m1 = r.begin1 + (r.end1 - r.begin1) / 2;
        Iterator m2 = std::lower_bound(r.begin2, r.end2, *m1);

        begin1 = m1;
        begin2 = m2;
        end1 = r.end1;
        end2 = r.end2;
        out = r.out + (m1 - r.begin1) + (m2 - r.begin2);
        r.end1 = m1;
        r.end2 = m2;
    }

    ParallelMergeRange(Iterator b1, Iterator e1,
                       Iterator b2, Iterator e2,
                       Iterator o)
        : begin1(b1), end1(e1)
        , begin2(b2), end2(e2)
        , out(o)
    {}
};

template <typename Iterator>
struct ParallelMergeBody {
    void operator()(ParallelMergeRange<Iterator>& r) const {
        std::merge(r.begin1, r.end1, r.begin2, r.end2, r.out);
    }
};

template <typename Iterator>
void ParallelMerge(Iterator begin1, Iterator end1, Iterator begin2, Iterator end2, Iterator out) {
    tbb::parallel_for(ParallelMergeRange<Iterator>{begin1, end1, begin2, end2, out},
                      ParallelMergeBody<Iterator>{});
}
/*end_parallel_for_merge_example*/

#include <iostream>

int main() {
    // Average
    std::vector<float> input(10000, 1);
    std::vector<float> output(10000, 0);

    ParallelAverage(input, output);
    if (output != std::vector<float>{10000, 1}) {
        std::cerr << "Incorrect average" << std::endl;
        return 1;
    }

    // Merge
    std::vector<int> input1(10000), input2(5000);

    int input1_value = 0;
    int input2_value = 1;

    for (auto& input : input1) {
        input = input1_value;
        input1_value += 2;
    }

    for (auto& input : input2) {
        input = input2_value;
        input2_value += 2;
    }

    std::vector<int> output(input1.size() + input2.size(), 0);
    
    ParallelMerge(input1.begin(), input1.end(), input2.begin(), input2.end(), output.begin());

    int output_value = 0;

    for (auto& value : output) {
        if (value != output_value) {
            std::cerr << "Incorrect merge" << std::cerr;
            return 1;
        }
        ++output_value;
    }
}
