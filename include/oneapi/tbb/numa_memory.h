#ifndef __TBB_numa_memory_H
#define __TBB_numa_memory_H

#include "info.h"

namespace tbb {
namespace detail {

namespace r1 {

TBB_EXPORT void *__TBB_EXPORTED_FUNC alloc_interleaved(size_t size, size_t interleaving_step,
                        const tbb::detail::d1::numa_node_id *nodes, size_t nodes_count);
TBB_EXPORT void __TBB_EXPORTED_FUNC free_interleaved(void *ptr, size_t size);

inline void *alloc_interleaved(size_t size) {
    return alloc_interleaved(size, 0, nullptr, 0);
}

inline void *alloc_interleaved(size_t size, const std::vector<tbb::detail::d1::numa_node_id> &nodes) {
    return alloc_interleaved(size, 0, nodes.data(), nodes.size());
}

inline void *alloc_interleaved(size_t size, size_t interleaving_step) {
    return alloc_interleaved(size, interleaving_step, nullptr, 0);
}

inline void *alloc_interleaved(size_t size, size_t interleaving_step,
                               const std::vector<tbb::detail::d1::numa_node_id> &nodes) {
    return alloc_interleaved(size, interleaving_step, nodes.data(), nodes.size());
}


} // namespace r1
} // namespace detail

inline namespace v1 {
using detail::r1::alloc_interleaved;
using detail::r1::free_interleaved;
} // inline namespace v1

} // namespace tbb

#endif /* __TBB_numa_memory_H */
