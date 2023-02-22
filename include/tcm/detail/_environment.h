/*
    Copyright (c) 2023 Intel Corporation
*/

#ifndef __TCM_ENVIRONMENT_HEADER
#define __TCM_ENVIRONMENT_HEADER

#include "_tcm_assert.h"

#include <cstdlib> // for std::atoi, std::atof
#include <cstring> // for std::strncpy

namespace tcm {
namespace internal {
    struct environment {
        static constexpr int buffer_size = 16;
        int tcm_disable = 0;
        int tcm_version = 0;
        float tcm_oversubscription_factor = 1.0;
        char tcm_resource_distribution_strategy[buffer_size+1] = "FAIR";

        environment() {
            process_env_var("TCM_VERSION", tcm_version);
            process_env_var("TCM_DISABLE", tcm_disable);
            process_env_var("TCM_OVERSUBSCRIPTION_FACTOR", tcm_oversubscription_factor);
            process_env_var("TCM_RESOURCE_DISTRIBUTION_STRATEGY", tcm_resource_distribution_strategy);

            print_version(*this);
        }

        static void print_version(const environment&);

    private:
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
                dest = std::strncpy(dest, value, buffer_size);
            }
        }
    };
} // namespace internal
} // namespace tcm

#endif // __TCM_ENVIRONMENT_HEADER
