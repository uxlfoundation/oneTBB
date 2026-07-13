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

#ifndef __TBB_parallel_sort_v2_H
#define __TBB_parallel_sort_v2_H

#include "detail/_namespace_injection.h"
#include "parallel_for.h"
#include "blocked_range.h"
#include "parallel_sort.h"
#include "parallel_reduce_partition_v2.h"

#include <algorithm>
#include <iterator>
#include <functional>
#include <cstddef>

namespace tbb {
namespace detail {
namespace d1 {

template <typename RandomAccessIterator, typename Compare>
class quick_sort_range_v2 {
    RandomAccessIterator m_first;
    RandomAccessIterator m_last;
    const Compare&       m_comp;
    task_group_context&  m_ctx;
    bool                 m_leftmost;

    using iterator_traits = std::iterator_traits<RandomAccessIterator>;
    using difference_type = typename iterator_traits::difference_type;
    using value_type = typename iterator_traits::value_type;

public:
    quick_sort_range_v2(RandomAccessIterator first, RandomAccessIterator last, const Compare& comp, task_group_context& ctx)
        : m_first(first)
        , m_last(last)
        , m_comp(comp)
        , m_ctx(ctx)
        , m_leftmost(true)
    {}

    quick_sort_range_v2(quick_sort_range_v2& range, split)
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

    void split_range_checked(quick_sort_range_v2& range) {
        // Range can stop being divisible after partitioning, produce an empty range in this case
        if (!is_divisible()) {
            range.m_first = m_last;
            range.m_last = m_last;
            range.m_leftmost = false;
        } else {
            split_range_unchecked(range);
        }
    }

    void split_range_unchecked(quick_sort_range_v2& range) {
        __TBB_ASSERT(is_divisible(), "Range that is split is not divisible");

        // Choose the pivot
        difference_type pivot_index = pseudo_median_of_nine(m_first, m_last - m_first, m_comp);

        // Place the pivot at the beginning of the range
        if (pivot_index != 0) std::iter_swap(m_first, m_first + pivot_index);

        // Move the pivot value out of the range to speed up the comparisons
        value_type pivot = *m_first;

        // Optimization for the equivalent pivots
        // TODO: link?
        // If the partitioned range is not the leftmost range, the element before m_first is a pivot from the previous partition
        // If the pivot is the same, partition the range first using the >= pivot predicate to place all elements equal to the pivot
        // at their final positions in the sorted range
        if (!m_leftmost && !m_comp(*(m_first - 1), *m_first)) {
            auto equal_to_pivot = [&](const value_type& x) {
                // It is enough to run the predicate once since all elements in the range are guaranteed to be >= pivot by the previous partition
                return !m_comp(pivot, x);
            };

            m_first = parallel_reduce_partition(m_first, m_last, equal_to_pivot, m_ctx);
            // Rerun the splitting with avoiding the elements equal to current pivot
            split_range_checked(range);
        } else { // The range is a leftmost range, or a pivot is difference from the previous partition
            auto less_then_pivot = [&](const value_type& x) {
                return m_comp(x, pivot);
            };

            auto it = parallel_reduce_partition(std::next(m_first), m_last, less_then_pivot, m_ctx);

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

template <typename RandomAccessIterator, typename Compare>
void do_parallel_quick_sort_v2(RandomAccessIterator first, RandomAccessIterator last, const Compare& comp,
                               task_group_context& ctx)
{
    __TBB_ASSERT(!ctx.is_group_execution_cancelled(), "Running do_parallel_quick_sort_v2 on a cancelled context");
    using range_type = quick_sort_range_v2<RandomAccessIterator, Compare>;
    parallel_for(range_type(first, last, comp, ctx),
                 [](const range_type& range) { range.leaf_sort(); },
                 ctx);
}

template <typename RandomAccessIterator, typename Compare>
void parallel_quick_sort_v2(RandomAccessIterator first, RandomAccessIterator last, const Compare& comp) {
    task_group_context my_context(PARALLEL_SORT);
    constexpr int serial_cutoff = 9;

    __TBB_ASSERT( first + serial_cutoff < last, "min_parallel_size is smaller than serial cutoff?" );
    RandomAccessIterator k = first;
    for( ; k != first + serial_cutoff; ++k ) {
        if( comp(*(k + 1), *k) ) {
            do_parallel_quick_sort_v2(first, last, comp, my_context);
            return;
        }
    }

    // Check if input range is already sorted
    parallel_for(blocked_range<RandomAccessIterator>(k + 1, last),
                 quick_sort_pretest_body<RandomAccessIterator, Compare>(comp, my_context),
                 my_context);

    if( my_context.is_group_execution_cancelled() ) {
        my_context.reset();
        do_parallel_quick_sort_v2(first, last, comp, my_context);
    }
}

} // namespace d1
} // namespace detail
} // namespace tbb

#endif // __TBB_parallel_sort_v2_H
