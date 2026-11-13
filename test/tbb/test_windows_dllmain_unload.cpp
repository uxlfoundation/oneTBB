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

//! \file test_windows_dllmain_unload.cpp
//! \brief Regression test for dynamic unloads performed from Windows DllMain.
//!
//! The host does not link oneTBB. It preloads a fake tbbmalloc.dll, dynamically loads oneTBB and a
//! plugin that uses it, then explicitly unloads oneTBB. oneTBB's dynamic_link() takes a loader
//! reference to the fake allocator. After the host releases its own reference, that reference must
//! still be present: unloading it from DLL_PROCESS_DETACH calls FreeLibrary() while the loader lock
//! is held. With the fix, oneTBB leaves the reference for the OS; the host releases it explicitly.

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace {

using attach_scheduler_t = void (*)();
using run_work_t = int (*)();
using finalize_work_t = int (*)();
using fake_allocator_calls_t = int (*)();

int fail(const char* message, DWORD error = 0) {
    if (error) {
        std::fprintf(stderr, "FAILED: %s (error %lu)\n", message, error);
    } else {
        std::fprintf(stderr, "FAILED: %s\n", message);
    }
    return 1;
}

std::string module_directory() {
    char path[MAX_PATH] = {0};
    DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    std::string result(path);
    const std::size_t slash = result.find_last_of("\\/");
    return slash == std::string::npos ? std::string() : result.substr(0, slash);
}

std::string base_name(const char* path) {
    std::string result(path);
    const std::size_t slash = result.find_last_of("\\/");
    return slash == std::string::npos ? result : result.substr(slash + 1);
}

void* symbol(HMODULE module, const char* name) {
    return reinterpret_cast<void*>(GetProcAddress(module, name));
}

int run_cycle(const char* tbb_path, const char* plugin_path, const char* fake_allocator_name, int cycle) {
    const std::string directory = module_directory();
    if (directory.empty()) {
        return fail("could not determine the host module directory");
    }
    const std::string fake_path = directory + "\\" + fake_allocator_name;
    const std::string tbb_name = base_name(tbb_path);

    HMODULE fake = LoadLibraryA(fake_path.c_str());
    if (!fake) {
        return fail("could not load the fake allocator", GetLastError());
    }
    HMODULE tbb = LoadLibraryA(tbb_path);
    if (!tbb) {
        return fail("could not load oneTBB", GetLastError());
    }
    HMODULE plugin = LoadLibraryA(plugin_path);
    if (!plugin) {
        return fail("could not load the plugin", GetLastError());
    }

    auto attach_scheduler = reinterpret_cast<attach_scheduler_t>(symbol(plugin, "attach_scheduler"));
    auto run_work = reinterpret_cast<run_work_t>(symbol(plugin, "run_work"));
    auto finalize_work = reinterpret_cast<finalize_work_t>(symbol(plugin, "finalize_work"));
    auto fake_allocator_calls = reinterpret_cast<fake_allocator_calls_t>(symbol(fake, "fake_allocator_calls"));
    if (!attach_scheduler || !run_work || !finalize_work || !fake_allocator_calls) {
        return fail("a required exported symbol was not resolved");
    }

    attach_scheduler();
    if (run_work() != 0) {
        return fail("plugin work failed");
    }
    if (fake_allocator_calls() == 0) {
        return fail("oneTBB did not use the preloaded fake allocator");
    }
    if (finalize_work() != 0) {
        return fail("scheduler finalization failed");
    }

    if (!FreeLibrary(plugin)) {
        return fail("could not unload the plugin", GetLastError());
    }
    if (!FreeLibrary(tbb)) {
        return fail("could not unload oneTBB", GetLastError());
    }
    if (GetModuleHandleA(tbb_name.c_str())) {
        return fail("oneTBB is still loaded after FreeLibrary");
    }
    if (!FreeLibrary(fake)) {
        return fail("could not release the host allocator reference", GetLastError());
    }

    // With the DllMain unload, oneTBB has already released its reference and the module is gone.
    if (!GetModuleHandleA(fake_allocator_name)) {
        return fail("oneTBB released a dynamic-link handle from DllMain");
    }
    // With the fix, one reference remains. Release it from normal thread context.
    if (!FreeLibrary(fake)) {
        return fail("could not release the retained allocator reference", GetLastError());
    }
    if (GetModuleHandleA(fake_allocator_name)) {
        return fail("unexpected extra references to the fake allocator");
    }

    std::printf("cycle %d: no dynamic unload from DllMain\n", cycle);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 5) {
        std::fprintf(stderr,
                     "usage: %s close|reload <tbb-dll> <plugin-dll> <fake-allocator-name>\n",
                     argv[0]);
        return 2;
    }

    const char* mode = argv[1];
    int cycles = 0;
    if (std::strcmp(mode, "close") == 0) {
        cycles = 1;
    } else if (std::strcmp(mode, "reload") == 0) {
        cycles = 3;
    } else {
        std::fprintf(stderr, "unknown mode: %s\n", mode);
        return 2;
    }

    for (int cycle = 0; cycle < cycles; ++cycle) {
        const int result = run_cycle(argv[2], argv[3], argv[4], cycle);
        if (result) {
            return result;
        }
    }
    return 0;
}
