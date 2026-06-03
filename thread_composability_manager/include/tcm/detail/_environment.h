/*
   Copyright (c) 2023 Intel Corporation
   Copyright (c) 2026 UXL Foundation Contributors

   SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

#ifndef __TCM_ENVIRONMENT_HEADER
#define __TCM_ENVIRONMENT_HEADER

#include "_tcm_assert.h"

#include <cstdlib> // for std::atoi, std::atof
#include <cstring> // for std::strncpy
#include "tcm/types.h" // for tcm_automatic

namespace tcm {
namespace internal {
    struct environment {
        static constexpr int string_size = 16;
        int tcm_enable = tcm_automatic;
        int tcm_version = 0;
        float tcm_oversubscription_factor = 1.0f;
        char tcm_resource_distribution_strategy[string_size+1] = "FAIR";

        environment() {
            process_env_var("TCM_VERSION", tcm_version);
            process_env_var("TCM_ENABLE", tcm_enable);

            print_version(*this);
        }

        static int get_version_string(const environment& env_info, char* buffer, uint32_t buffer_size);

    private:
        static void print_version(const environment& env_info);
        // MSVC Warning: Arg can be incorrect: this does not match function name specification
        __TCM_SUPPRESS_WARNING_WITH_PUSH(6387)
        char* get_env(const char* envname) {
            __TCM_ASSERT(envname, "get_env requires valid C string");
            return std::getenv(envname);
        }
        __TCM_SUPPRESS_WARNING_POP

        void process_env_var(const char* env_var, int& dest) {
            if (const char* value = get_env(env_var)) {
                dest = std::atoi(value);
            }
        }
    };
} // namespace internal
} // namespace tcm

#endif // __TCM_ENVIRONMENT_HEADER
