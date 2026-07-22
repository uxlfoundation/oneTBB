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

/*begin_parallel_reduce_lambda_example*/
#include <oneapi/tbb/parallel_reduce.h>
#include <oneapi/tbb/blocked_range.h>
#include <vector>

float ParallelSum(const std::vector<float>& input) {
    return tbb::parallel_reduce(
        tbb::blocked_range<float*>(input.data(), input.data() + input.size()),
        0.f,
        [](const tbb::blocked_range<float*>& r, float init) -> float {
            for (float* a = r.begin(); a != r.end(); ++a) {
                init += *a;
            }
            return init;
        },
        [](float x, float y) -> float {
            return x + y;
        }
    );
}
/*end_parallel_reduce_lambda_example*/

#include <cmath>
#include <iostream>

int main() {
    std::vector<float> input(10000, 1.0f);

    float sum = ParallelSum(input);

    if (std::fabs(sum - 10000.0f) >= 1e-3f) {
        std::cerr << "Incorrect ParallelSum" << std::endl;
        return 1;
    }
}
