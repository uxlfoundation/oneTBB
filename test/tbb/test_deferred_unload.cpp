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

//! \file test_deferred_unload.cpp
//! \brief Regression test for the deferred unloading of dynamically loaded TBB runtimes.
//!
//! The host does not link oneTBB; it dlopen()s test_deferred_unload_plugin, which does.
//! Modes: exit (never dlclose, the Python-extension scenario), close (explicit dlclose),
//! reload (repeated close). The process must exit with status 0.

#include <cstdio>
#include <string>

#include <dlfcn.h>

#ifndef PLUGIN_PATH
#error "PLUGIN_PATH must be defined to the path of test_deferred_unload_plugin"
#endif

namespace {

using run_work_t = void (*)();
using stop_work_t = void (*)();

void report(const char* what, const char* detail) {
    std::fprintf(stderr, "FAILED: %s%s%s\n", what,
                 detail ? ": " : "", detail ? detail : "");
}

int load_and_run(void*& handle) {
    handle = dlopen(PLUGIN_PATH, RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        report("dlopen", dlerror());
        return 1;
    }
    run_work_t run_work = reinterpret_cast<run_work_t>(dlsym(handle, "run_work"));
    if (!run_work) {
        report("dlsym(run_work)", dlerror());
        return 1;
    }
    run_work();
    return 0;
}

int unload(void*& handle) {
    // Join the workers first: unloading oneTBB while they run is a separate, pre-existing
    // hazard. This test targets the unload ordering of the libraries oneTBB loaded itself.
    stop_work_t stop_work = reinterpret_cast<stop_work_t>(dlsym(handle, "stop_work"));
    if (!stop_work) {
        report("dlsym(stop_work)", dlerror());
        return 1;
    }
    stop_work();
    if (dlclose(handle) != 0) {
        report("dlclose", dlerror());
        return 1;
    }
    handle = nullptr;
    return 0;
}

// Load the plugin but never dlclose() it: the libraries oneTBB loaded are unloaded from
// the process exit handlers.
int run_exit() {
    void* handle = nullptr;
    return load_and_run(handle);
}

// Explicitly dlclose() the plugin: the libraries oneTBB loaded must be unloaded too.
int run_close() {
    void* handle = nullptr;
    int rc = load_and_run(handle);
    if (rc) {
        return rc;
    }
    return unload(handle);
}

int run_reload() {
    constexpr int iterations = 3;
    for (int i = 0; i < iterations; ++i) {
        void* handle = nullptr;
        int rc = load_and_run(handle);
        if (rc) {
            return rc;
        }
        rc = unload(handle);
        if (rc) {
            return rc;
        }
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s exit|close|reload\n", argv[0]);
        return 2;
    }

    const std::string mode(argv[1]);
    if (mode == "exit") {
        return run_exit();
    }
    if (mode == "close") {
        return run_close();
    }
    if (mode == "reload") {
        return run_reload();
    }

    std::fprintf(stderr, "unknown mode: %s\n", mode.c_str());
    return 2;
}
