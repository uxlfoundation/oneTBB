/*
    Copyright (C) 2023 Intel Corporation

    This software and the related documents are Intel copyrighted materials, and your use of them is
    governed by the express license under which they were provided to you ("License"). Unless the
    License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
    transmit this software or the related documents without Intel's prior written permission.

    This software and the related documents are provided as is, with no express or implied
    warranties, other than those that are expressly stated in the License.
*/

#ifndef __TCM_ENVIRONMENT_HEADER
#define __TCM_ENVIRONMENT_HEADER

#include "_tcm_assert.h"

#include <cstdlib> // for std::atoi, std::atof
#include <cstring> // for std::strncpy

namespace tcm {
namespace internal {
    struct environment {
        static constexpr int string_size = 16;
        int tcm_disable = 0;
        int tcm_version = 0;
        float tcm_oversubscription_factor = 1.0;
        char tcm_resource_distribution_strategy[string_size+1] = "FAIR";

        environment() {
            process_env_var("TCM_VERSION", tcm_version);
            process_env_var("TCM_DISABLE", tcm_disable);
            process_env_var("TCM_OVERSUBSCRIPTION_FACTOR", tcm_oversubscription_factor);
            process_env_var("TCM_RESOURCE_DISTRIBUTION_STRATEGY", tcm_resource_distribution_strategy);

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
        void process_env_var(const char* env_var, float& dest) {
            if (const char* value = get_env(env_var)) {
                dest = static_cast<float>(std::atof(value));
            }
        }
        void process_env_var(const char* env_var, char* dest) {
            if (const char* value = get_env(env_var)) {
                dest = std::strncpy(dest, value, string_size);
            }
        }
    };
} // namespace internal
} // namespace tcm

#endif // __TCM_ENVIRONMENT_HEADER
