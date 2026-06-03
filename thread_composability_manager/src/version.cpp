/*
   Copyright (c) 2023 Intel Corporation
   Copyright (c) 2026 UXL Foundation Contributors

   SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

#include "tcm/detail/_environment.h"
#include "tcm/version.h"

#if _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

#include <cstdio> // for std::snprintf, std::vsnprintf, std::fprintf
#include <cstdarg> // for C variadic functions

#if _WIN32
    extern "C" __declspec(dllimport) unsigned hwloc_get_api_version();
#else
    extern "C" unsigned hwloc_get_api_version();
#endif

#ifndef __TCM_BUILD_TIME
    #define __TCM_BUILD_TIME "unknown"
#endif

#ifndef __TCM_COMMIT_ID
    #define __TCM_COMMIT_ID "unknown"
#endif

#ifndef TCM_DEBUG
    #define __TCM_DEBUG_STRING "undefined"
#else
    #define __TCM_DEBUG_STRING "set"
#endif

#define __TCM_TO_STR_IMPL(str) #str
#define __TCM_TO_STR(str) __TCM_TO_STR_IMPL(str)

#define TCM_VERSION_STRING __TCM_TO_STR(TCM_VERSION_MAJOR) "." \
                           __TCM_TO_STR(TCM_VERSION_MINOR) "." \
                           __TCM_TO_STR(TCM_VERSION_PATCH)     \

#define __TCM_VERSION_NUMBER \
    "TCM: VERSION            " TCM_VERSION_STRING "\n"

#define TCM_PRINT_VERSION __TCM_VERSION_NUMBER

namespace tcm {
namespace internal {

const char* get_hwloc_path() {
#if _WIN32
    static char path[MAX_PATH+1];
    HMODULE handle;
    bool res = GetModuleHandleEx(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCSTR)&hwloc_get_api_version,
        &handle
    );
    if (res) {
        DWORD dres = GetModuleFileName(handle, path, DWORD(MAX_PATH));
        if (dres != 0 && dres <= MAX_PATH) {
            return path;
        }
    }
#else
    Dl_info dlinfo;
    int res = dladdr((unsigned*)&hwloc_get_api_version, &dlinfo);
    if (res) {
        return dlinfo.dli_fname;
    }
#endif
    return "unknown";
}

int print_extra_info(char* buffer, uint32_t buffer_size, const char* format, ...) {
    constexpr int local_buffer_size = 512;
    char local_buffer[local_buffer_size] {};
    va_list args;
    va_start(args, format);
    std::vsnprintf(local_buffer, local_buffer_size, format, args);
    va_end(args);
    return std::snprintf(buffer, buffer_size, "TCM: %s\n", local_buffer);
}

int environment::get_version_string(const environment& env_info, char* buffer, uint32_t buffer_size) {
    __TCM_ASSERT(buffer, "Character buffer must be not null");
    constexpr uint32_t default_buffer_sz = 512;
    static struct tcm_meta_info {
        char version_info[default_buffer_sz]{};
        char hwloc_info[default_buffer_sz]{};
        char internal_info[default_buffer_sz]{};
        char tcm_variables_info[default_buffer_sz]{};

        tcm_meta_info(const environment& env_info) {
            // The leading "\0" is needed to get clean result when invoking "strings" on a binary
            static const char tcm_version_string[] = "\0" TCM_PRINT_VERSION;
            std::snprintf(version_info, default_buffer_sz, "%s" , tcm_version_string+1);

            unsigned hwloc_version = hwloc_get_api_version();

            int printed = 0;
            if (env_info.tcm_version > 0) {
                // -18 format is needed because the length "HWLOC LIBRARY PATH"
                // string is 18 symbols long so this amount of symbols must be reserved
                print_extra_info(
                    hwloc_info, default_buffer_sz, "%-18s %d.%d.%d", "HWLOC API VERSION",
                    (hwloc_version >> 16), (hwloc_version >> 8) & 0xff, (hwloc_version >> 0) & 0xff
                );
                printed  = print_extra_info(internal_info, default_buffer_sz,
                                            "%-18s %s", "HWLOC LIBRARY PATH", get_hwloc_path());
                printed += print_extra_info(internal_info + printed, default_buffer_sz-printed,
                                            "%-18s %s", "BUILD TIME", __TCM_BUILD_TIME);
                printed += print_extra_info(internal_info + printed, default_buffer_sz-printed,
                                            "%-18s %s", "COMMIT ID", __TCM_COMMIT_ID);
                printed += print_extra_info(internal_info + printed, default_buffer_sz-printed,
                                            "%-18s %s", "TCM_DEBUG", __TCM_DEBUG_STRING);
                print_extra_info(internal_info + printed, default_buffer_sz-printed,
                                 "%-18s %d", "TCM_ENABLE", env_info.tcm_enable);
            } else if (env_info.tcm_enable < 1) {
                print_extra_info(tcm_variables_info, default_buffer_sz,
                                 "%-18s %s", "TCM", "disabled");
            }

        }
    } print_info{env_info};

    return std::snprintf(buffer, buffer_size, "%s%s%s%s",
        print_info.version_info,
        print_info.hwloc_info,
        print_info.internal_info,
        print_info.tcm_variables_info
    );
}

void environment::print_version(const environment& env_info) {
    if (env_info.tcm_version > 0) {
        constexpr int buffer_size = 1024;
        char print_buffer[buffer_size];
        int printed = environment::get_version_string(env_info, print_buffer, buffer_size);
        __TCM_ASSERT_EX(printed <= buffer_size,
                        "Must not write more data than can be fit in the buffer.");
        std::fprintf(stderr, "%s", print_buffer);
    }
}

} // namespace internal
} // namespace tcm

extern "C" {
unsigned tcmGetVersion() {
    return TCM_VERSION;
}

const char* tcmRuntimeVersion() {
    static const char tcm_version_string[] = TCM_VERSION_STRING;
    return tcm_version_string;
}

unsigned tcmRuntimeInterfaceVersion() {
    return 1000 * TCM_VERSION_MAJOR + 10 * TCM_VERSION_MINOR;
}
}
