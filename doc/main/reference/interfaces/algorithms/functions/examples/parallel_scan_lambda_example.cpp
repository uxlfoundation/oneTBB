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

/*begin_parallel_scan_lambda_example*/

#include <oneapi/tbb/parallel_scan.h>
#include <oneapi/tbb/blocked_range.h>
#include <vector>

template <typename T>
T DoParallelScan(T identity, std::vector<T>& y, const std::vector<T>& z) {
    return tbb::parallel_scan(
        tbb::blocked_range<int>(0,z.size()),
        identity,
        [&](const tbb::blocked_range<int>& r, T sum, bool is_final_scan) -> T {
            T temp = sum;
            for( int i = r.begin(); i < r.end(); ++i ) {
                temp += z[i];
                if( is_final_scan )
                    y[i] = temp;
            }
            return temp;
        },
        []( T left, T right ) {
            return left + right;
        }
    );
}
/*end_parallel_scan_lambda_example*/

#include <iostream>

int main() {
    std::vector<int> input(10000, 1);
    std::vector<int> output(10000, 0);

    int sum = DoParallelScan(0, output, input);

    if (sum != 10000) {
        std::cerr << "Incorrect sum" << std::endl;
        return 1;
    }

    int output_value = 0;
    for (int item : output) {
        if (item != output_value) {
            std::cerr << "Incorrect prefix sum" << std::endl;
            return 1;
        }
        ++output_value;
    }
}
