#ifndef __TBB_interleaved_vector_H
#define __TBB_interleaved_vector_H

#include "detail/_config.h"
#include "detail/_utils.h"
#include "detail/_namespace_injection.h"

namespace tbb {
namespace detail {

namespace r1 {
TBB_EXPORT void *__TBB_EXPORTED_FUNC alloc_interleave(size_t bank, size_t len);
TBB_EXPORT int __TBB_EXPORTED_FUNC free_interleave(void *addr, size_t len);
} // namespace r1

#define INTERLEAVE_MEM 1

namespace d1 {

template<typename T, size_t STRIDE>
struct interleaved_vector;

template<typename T, size_t STRIDE>
class interleaved_vector_iterator {
    interleaved_vector<T, STRIDE> const* my_vector;
    std::size_t my_index;
public:
    using value_type = T;
    using iterator_category = std::random_access_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using pointer = T*;
    using reference = T&;

    interleaved_vector_iterator( interleaved_vector<T, STRIDE> const& vector, std::size_t index )
        : my_vector(&vector), my_index(index)
    {}
    interleaved_vector_iterator() : my_vector(nullptr), my_index(~std::size_t(0))
    {}
    T& operator*() const {
        return (*my_vector)[my_index];
    }
    interleaved_vector_iterator& operator++() {
        ++my_index;
        return *this;
    }
    interleaved_vector_iterator operator++(int) {
        interleaved_vector_iterator result = *this;
        ++(*this);
        return result;
    }
    bool operator==(const interleaved_vector_iterator& other) const {
        return my_vector == other.my_vector && my_index == other.my_index;
    }
    bool operator!=(const interleaved_vector_iterator& other) const {
        return !(*this == other);
    }
    interleaved_vector_iterator operator+(difference_type offset) const {
        return interleaved_vector_iterator(*my_vector, my_index + offset);
    }
    interleaved_vector_iterator& operator+=(difference_type offset) {
        my_index += offset;
        return *this;
    }
    interleaved_vector_iterator operator-(difference_type offset) const {
        return interleaved_vector_iterator(*my_vector, my_index - offset);
    }
    interleaved_vector_iterator& operator-=(difference_type offset) {
        my_index -= offset;
        return *this;
    }
    difference_type operator-(const interleaved_vector_iterator& other) const {
        return static_cast<difference_type>(my_index) - static_cast<difference_type>(other.my_index);
    }
};

template<typename T, size_t STRIDE>
struct interleaved_vector {
    // number of banks must be run-time parameter
    const std::size_t num_banks;
    const std::size_t count;
    std::vector<std::pair<T*, std::size_t>> banks;
    std::vector<T*> segments;

    interleaved_vector(std::size_t num_banks, std::size_t count)
        : num_banks(num_banks), count(count), banks(num_banks) {
        static_assert(STRIDE % sizeof(T) == 0, "STRIDE must be multiple of sizeof(T)");
        constexpr std::size_t elems_in_stride = STRIDE / sizeof(T);

        std::size_t total_size = count * sizeof(T);
        size_t num_strides = (total_size + STRIDE - 1) / STRIDE;

        segments.resize(num_strides);

        size_t tail = num_strides % num_banks;
        for (std::size_t i = 0; i < num_banks; ++i) {
            std::size_t bank_strides = num_strides / num_banks + (i < tail ? 1 : 0);
            banks[i].second = bank_strides * STRIDE;
#if INTERLEAVE_MEM
            banks[i].first = banks[i].second ? static_cast<T*>(tbb::detail::r1::alloc_interleave(i, banks[i].second)) : nullptr;
#else
            banks[i].first = banks[i].second ? static_cast<T*>(malloc(banks[i].second)) : nullptr;
#endif
        }

        for (size_t s = 0; s < num_strides; ++s) {
            std::size_t bank = s % num_banks;
            std::size_t bank_stride_index = s / num_banks;
            // move backwards to use just index for indexing
            segments[s] = banks[bank].first + bank_stride_index * elems_in_stride - elems_in_stride * s;
        }
    }
    T& operator[](std::size_t index) const __attribute__((always_inline)) {
        constexpr std::size_t elems_in_stride = STRIDE / sizeof(T);
        if constexpr ((elems_in_stride & (elems_in_stride - 1)) == 0) {
            constexpr std::size_t shift = __builtin_ctzll(elems_in_stride);  // log2 for division
            __TBB_ASSERT(shift == tbb::detail::log2(uintptr_t(elems_in_stride)), "log2 calculation error");

            return segments[index >> shift][index];
        }
        return segments[index / elems_in_stride][index];
    }
    ~interleaved_vector() {
        for (std::size_t i = 0; i < num_banks; ++i)
#if INTERLEAVE_MEM
            tbb::detail::r1::free_interleave(banks[i].first, banks[i].second);
#else
            free(banks[i].first);
#endif
    }

    using iterator = interleaved_vector_iterator<T, STRIDE>;
    iterator begin() const {
        return iterator(*this, 0);
    }
    iterator end() const {
        return iterator(*this, this->size());
    }
    std::size_t size() const { return count; }
};

template <typename T, size_t STRIDE, std::size_t B>
struct mdspan {
    interleaved_vector<T, STRIDE> *data;

    mdspan(interleaved_vector<T, STRIDE> *data) : data(data) {}

    T& operator()(std::size_t i, std::size_t j) const __attribute__((always_inline)) {
        return (*data)[i * B + j];
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
