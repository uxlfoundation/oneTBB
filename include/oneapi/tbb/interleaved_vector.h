#ifndef __TBB_interleaved_vector_H
#define __TBB_interleaved_vector_H

#include <new> // for std::align_val_t
#include "info.h"
#include "detail/_config.h"
#include "detail/_utils.h"
#include "detail/_namespace_injection.h"

#include <numaif.h>
#include <numa.h>
#include <errno.h>

namespace tbb {
namespace detail {

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

inline int find_numa_node_linux(void* addr) {
    int numa_node = -1;
    int status[1];
    void* pages[1] = { addr };

    // Query which node owns this page
    if (move_pages(0, 1, pages, NULL, status, 0) == 0) {
        numa_node = status[0];
    }

    return numa_node;
}

#if 1

template<typename T, size_t STRIDE>
// create single address space, interleave it between NUMA nodes
struct interleaved_vector {
    // number of banks must be run-time parameter
    const std::size_t my_num_banks;
    std::unique_ptr<T[], std::function<void(T*)>> my_data;
    interleaved_vector(std::size_t num_banks, std::size_t count) : my_num_banks(num_banks),
        my_data(new (std::align_val_t(STRIDE)) T[count], [](T* p){ operator delete[](p, std::align_val_t(STRIDE)); }) {
        __TBB_ASSERT_EX(reinterpret_cast<uintptr_t>(my_data.get()) % STRIDE == 0, "interleaved_vector must be aligned to STRIDE");
        constexpr size_t page_size = 4*1024;
        for (size_t i = 0; i < count; ++i)
            my_data[i] = 0;
        
        int count_pages = (count * sizeof(T) + page_size - 1) / page_size;
        std::unique_ptr<void*[]> pages = std::make_unique<void*[]>(count_pages);
        std::unique_ptr<int[]> nodes = std::make_unique<int[]>(count_pages);
        std::unique_ptr<int[]> status = std::make_unique<int[]>(count_pages);

        char *beg_ptr = reinterpret_cast<char*>(my_data.get()),
            *end_ptr = reinterpret_cast<char*>(my_data.get() + count);
        // move_pages() has no length parameter, so must be done per page
        for (char *ptr = beg_ptr; ptr < end_ptr; ptr += page_size) {
            unsigned page_idx = (ptr - beg_ptr) / page_size;
            unsigned stride_idx = (ptr - beg_ptr) / STRIDE;
            int node = stride_idx % 2;
            pages[page_idx] = ptr;
            nodes[page_idx] = node;
        }
        long ret = move_pages(0, count_pages, pages.get(), nodes.get(), status.get(), 0);
        if (ret < 0) {
                printf("interleaved_vector: move_to_node failed errno %d\n", errno);
                perror("move_pages");
                throw std::bad_alloc();
        }
        for (int i = 0; i < count_pages; ++i)
            if (status[i] < 0) {
                printf("interleaved_vector: move_to_node failed at %u status %d\n", i, status[i]);
                throw std::bad_alloc();
            }

        char *addr = (char*)my_data.get();
        printf("interleaved_vector NUMA nodes: %d %d\n", find_numa_node_linux(addr), find_numa_node_linux(addr+4*1024));

        unsigned long nodemask = 0x3;  // nodes 0 and 1
        ret = mbind(addr, end_ptr - addr, 
                MPOL_PREFERRED /*| MPOL_F_NUMA_BALANCING*/,
                &nodemask, 65, 0);
        if (ret < 0) {
                printf("interleaved_vector: mbind failed errno %d\n", errno);
                perror("mbind");
                throw std::bad_alloc();
        }
    }
    T* get() const {
        return my_data.get();
    }
};

#elif 0

template<typename T, size_t STRIDE>
// create single address space, interleave it between NUMA nodes
struct interleaved_vector {
    // number of banks must be run-time parameter
    const std::size_t my_num_banks;
    std::unique_ptr<T[], std::function<void(T*)>> my_data;
    interleaved_vector(std::size_t num_banks, std::size_t count) :
        my_num_banks(num_banks),
        my_data(new (std::align_val_t(STRIDE)) T[count], [](T* p){ operator delete[](p, std::align_val_t(STRIDE)); }) {
        __TBB_ASSERT_EX(reinterpret_cast<uintptr_t>(my_data.get()) % STRIDE == 0, "interleaved_vector");
        for (size_t i = 0; i < count; ++i)
            my_data[i] = 0;

        const char *end_ptr = reinterpret_cast<const char*>(my_data.get() + count);
        unsigned i = 0;
        for (char *ptr = reinterpret_cast<char*>(my_data.get());
             ptr < end_ptr; ptr += STRIDE, ++i) {
#if 0
            int node = i % 2;
            void *pages[] = {ptr};
            int nodes[] = {node};
            int status[1];
            long ret = move_pages(0, 1, pages, nodes, status, 0);
            if (ret < 0) {
                printf("interleaved_vector: move_to_node failed at %u errno %d\n", i, errno);
                perror("move_pages");
                throw std::bad_alloc();
            } else if (status[0] < 0) {
                printf("interleaved_vector: move_to_node failed at %u status %d\n", i, status[0]);
                throw std::bad_alloc();
            }
#else
            int ret = tbb::detail::r1::move_to_node(ptr,
                         std::min(STRIDE, static_cast<std::size_t>(end_ptr - ptr)),
                         (ptr - reinterpret_cast<const char*>(my_data.get())) / STRIDE % my_num_banks);
            __TBB_ASSERT_EX(ret == 0 && i, "interleaved_vector: move_to_node failed");
            if (ret != 0) {
                printf("interleaved_vector: move_to_node failed at %d, errno %d\n", i, ret, errno);
                throw std::bad_alloc();
            }
#endif
        }
    }
    T* get() const {
        return my_data.get();
    }
};

#else
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
#endif

#if 0
template <typename T, size_t STRIDE, std::size_t B>
struct mdspan {
    interleaved_vector<T, STRIDE> *data;

    mdspan(interleaved_vector<T, STRIDE> *data) : data(data) {}

    T& operator()(std::size_t i, std::size_t j) const __attribute__((always_inline)) {
        return (*data)[i * B + j];
    }
};
#endif

} // namespace d1
} // namespace detail

inline namespace v1 {
using detail::d1::interleaved_vector;
#if 0
using detail::d1::mdspan;
#endif
} // namespace v1

} // namespace tbb

#endif /* __TBB_interleaved_vector_H */
