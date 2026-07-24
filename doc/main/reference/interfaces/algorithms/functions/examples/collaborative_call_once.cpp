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

/*begin_collaborative_call_once_example*/
#include <oneapi/tbb/collaborative_call_once.h>
#include <oneapi/tbb/parallel_reduce.h>
#include <oneapi/tbb/blocked_range.h>

extern double foo(int i);

class LazyData {
    oneapi::tbb::collaborative_once_flag flag;
    double cachedProperty;
public:
    double getProperty() {
        oneapi::tbb::collaborative_call_once(flag, [&] {
            // serial part
            double result{};

            // parallel part where threads can collaborate
            result = oneapi::tbb::parallel_reduce(oneapi::tbb::blocked_range<int>(0, 1000), 0.,
                [] (auto r, double val) {
                    for(int i = r.begin(); i != r.end(); ++i) {
                        val += foo(i);
                    }
                    return val;
                },
                std::plus<double>{}
            );

            // continue serial part
            cachedProperty = result;
        });

        return cachedProperty;
    }
};
/*end_collaborative_call_once_example*/

#include <iostream>

double foo(int input) {
    return double(input);
}

int main() {
    LazyData data;

    double property = data.getProperty();
    
    if (property != data.getProperty()) {
        std::cerr << "Incorrect cached property" << std::endl;
    }
}
