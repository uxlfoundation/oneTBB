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

#ifndef WAVEFRONT_UTILS_H
#define WAVEFRONT_UTILS_H

#include <vector>
#include <chrono>
#include <string>

#include "common/utility/fast_random.hpp"

inline std::string generate_random_string(std::size_t length) {
    const char charset[] = "abcdefghijklmnopqrstuvwxyz";
    utility::FastRandom rand(41);

    std::string result;
    result.reserve(length);
    for (std::size_t i = 0; i < length; ++i) {
        result += charset[rand.get() % (sizeof(charset) - 1)];
    }
    return result;
}

inline void spin_wait_for_at_least(std::chrono::steady_clock::duration dur) {
    if (dur == std::chrono::steady_clock::duration{}) return;
    auto t1 = std::chrono::steady_clock::now();
    while ((std::chrono::steady_clock::now() - t1) < dur) {};
}

inline double calculate(double a, double b, double c, int block_size){
    double x = (a + b + c) / 3;
    int dummy = 0;
    for (int i = 0; i < block_size; i++) {
        dummy += (a + b + c) / 4;
    }

    if (!dummy) {
        spin_wait_for_at_least(std::chrono::nanoseconds(dummy));
    }

    return x;
}

#endif /* WAVEFRONT_UTILS_H */
