/*
    Copyright (c) 2023 Intel Corporation
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

#include <cstdio> // for std::fputs, std::vsnprintf, std::fprintf
#include <cstdarg> // for C variadic functions

#if _WIN32
    extern "C" __declspec(dllimport) unsigned hwloc_get_api_version();
#else
    extern "C" unsigned hwloc_get_api_version();
#endif

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

void print_extra_info(const char* format, ...) {
    constexpr int buffer_size = 512;
    char buffer[buffer_size] {};
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, buffer_size, format, args);
    va_end(args);
    std::fprintf(stderr, "TCM: %s\n", buffer);
}

void environment::print_version(const internal::environment& env_info) {
    // The leading "\0" is needed to provide a clean result when applying "strings" command to binary
    static const char tcm_version_string[] = "\0" TCM_PRINT_VERSION;
    if (env_info.tcm_version > 0) {
        std::fputs(tcm_version_string+1, stderr);
        unsigned hwloc_version = hwloc_get_api_version();
        print_extra_info("%s\t\t\t%d.%d.%d", "HWLOC API version", (hwloc_version >> 16), (hwloc_version >> 8) & 0xff, hwloc_version & 0xff);
        print_extra_info("%s\t\t\t%s", "HWLOC library path", get_hwloc_path());
        print_extra_info("%s\t%.2f", "TCM_OVERSUBSCRIPTION_FACTOR", env_info.tcm_oversubscription_factor);
        print_extra_info("%s\t%s", "TCM_RESOURCE_DISTRIBUTION_STRATEGY", env_info.tcm_resource_distribution_strategy);
    }
}

} // namespace internal
} // namespace tcm

extern "C" {
const char* tcmRuntimeVersion() {
    static const char tcm_version_string[] = TCM_VERSION;
    return tcm_version_string;
}

unsigned tcmRuntimeInterfaceVersion() {
    return TCM_INTERFACE_VERSION;
}
}
