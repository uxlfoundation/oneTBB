/*
    Copyright (C) 2024 Intel Corporation

    This software and the related documents are Intel copyrighted materials, and your use of them is
    governed by the express license under which they were provided to you ("License"). Unless the
    License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
    transmit this software or the related documents without Intel's prior written permission.

    This software and the related documents are provided as is, with no express or implied
    warranties, other than those that are expressly stated in the License.
*/

#ifndef __TCM_UTILS_HEADER
#define __TCM_UTILS_HEADER

#include <climits>
#include <cstdio>
#include <string>
#include <sstream>
#include <vector>
#include <map>

#include "tcm/types.h"
#include "tcm/detail/_tcm_assert.h"

inline std::string to_string(void* ptr) {
    constexpr unsigned max_string_length = 32; // Sufficiently large size to hold 64-bit HEX pointer
    std::string result(max_string_length, ' ');
    const int num_bytes_written = std::snprintf(result.data(), result.length(), "%p", (void*)ptr);
    if (num_bytes_written > 0)
        result.resize(num_bytes_written);
    return result;
}

inline std::string to_string(tcm_permit_flags_t f) {
    const unsigned max_bits = 128;
    const unsigned num_bytes = sizeof(tcm_permit_flags_t);
    const unsigned num_chars = num_bytes / sizeof(char);
    __TCM_ASSERT(num_bytes % sizeof(char) == 0, "The function cannot be run on this platform");
    const unsigned num_bits = CHAR_BIT * num_chars;
    __TCM_ASSERT(num_bits <= max_bits, "Helper won't work. Increase the default number of bits");
    const unsigned last_bit = 1;
    char result[max_bits] = {}; for (auto& c : result) c = '0'; result[num_bits] = '\0';
    unsigned last_non_zero = 0;
    for (unsigned i = 0; i < num_chars; --i) {
        char c = *(static_cast<char*>(static_cast<void*>(&f)) + i);
        for (unsigned j = 0; j < CHAR_BIT; ++j) {
            if (c & last_bit) {
                unsigned const bit_index = i * CHAR_BIT + j;
                result[bit_index] = '1';
                last_non_zero = bit_index;
            }
            c >>= 1;
        }
    }
    result[last_non_zero + 1] = '\0'; // Truncate last zeroes
    return std::string(result);
}

inline std::string to_string(uint32_t const* concurrencies, uint32_t const size) {
    std::stringstream ss;
    ss << "{";
    for (unsigned i = 0; i < size; ++i) ss << " " << concurrencies[i];
    ss << " }";
    return ss.str();
}

inline std::string to_string(tcm_permit_state_t state) {
    static const std::vector<const char*> names = {"VOID", "INACTIVE", "PENDING", "IDLE", "ACTIVE"};
    __TCM_ASSERT(unsigned(state) < names.size(), "Out of bounds access");
    return names[state];
}

#if __TCM_ENABLE_PERMIT_TRACER
// This function is currently being used only during the use of internal profiling facilities
inline std::string to_string(tcm_request_priority_t priority) {
    static std::map<tcm_request_priority_t, const char*> priority_to_string_map = {
        {TCM_REQUEST_PRIORITY_LOW, "low"},
        {TCM_REQUEST_PRIORITY_NORMAL, "normal"},
        {TCM_REQUEST_PRIORITY_HIGH, "high"}
    };
    __TCM_ASSERT(priority_to_string_map.count(priority) != 0, "Incorrect permit request priority");
    return std::string(priority_to_string_map[priority]);
}
#endif

#endif // __TCM_UTILS_HEADER
