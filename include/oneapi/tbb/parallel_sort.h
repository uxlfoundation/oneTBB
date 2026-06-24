/*
    Copyright (c) 2005-2021 Intel Corporation

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

#ifndef __TBB_parallel_sort_H
#define __TBB_parallel_sort_H

#include "detail/_namespace_injection.h"
#include "detail/_template_helpers.h"
#include "parallel_for.h"
#include "blocked_range.h"
#include "profiling.h"

#include <algorithm>
#include <iterator>
#include <functional>
#include <cstddef>
#include <atomic>
#include <vector>

namespace tbb {
namespace detail {
#if __TBB_CPP20_CONCEPTS_PRESENT
inline namespace d0 {

// TODO: consider using std::strict_weak_order concept
template <typename Compare, typename Iterator>
concept compare = requires( const std::remove_reference_t<Compare>& comp, typename std::iterator_traits<Iterator>::reference value ) {
    // Forward via iterator_traits::reference
    { comp(typename std::iterator_traits<Iterator>::reference(value),
           typename std::iterator_traits<Iterator>::reference(value)) } -> std::convertible_to<bool>;
};

// Inspired by std::__PartiallyOrderedWith exposition only concept
template <typename T>
concept less_than_comparable = requires( const std::remove_reference_t<T>& lhs,
                                         const std::remove_reference_t<T>& rhs ) {
    { lhs < rhs } -> boolean_testable;
};

} // namespace d0
#endif // __TBB_CPP20_CONCEPTS_PRESENT
namespace d1 {

inline constexpr std::size_t serial_sort_cutoff() {
    return 2000;
}

// TODO: should the block size depend on the problem size?
template <typename DifferenceType>
constexpr std::size_t partition_block_size(DifferenceType /*problem_size*/) {
    return 1024;
}

inline constexpr std::size_t serial_partition_cutoff(std::size_t block_size) {
    return 4 * block_size;
}

inline constexpr std::size_t partition_spawn_threshold(std::size_t block_size) {
    return 2 * block_size;
}

template <typename DifferenceType>
struct BlockEntry {
    DifferenceType block_begin;
    DifferenceType chunk_begin;
    bool           is_dirty;
};

enum class side {
    left, right
};

template <side S>
struct side_tag {};

template <typename DirtyBlockType, typename DifferenceType>
void identify_dirty_range(const DirtyBlockType& dirty_block, DifferenceType block_size, DifferenceType dst_block_begin,
                          DifferenceType& begin, DifferenceType& end, DifferenceType& dst_begin,
                          side_tag<side::left>)
{
    // Left side - dirty block is the block suffix
    begin = dirty_block.chunk_begin;
    end = dirty_block.block_begin + block_size;
    dst_begin = dst_block_begin + (dirty_block.chunk_begin - dirty_block.block_begin);
}

template <typename DirtyBlockType, typename DifferenceType>
void identify_dirty_range(const DirtyBlockType& dirty_block, DifferenceType, DifferenceType dst_block_begin,
                          DifferenceType& begin, DifferenceType& end, DifferenceType& dst_begin,
                          side_tag<side::right>)
{
    // Right side - dirty block is the block prefix
    begin = dirty_block.block_begin;
    end = dirty_block.chunk_begin;
    dst_begin = dst_block_begin;
}

template <typename DirtyBlocksType, typename DifferenceType>
bool slot_is_dirty(const DirtyBlocksType& dirty_blocks, DifferenceType slot) {
    for (auto& dirty_block : dirty_blocks) {
        if (dirty_block.block_begin == slot) return true;
    }
    return false;
}

template <side S, typename RandomAccessIterator, typename DifferenceType, typename DirtyBlocksType>
void relocate_side(RandomAccessIterator first, DifferenceType block_size,
                   DifferenceType slab_begin, DifferenceType slab_end,
                   const DirtyBlocksType& dirty_blocks)
{
    DifferenceType slot = slab_begin;

    for (auto& dirty_block : dirty_blocks) {
        if (slab_begin <= dirty_block.block_begin && dirty_block.block_begin < slab_end) {
            // Dirty block already in the slab - keep it as-is
            continue;
        }

        // Advance to next neutralized block in the slab
        while (slot < slab_end && slot_is_dirty(dirty_blocks, slot)) {
            slot += block_size;
        }

        DifferenceType begin, end, dst_begin;
        identify_dirty_range(dirty_block, block_size, slot, begin, end, dst_begin, side_tag<S>{});

        // Move dirty block to the slot in the slab
        std::swap_ranges(first + begin, first + end, first + dst_begin);
        slot += block_size;
    }
}

template <typename RandomAccessIterator, typename Predicate, typename Branchless>
RandomAccessIterator parallel_partition(RandomAccessIterator first, RandomAccessIterator last, Predicate pred,
                                        task_group_context& ctx, Branchless, std::size_t block_size = 0);

template <typename RandomAccessIterator, typename Predicate, typename DifferenceType,
          typename BlockMapType, typename IsBranchless>
RandomAccessIterator finalize_parallel_partition(RandomAccessIterator first, Predicate pred, DifferenceType block_size,
                                                 task_group_context& ctx, DifferenceType remainder_begin, DifferenceType remainder_end,
                                                 BlockMapType& left_block_map, BlockMapType& right_block_map,
                                                 IsBranchless is_branchless)
{
    // Some amount of dirty blocks is left after the first stage of parallel_partition
    // Also, some elements in the middle are left untouched by any participant
    // finalize stage moves these dirty blocks closer to the middle to form a slab, and re-runs the partition on that slab
    // Example: after the first stage we have (Nx - neutralized block, DLx - dirty left block, DRx - dirty right block, MID - untouched middle)
    // N1 N2 DL1 DL2 N3 DL3 MID N4 N5 DR1 N6 DR2 - three dirty left blocks, two dirty right blocks
    // The target slab is 3 dirty blocks on the left from the middle and 2 dirty blocks on the right ([] - boundaries of the slab)
    // N1 N2 DL1 [ DL2 N3 DL3 MID N4 N5 ] DR1 N6 DR2
    // If the dirty block is within the slab - it is kept as is
    // If the dirty block is outside the slab - find the neutralized block withing the slab and swap them:
    // N1 N2 N3 [DL2 DL1 DL3 MID DR1 DR2 ] N4 N6 N5

    // Split the dirty blocks from other blocks
    BlockMapType left_dirty_blocks, right_dirty_blocks;
    left_dirty_blocks.reserve(left_block_map.size());
    right_dirty_blocks.reserve(right_block_map.size());

    for (auto& block_entry : left_block_map) {
        if (block_entry.is_dirty) {
            left_dirty_blocks.emplace_back(block_entry);
        }
    }

    for (auto& block_entry : right_block_map) {
        if (block_entry.is_dirty) {
            right_dirty_blocks.emplace_back(block_entry);
        }
    }

    // Identify the boundaries of the slab
    DifferenceType left_slab_end = remainder_begin;
    DifferenceType left_slab_begin = left_slab_end - (left_dirty_blocks.size() * block_size);

    DifferenceType right_slab_begin = remainder_end;
    DifferenceType right_slab_end = right_slab_begin + (right_dirty_blocks.size() * block_size);

    relocate_side<side::left >(first, block_size, left_slab_begin, left_slab_end, left_dirty_blocks);
    relocate_side<side::right>(first, block_size, right_slab_begin, right_slab_end, right_dirty_blocks);

    std::size_t next_level_block_size = std::max<std::size_t>(block_size / 2, 128);
    return parallel_partition(first + left_slab_begin, first + right_slab_end, pred, ctx,
                              is_branchless, next_level_block_size);
}

template <typename DifferenceType>
bool try_get_left_block(DifferenceType& chunk_begin, DifferenceType block_size,
                        std::atomic<DifferenceType>& g_distance, std::atomic<DifferenceType>& g_head)
{
    auto remainder = g_distance.fetch_sub(block_size, std::memory_order_relaxed);
    if (remainder < block_size) {
        g_distance.fetch_add(block_size, std::memory_order_relaxed);
        return false;
    }
    chunk_begin = g_head.fetch_add(block_size, std::memory_order_relaxed);
    return true;
}

template <typename DifferenceType>
bool try_get_right_block(DifferenceType& chunk_begin, DifferenceType block_size,
                         std::atomic<DifferenceType>& g_distance, std::atomic<DifferenceType>& g_tail)
{
    auto remainder = g_distance.fetch_sub(block_size, std::memory_order_relaxed);
    if (remainder < block_size) {
        g_distance.fetch_add(block_size, std::memory_order_relaxed);
        return false;
    }
    auto chunk_end = g_tail.fetch_sub(block_size, std::memory_order_relaxed);
    chunk_begin = chunk_end - block_size;
    return true;
}

template <typename DifferenceType, typename Predicate, typename RandomAccessIterator, typename BlockMap>
void parallel_partition_task_body(std::size_t index, DifferenceType block_size, Predicate& pred,
                                  RandomAccessIterator g_begin, std::atomic<DifferenceType>& g_distance,
                                  std::atomic<DifferenceType>& g_head, std::atomic<DifferenceType>& g_tail,
                                  BlockMap& g_left_block_map, BlockMap& g_right_block_map,
                                  /*branchless = */std::true_type)
{
    DifferenceType left_begin = 0, right_begin = 0;
    bool have_left_block = try_get_left_block(left_begin, block_size, g_distance, g_head);
    bool have_right_block = try_get_right_block(right_begin, block_size, g_distance, g_tail);

    std::vector<DifferenceType> left_offsets(block_size);
    std::vector<DifferenceType> right_offsets(block_size);

    DifferenceType n_left_start = 0, n_left_count = 0;
    DifferenceType n_right_start = 0, n_right_count = 0;

    DifferenceType left_index = left_begin;
    DifferenceType right_index = right_begin;

    while (have_left_block && have_right_block) {
        if (n_left_start == n_left_count) {
            n_left_start = n_left_count = 0;
            for (DifferenceType i = 0; i < block_size; ++i) {
                left_offsets[n_left_count] = i;
                n_left_count += DifferenceType(!pred(g_begin[left_index + i]));
            }
        }

        if (n_right_start == n_right_count) {
            n_right_start = n_right_count = 0;
            for (DifferenceType i = 0; i < block_size; ++i) {
                right_offsets[n_right_count] = i;
                n_right_count += DifferenceType(pred(g_begin[right_index + i]));
            }
        }

        DifferenceType m = std::min<DifferenceType>(n_left_count - n_left_start, n_right_count - n_right_start);
        for (DifferenceType k = 0; k < m; ++k) {
            std::iter_swap(g_begin + (left_index + left_offsets[n_left_start + k]),
                           g_begin + (right_index + right_offsets[n_right_start + k]));
        }

        n_left_start += m;
        n_right_start += m;

        if (n_left_start == n_left_count) {
            have_left_block = try_get_left_block(left_begin, block_size, g_distance, g_head);
            if (have_left_block) left_index = left_begin;
        }

        if (n_right_start == n_right_count) {
            have_right_block = try_get_right_block(right_begin, block_size, g_distance, g_tail);
            if (have_right_block) right_index = right_begin;
        }
    }

    // Store dirty blocks
    // TODO: comment why tiny partition is needed
    if (have_left_block) {
        DifferenceType left_end = left_begin + block_size;
        DifferenceType pp = DifferenceType(std::partition(g_begin + left_begin, g_begin + left_end, pred) - g_begin);
        g_left_block_map[index] = BlockEntry<DifferenceType>{left_begin, pp, true};
    }
    if (have_right_block) {
        DifferenceType right_end = right_begin + block_size;
        DifferenceType pp = DifferenceType(std::partition(g_begin + right_begin, g_begin + right_end, pred) - g_begin);
        g_right_block_map[index] = BlockEntry<DifferenceType>{right_begin, pp, true};
    }
}                                    

template <typename DifferenceType, typename Predicate, typename RandomAccessIterator, typename BlockMap>
void parallel_partition_task_body(std::size_t index, DifferenceType block_size, Predicate& pred,
                                  RandomAccessIterator g_begin, std::atomic<DifferenceType>& g_distance,
                                  std::atomic<DifferenceType>& g_head, std::atomic<DifferenceType>& g_tail,
                                  BlockMap& g_left_block_map, BlockMap& g_right_block_map,
                                  /*branchless = */std::false_type)
{
    DifferenceType left_begin = 0, right_begin = 0;
    bool have_left_block = try_get_left_block(left_begin, block_size, g_distance, g_head);
    bool have_right_block = try_get_right_block(right_begin, block_size, g_distance, g_tail);

    DifferenceType left_index = left_begin;
    DifferenceType left_end = left_begin + block_size;
    DifferenceType right_index = right_begin + block_size;

    while (have_left_block && have_right_block) {
        while (left_index < left_end && pred(g_begin[left_index])) {
            ++left_index;
        }

        while (right_index > right_begin && !pred(g_begin[right_index - 1])) {
            --right_index;
        }

        if (left_index < left_end && right_index > right_begin) {
            std::iter_swap(g_begin + left_index, g_begin + (right_index - 1));
            ++left_index;
            --right_index;
            continue;
        }

        // Left block fully processed
        if (left_index == left_end) {
            have_left_block = try_get_left_block(left_begin, block_size, g_distance, g_head);
            if (have_left_block) {
                left_index = left_begin;
                left_end = left_begin + block_size;
            }
        }

        // Right block fully processed
        if (right_index == right_begin) {
            have_right_block = try_get_right_block(right_begin, block_size, g_distance, g_tail);
            if (have_right_block) {
                right_index = right_begin + block_size;
            }
        }
    }

    // Store dirty blocks
    if (have_left_block) {
        g_left_block_map[index] = BlockEntry<DifferenceType>{left_begin, left_index, true};
    }
    if (have_right_block) {
        g_right_block_map[index] = BlockEntry<DifferenceType>{right_begin, right_index, true};
    }
}

template <typename RandomAccessIterator, typename Predicate, typename IsBranchless>
class ParallelPartitionTask : public task {
    using difference_type = typename std::iterator_traits<RandomAccessIterator>::difference_type;
public:
    using block_entry_type = BlockEntry<difference_type>;
private:
    const std::size_t      m_index;
    const difference_type  m_block_size;
    Predicate&             m_pred;
    wait_context&          m_wait_ctx; // TODO: try thread reference vertex instead
    task_group_context&    m_ctx;
    small_object_allocator m_allocator;

    const RandomAccessIterator     g_begin;
    std::atomic<difference_type>&  g_distance;
    std::atomic<difference_type>&  g_head;
    std::atomic<difference_type>&  g_tail;
    std::vector<block_entry_type>& g_left_block_map;
    std::vector<block_entry_type>& g_right_block_map;
public:

    ParallelPartitionTask(std::size_t index, difference_type block_size, Predicate& pred,
                          wait_context& wait_ctx, task_group_context& ctx, small_object_allocator& allocator,
                          RandomAccessIterator global_begin, std::atomic<difference_type>& global_distance,
                          std::atomic<difference_type>& global_head, std::atomic<difference_type>& global_tail,
                          std::vector<block_entry_type>& global_left_block_map,
                          std::vector<block_entry_type>& global_right_block_map)
        : m_index(index)
        , m_block_size(block_size)
        , m_pred(pred)
        , m_wait_ctx(wait_ctx)
        , m_ctx(ctx)
        , m_allocator(allocator)
        , g_begin(global_begin)
        , g_distance(global_distance)
        , g_head(global_head)
        , g_tail(global_tail)
        , g_left_block_map(global_left_block_map)
        , g_right_block_map(global_right_block_map)
    {
        m_wait_ctx.reserve();
    }

    ~ParallelPartitionTask() {
        m_wait_ctx.release();
    }

    task* execute(execution_data& ed) override {
        // Spawn new participant if there is enough work to share
        if (m_index + 1 < g_left_block_map.size() &&
            g_distance.load(std::memory_order_relaxed) > difference_type(partition_spawn_threshold(m_block_size))) {
            spawn(*m_allocator.new_object<ParallelPartitionTask>(m_index + 1, m_block_size, m_pred, m_wait_ctx,
                                                                 m_ctx, m_allocator,
                                                                 g_begin, g_distance, g_head, g_tail,
                                                                 g_left_block_map, g_right_block_map),
                  m_ctx);
        }

        parallel_partition_task_body(m_index, m_block_size, m_pred, g_begin, g_distance, g_head, g_tail,
                                     g_left_block_map, g_right_block_map,
                                     IsBranchless{});

        m_allocator.delete_object(this, ed);
        return nullptr;
    }

    task* cancel(execution_data& ed) override {
        m_allocator.delete_object(this, ed);
        return nullptr;
    }
};

template <typename RandomAccessIterator, typename Predicate, typename IsBranchless>
RandomAccessIterator parallel_partition(RandomAccessIterator first, RandomAccessIterator last, Predicate pred,
                                        task_group_context& ctx, IsBranchless is_branchless, std::size_t block_size)
{
    using iterator_traits = std::iterator_traits<RandomAccessIterator>;
    using difference_type = typename iterator_traits::difference_type;

    const difference_type n = std::distance(first, last);
    if (block_size == 0) block_size = partition_block_size(n);

    using task_type = ParallelPartitionTask<RandomAccessIterator, Predicate, IsBranchless>;
    using block_entry_type = typename task_type::block_entry_type;

    const std::size_t max_participants = std::min<std::size_t>(this_task_arena::max_concurrency(),
                                                               std::size_t(n / serial_partition_cutoff(block_size)));

    if (n < difference_type(serial_partition_cutoff(block_size)) || max_participants <= 1) {
        return std::partition(first, last, pred);
    }

    alignas(max_nfs_size) std::atomic<difference_type> g_distance(n);
    alignas(max_nfs_size) std::atomic<difference_type> g_head(0);
    alignas(max_nfs_size) std::atomic<difference_type> g_tail(n);

    std::vector<block_entry_type> g_left_block_map (max_participants, block_entry_type{0, 0, false});
    std::vector<block_entry_type> g_right_block_map(max_participants, block_entry_type{0, 0, false});

    small_object_allocator allocator;
    wait_context wait_ctx(0); // TODO: consider using thread reference_vertex in tasks

    // Spawn the second participant
    spawn(*allocator.new_object<task_type>(/*index = */1, difference_type(block_size), pred, wait_ctx, ctx, allocator,
                                           first, g_distance, g_head, g_tail, g_left_block_map, g_right_block_map), ctx);

    // Join the partition as a first participant
    parallel_partition_task_body(/*index = */0, difference_type(block_size), pred,
                                 first, g_distance, g_head, g_tail, g_left_block_map, g_right_block_map,
                                 is_branchless);

    // Wait for all participants to complete processing the blocks
    wait(wait_ctx, ctx);

    return finalize_parallel_partition(first, pred, difference_type(block_size), ctx,
                                       g_head.load(std::memory_order_relaxed), g_tail.load(std::memory_order_relaxed),
                                       g_left_block_map, g_right_block_map,
                                       is_branchless);
}

template <typename Compare>
struct is_default_compare : std::false_type {};

template <typename T>
struct is_default_compare<std::less<T>> : std::true_type {};

template <typename T>
struct is_default_compare<std::greater<T>> : std::true_type {};

template <typename T, typename Compare>
struct use_branchless_partition
    : tbb::detail::conjunction<std::is_arithmetic<T>, is_default_compare<Compare>> {};

//! Range used in quicksort to split elements into subranges based on a value.
/** The split operation selects a splitter and places all elements less than or equal
    to the value in the first range and the remaining elements in the second range.
    @ingroup algorithms */
template <typename RandomAccessIterator, typename Compare>
class quick_sort_range {
    RandomAccessIterator m_first;
    RandomAccessIterator m_last;
    const Compare&       m_comp;
    task_group_context&  m_ctx;
    bool                 m_leftmost;

    using iterator_traits = std::iterator_traits<RandomAccessIterator>;
    using difference_type = typename iterator_traits::difference_type;
    using value_type = typename iterator_traits::value_type;

public:
    quick_sort_range(RandomAccessIterator first, RandomAccessIterator last, const Compare& comp, task_group_context& ctx)
        : m_first(first)
        , m_last(last)
        , m_comp(comp)
        , m_ctx(ctx)
        , m_leftmost(true)
    {}

    quick_sort_range(quick_sort_range& range, split)
        : m_comp(range.m_comp)
        , m_ctx(range.m_ctx)
    {
        range.split_range_unchecked(*this);
    }

    bool empty() const { return m_first == m_last; }
    bool is_divisible() const { return (m_last - m_first) >= difference_type(serial_sort_cutoff()); }

    void leaf_sort() const {
        std::sort(m_first, m_last, m_comp);
    }

    static difference_type median_of_three( const RandomAccessIterator& array, difference_type l, difference_type m, difference_type r,
                                            const Compare& comp)
    {
        return comp(array[l], array[m]) ? ( comp(array[m], array[r]) ? m : ( comp(array[l], array[r]) ? r : l ) )
                                        : ( comp(array[r], array[m]) ? m : ( comp(array[r], array[l]) ? r : l ) );
    }

    static difference_type pseudo_median_of_nine( const RandomAccessIterator& array, difference_type n, const Compare& comp ) {
        std::size_t offset = n / 8u;
        return median_of_three(array,
                               median_of_three(array, 0 , offset, offset * 2, comp),
                               median_of_three(array, offset * 3, offset * 4, offset * 5, comp),
                               median_of_three(array, offset * 6, offset * 7, n - 1, comp),
                               comp);

    }

    void split_range_checked(quick_sort_range& range) {
        // Range can stop being divisible after partitioning, produce an empty range in this case
        if (!is_divisible()) {
            range.m_first = m_last;
            range.m_last = m_last;
            range.m_leftmost = false;
        } else {
            split_range_unchecked(range);
        }
    }

    template <typename Predicate>
    static RandomAccessIterator do_parallel_partition(RandomAccessIterator first, RandomAccessIterator last,
                                                      Predicate& pred, task_group_context& ctx)
    {
        return parallel_partition(first, last, pred, ctx, use_branchless_partition<value_type, Compare>{});
    }

    void split_range_unchecked(quick_sort_range& range) {
        __TBB_ASSERT(is_divisible(), "Range that is split is not divisible");

        // Choose the pivot
        difference_type pivot_index = pseudo_median_of_nine(m_first, m_last - m_first, m_comp);

        // Place the pivot at the beginning of the range
        if (pivot_index != 0) std::iter_swap(m_first, m_first + pivot_index);

        // Move the pivot value out of the range to speed up the comparisons
        value_type pivot = *m_first; // TODO: std::move

        // Optimization for the equivalent pivots
        // TODO: link?
        // If the partitioned range is not a leftmost range, the element before m_first is a pivot from the previous partition
        // Partition the range using the >= predicate to place all elements equivalent to the pivot at their final positions in the sorted range
        if (!m_leftmost && !m_comp(*(m_first - 1), *m_first)) {
            auto equal_to_pivot = [&](const value_type& x) {
                // It is enough to run the predicate once since all the elements in the range are guaranteed to be >= pivot by the previous partition
                return !m_comp(pivot, x);
            };

            m_first = do_parallel_partition(m_first, m_last, equal_to_pivot, m_ctx);
            // Rerun the splitting with avoiding the elements equal to current pivot
            split_range_checked(range);
        } else { // The range is a leftmost range, or a pivot is different from the previous partition
            auto less_then_pivot = [&](const value_type& x) {
                return m_comp(x, pivot);
            };

            auto it = do_parallel_partition(std::next(m_first), m_last, less_then_pivot, m_ctx);

            // Put the pivot on it's correct place in the partitioned range
            RandomAccessIterator pivot_pos;
            if (it == std::next(m_first)) {
                pivot_pos = m_first;
            } else {
                std::iter_swap(std::prev(it), m_first);
                pivot_pos = std::prev(it);
            }

            difference_type left_size = pivot_pos - m_first;
            difference_type right_size = m_last - (pivot_pos + 1);

            // Rerun the splitting if no elements were partitioned
            if (left_size == 0) {
                m_first = pivot_pos + 1;
                m_leftmost = false;
                split_range_checked(range);
                return;
            }

            if (right_size == 0) {
                m_last = pivot_pos;
                split_range_checked(range);
                return;
            }

            // There are enough elements to feed both ranges, keep the smaller part in this range object
            if (left_size > right_size) {
                range.m_first = m_first;
                range.m_last = pivot_pos;
                range.m_leftmost = m_leftmost;

                m_first = pivot_pos + 1;
                m_leftmost = false;
            } else {
                range.m_first = pivot_pos + 1;
                range.m_last = m_last;
                range.m_leftmost = false;

                m_last = pivot_pos;
            }
        }
    }
};

//! Body class used to test if elements in a range are presorted
/** @ingroup algorithms */
template<typename RandomAccessIterator, typename Compare>
class quick_sort_pretest_body {
    const Compare& comp;
    task_group_context& context;

public:
    quick_sort_pretest_body() = default;
    quick_sort_pretest_body( const quick_sort_pretest_body& ) = default;
    void operator=( const quick_sort_pretest_body& ) = delete;

    quick_sort_pretest_body( const Compare& _comp, task_group_context& _context ) : comp(_comp), context(_context) {}

    void operator()( const blocked_range<RandomAccessIterator>& range ) const {
        RandomAccessIterator my_end = range.end();

        int i = 0;
        //TODO: consider using std::is_sorted() for each 64 iterations (requires performance measurements)
        for( RandomAccessIterator k = range.begin(); k != my_end; ++k, ++i ) {
            if( i % 64 == 0 && context.is_group_execution_cancelled() ) break;

            // The k - 1 is never out-of-range because the first chunk starts at begin+serial_cutoff+1
            if( comp(*(k), *(k - 1)) ) {
                context.cancel_group_execution();
                break;
            }
        }
    }
};

//! Method to perform parallel_for based quick sort.
/** @ingroup algorithms */
template<typename RandomAccessIterator, typename Compare>
void do_parallel_quick_sort( RandomAccessIterator begin, RandomAccessIterator end, const Compare& comp,
                             task_group_context& ctx )
{
    __TBB_ASSERT(!ctx.is_group_execution_cancelled(), "Running do_parallel_quick_sort on a cancelled context");
    using range_type = quick_sort_range<RandomAccessIterator, Compare>;
    parallel_for(range_type(begin, end, comp, ctx),
                 [](const range_type& range) { range.leaf_sort(); },
                 auto_partitioner(), ctx);
}

//! Wrapper method to initiate the sort by calling parallel_for.
/** @ingroup algorithms */
template<typename RandomAccessIterator, typename Compare>
void parallel_quick_sort( RandomAccessIterator begin, RandomAccessIterator end, const Compare& comp ) {
    task_group_context my_context(PARALLEL_SORT);
    constexpr int serial_cutoff = 9;

    __TBB_ASSERT( begin + serial_cutoff < end, "min_parallel_size is smaller than serial cutoff?" );
    RandomAccessIterator k = begin;
    for( ; k != begin + serial_cutoff; ++k ) {
        if( comp(*(k + 1), *k) ) {
            do_parallel_quick_sort(begin, end, comp, my_context);
            return;
        }
    }

    // Check is input range already sorted
    parallel_for(blocked_range<RandomAccessIterator>(k + 1, end),
                 quick_sort_pretest_body<RandomAccessIterator, Compare>(comp, my_context),
                 auto_partitioner(),
                 my_context);

    if( my_context.is_group_execution_cancelled() ) {
        my_context.reset();
        do_parallel_quick_sort(begin, end, comp, my_context);
    }
}

/** \page parallel_sort_iter_req Requirements on iterators for parallel_sort
    Requirements on the iterator type \c It and its value type \c T for \c parallel_sort:

    - \code void iter_swap( It a, It b ) \endcode Swaps the values of the elements the given
    iterators \c a and \c b are pointing to. \c It should be a random access iterator.

    - \code bool Compare::operator()( const T& x, const T& y ) \endcode True if x comes before y;
**/

/** \name parallel_sort
    See also requirements on \ref parallel_sort_iter_req "iterators for parallel_sort". **/
//@{

#if __TBB_CPP20_CONCEPTS_PRESENT
template<typename It>
using iter_value_type = typename std::iterator_traits<It>::value_type;

template<typename Range>
using range_value_type = typename std::iterator_traits<range_iterator_type<Range>>::value_type;
#endif

//! Sorts the data in [begin,end) using the given comparator
/** The compare function object is used for all comparisons between elements during sorting.
    The compare object must define a bool operator() function.
    @ingroup algorithms **/
template<typename RandomAccessIterator, typename Compare>
    __TBB_requires(std::random_access_iterator<RandomAccessIterator> &&
                   compare<Compare, RandomAccessIterator> &&
                   std::movable<iter_value_type<RandomAccessIterator>>)
void parallel_sort( RandomAccessIterator begin, RandomAccessIterator end, const Compare& comp ) {
    using difference_type = typename std::iterator_traits<RandomAccessIterator>::difference_type;

    if( end > begin ) {
        if( end - begin < difference_type(serial_sort_cutoff()) ) {
            std::sort(begin, end, comp);
        } else {
            parallel_quick_sort(begin, end, comp);
        }
    }
}

//! Sorts the data in [begin,end) with a default comparator \c std::less
/** @ingroup algorithms **/
template<typename RandomAccessIterator>
    __TBB_requires(std::random_access_iterator<RandomAccessIterator> &&
                   less_than_comparable<iter_value_type<RandomAccessIterator>> &&
                   std::movable<iter_value_type<RandomAccessIterator>>)
void parallel_sort( RandomAccessIterator begin, RandomAccessIterator end ) {
    parallel_sort(begin, end, std::less<typename std::iterator_traits<RandomAccessIterator>::value_type>());
}

//! Sorts the data in rng using the given comparator
/** @ingroup algorithms **/
template<typename Range, typename Compare>
    __TBB_requires(container_based_sequence<Range, std::random_access_iterator_tag> &&
                   compare<Compare, range_iterator_type<Range>> &&
                   std::movable<range_value_type<Range>>)
void parallel_sort( Range&& rng, const Compare& comp ) {
    parallel_sort(std::begin(rng), std::end(rng), comp);
}

//! Sorts the data in rng with a default comparator \c std::less
/** @ingroup algorithms **/
template<typename Range>
    __TBB_requires(container_based_sequence<Range, std::random_access_iterator_tag> &&
                   less_than_comparable<range_value_type<Range>> &&
                   std::movable<range_value_type<Range>>)
void parallel_sort( Range&& rng ) {
    parallel_sort(std::begin(rng), std::end(rng));
}
//@}

} // namespace d1
} // namespace detail

inline namespace v1 {
    using detail::d1::parallel_sort;
} // namespace v1
} // namespace tbb

#endif /*__TBB_parallel_sort_H*/
