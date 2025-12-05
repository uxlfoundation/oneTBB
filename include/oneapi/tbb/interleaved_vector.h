#ifndef __TBB_interleaved_vector_H
#define __TBB_interleaved_vector_H

#include "detail/_config.h"
#include "detail/_namespace_injection.h"

namespace tbb {
namespace detail {

namespace r1 {
TBB_EXPORT void *__TBB_EXPORTED_FUNC alloc_interleave(size_t bank, size_t len);
TBB_EXPORT int __TBB_EXPORTED_FUNC free_interleave(void *addr, size_t len);
} // namespace r1

namespace d1 {

template<typename T, size_t STRIDE>
struct interleaved_vector {
    // number of banks is run-time parameter
    const std::size_t num_banks;
    std::vector<T*> banks;
    std::vector<std::size_t> sizes;

    interleaved_vector(std::size_t num_banks, std::size_t count)
        : num_banks(num_banks), banks(num_banks), sizes(num_banks) {
        std::size_t total_size = count * sizeof(T);
        int num_strides = (total_size + STRIDE - 1) / STRIDE;
        int tail = num_strides % num_banks;
        for (std::size_t i = 0; i < num_banks; ++i) {
            std::size_t bank_strides = num_strides / num_banks + (i < tail ? 1 : 0);
            sizes[i] = bank_strides * STRIDE;
#if 1
            banks[i] = sizes[i] ? static_cast<T*>(tbb::detail::r1::alloc_interleave(i, sizes[i])) : nullptr;
#else
            banks[i] = sizes[i] ? static_cast<T*>(malloc(sizes[i])) : nullptr;
#endif
        }
    }
    T& operator[](std::size_t index) const __attribute__((always_inline)) {
        static_assert(STRIDE % sizeof(T) == 0, "STRIDE must be multiple of sizeof(T)");
        constexpr std::size_t elems_in_stride = STRIDE / sizeof(T);

#if 0
        std::size_t bank = (index / elems_in_stride) % num_banks;
        std::size_t offset = (index / (elems_in_stride * num_banks)) * elems_in_stride + index % elems_in_stride;
        return banks[bank][offset];
#else
        // Fast path: if num_banks is power of 2, use bit operations
        // Check at runtime if it's power of 2: (num_banks & (num_banks - 1)) == 0
        if (__builtin_expect((num_banks & (num_banks - 1)) == 0, 1)) {
            const std::size_t stride_index = index / elems_in_stride;
            const std::size_t bank = stride_index & (num_banks - 1);  // fast modulo
            const std::size_t major_offset = (stride_index >> __builtin_ctzll(num_banks)) * elems_in_stride;
            const std::size_t minor_offset = index & (elems_in_stride - 1);  // fast modulo if power of 2
            const std::size_t offset = major_offset + minor_offset;
            return banks[bank][offset];
        }
        
        // Fallback for non-power-of-2
        const std::size_t stride_index = index / elems_in_stride;
        const std::size_t bank = stride_index % num_banks;
        const std::size_t major_offset = (stride_index / num_banks) * elems_in_stride;
        const std::size_t minor_offset = index % elems_in_stride;
        const std::size_t offset = major_offset + minor_offset;
        
        return banks[bank][offset];
#endif
    }
    ~interleaved_vector() {
#if 1
        for (std::size_t i = 0; i < num_banks; ++i)
            tbb::detail::r1::free_interleave(banks[i], sizes[i]);
#endif
    }
};

template <typename T, size_t STRIDE, std::size_t B>
struct mdspan {
    interleaved_vector<T, STRIDE> *data;

    mdspan(interleaved_vector<T, STRIDE> *data) : data(data) {}

    T& operator()(std::size_t i, std::size_t j) const __attribute__((always_inline)) {
        static_assert(STRIDE % sizeof(T) == 0, "STRIDE must be multiple of sizeof(T)");
        constexpr std::size_t elems_in_stride = STRIDE / sizeof(T);

        std::size_t index = i * B + j;
        std::size_t num_banks = data->num_banks;
#if 0
        std::size_t bank = index / elems_in_stride % num_banks;
        std::size_t offset = index / (elems_in_stride * num_banks) * elems_in_stride + index % elems_in_stride;
        return data->banks[bank][offset];
#else
        // Fast path: if num_banks is power of 2, use bit operations
        // Check at runtime if it's power of 2: (num_banks & (num_banks - 1)) == 0
        if (__builtin_expect((num_banks & (num_banks - 1)) == 0, 1)) {
            const std::size_t stride_index = index / elems_in_stride;
            const std::size_t bank = stride_index & (num_banks - 1);  // fast modulo
            const std::size_t major_offset = (stride_index >> __builtin_ctzll(num_banks)) * elems_in_stride;
            const std::size_t minor_offset = index & (elems_in_stride - 1);  // fast modulo if power of 2
            const std::size_t offset = major_offset + minor_offset;
            return data->banks[bank][offset];
        }
        
        // Fallback for non-power-of-2
        const std::size_t stride_index = index / elems_in_stride;
        const std::size_t bank = stride_index % num_banks;
        const std::size_t major_offset = (stride_index / num_banks) * elems_in_stride;
        const std::size_t minor_offset = index % elems_in_stride;
        const std::size_t offset = major_offset + minor_offset;
        
        return data->banks[bank][offset];
#endif
    }
};

} // namespace d1
} // namespace detail

inline namespace v1 {
using detail::d1::interleaved_vector;
using detail::d1::mdspan;
} // namespace v1

} // namespace tbb

#endif /* __TBB_interleaved_vector_H */
