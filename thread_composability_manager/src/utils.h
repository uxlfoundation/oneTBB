/*
   Copyright (c) 2024 Intel Corporation
   Copyright (c) 2026 UXL Foundation Contributors

   SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

#ifndef __TCM_UTILS_HEADER
#define __TCM_UTILS_HEADER

#include <cstdio>
#include <cstring>
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
    constexpr unsigned num_bytes = sizeof(tcm_permit_flags_t);
    static_assert(num_bytes == 4 && sizeof(uint32_t) == num_bytes,
                  "The function assumes permit flags type has specific size.");
    uint32_t v = 0;
    std::memcpy(&v, &f, sizeof(v));
    constexpr unsigned size = 2 * num_bytes + 1; // 2 HEX characters per byte + terminating null
    char a[size] = {};
    std::snprintf(a, size, "0x%X", v); // Note: Allocation of bit fields is implementation-defined
    return std::string(a);
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
