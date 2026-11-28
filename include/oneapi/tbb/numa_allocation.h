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

#ifndef __TBB_numa_allocation_H
#define __TBB_numa_allocation_H

#include "detail/_config.h"
#include "detail/_template_helpers.h"
#include <limits>
#if !__TBB_PREVIEW_NUMA_ALLOCATION
#error Set TBB_PREVIEW_NUMA_ALLOCATION to include numa_allocation.h
#endif

#include "info.h"

namespace tbb {
namespace detail {

namespace r1 {

TBB_EXPORT void *__TBB_EXPORTED_FUNC allocate_interleaved(size_t bytes,
    const tbb::detail::d1::numa_node_id *nodes, size_t nodes_count, size_t bytes_per_chunk);
TBB_EXPORT void __TBB_EXPORTED_FUNC deallocate_interleaved(void *ptr, size_t bytes);

} // namespace r1

namespace d1 {

inline void *allocate_numa_interleaved(size_t bytes,
                                       const std::vector<tbb::numa_node_id>& nodes,
                                       size_t bytes_per_chunk = 0) {
    if (nodes.empty())
        return nullptr;
    return r1::allocate_interleaved(bytes, nodes.data(), nodes.size(), bytes_per_chunk);
}

inline void *allocate_numa_interleaved(size_t bytes, size_t bytes_per_chunk = 0) {
    return r1::allocate_interleaved(bytes, nullptr, 0, bytes_per_chunk);
}

inline void deallocate_numa_interleaved(void *ptr, size_t bytes) {
    r1::deallocate_interleaved(ptr, bytes);
}

template <typename T>
inline T* allocate_numa_interleaved(
    size_t count,
    const std::vector<tbb::numa_node_id>& nodes,
    size_t bytes_per_chunk = 0)
{
    if (nodes.empty() || count > (std::numeric_limits<size_t>::max)() / sizeof(T))
        return nullptr;

    return static_cast<T*>(r1::allocate_interleaved(
        count * sizeof(T), nodes.data(), nodes.size(), bytes_per_chunk));
}

template <typename T>
inline T* allocate_numa_interleaved(
    size_t count,
    size_t bytes_per_chunk = 0)
{
    if (count > (std::numeric_limits<size_t>::max)() / sizeof(T))
        return nullptr;

    return static_cast<T*>(r1::allocate_interleaved(
        count * sizeof(T), nullptr, 0, bytes_per_chunk));
}

template <typename T>
inline void deallocate_numa_interleaved(type_identity_t<T>* ptr, size_t count)
{
    r1::deallocate_interleaved(const_cast<typename std::remove_cv<T>::type*>(ptr), count * sizeof(T));
}

// Delete the generic deallocate to prevent misuse with typed pointers without
// explicit template argument
template <typename T>
void deallocate_numa_interleaved(T, size_t) = delete;

} // namespace d1
} // namespace detail

inline namespace v1 {
using detail::d1::allocate_numa_interleaved;
using detail::d1::deallocate_numa_interleaved;
} // inline namespace v1

} // namespace tbb

#endif /* __TBB_numa_allocation_H */
