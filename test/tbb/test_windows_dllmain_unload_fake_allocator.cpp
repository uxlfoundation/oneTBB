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

//! \file test_windows_dllmain_unload_fake_allocator.cpp
//! \brief Minimal tbbmalloc.dll stand-in for test_windows_dllmain_unload.cpp.
//!
//! oneTBB resolves these four exports through dynamic_link(). The call counter lets the host prove
//! that the stand-in was actually pinned before it checks loader reference counts.

#include <atomic>
#include <cstdlib>
#include <malloc.h>

namespace {
std::atomic<int> allocation_calls{0};
} // namespace

extern "C" __declspec(dllexport) void* scalable_malloc(std::size_t size) {
    allocation_calls.fetch_add(1, std::memory_order_relaxed);
    return std::malloc(size);
}

extern "C" __declspec(dllexport) void scalable_free(void* pointer) {
    std::free(pointer);
}

extern "C" __declspec(dllexport) void* scalable_aligned_malloc(std::size_t size, std::size_t alignment) {
    allocation_calls.fetch_add(1, std::memory_order_relaxed);
    return _aligned_malloc(size, alignment);
}

extern "C" __declspec(dllexport) void scalable_aligned_free(void* pointer) {
    _aligned_free(pointer);
}

extern "C" __declspec(dllexport) int fake_allocator_calls() {
    return allocation_calls.load(std::memory_order_relaxed);
}
