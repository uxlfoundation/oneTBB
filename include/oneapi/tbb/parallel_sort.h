/*
    Copyright (c) 2005-2021 Intel Corporation
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

#ifndef __TBB_parallel_sort_H
#define __TBB_parallel_sort_H

#include "detail/_namespace_injection.h"
#include "detail/_parallel_partition.h"
#include "parallel_for.h"
#include "blocked_range.h"
#include "profiling.h"

#include <algorithm>
#include <iterator>
#include <functional>
#include <cstddef>

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

    static constexpr std::size_t grainsize = 500;

    using iterator_traits = std::iterator_traits<RandomAccessIterator>;
    using difference_type = typename iterator_traits::difference_type;
    using value_type = typename iterator_traits::value_type;
public:
    quick_sort_range(RandomAccessIterator first, RandomAccessIterator last,
                     const Compare& comp, task_group_context& ctx)
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
        range.split_unchecked(*this);
    }

    bool empty() const { return m_first == m_last; }
    bool is_divisible() const { return (m_last - m_first) >= difference_type(grainsize); }

    void leaf_sort() const {
        std::sort(m_first, m_last, m_comp);
    }

    difference_type median_of_three(difference_type l, difference_type m, difference_type r) const {
        return m_comp(m_first[l], m_first[m]) ? ( m_comp(m_first[m], m_first[r]) ? m : ( m_comp(m_first[l], m_first[r]) ? r : l ) )
                                              : ( m_comp(m_first[r], m_first[m]) ? m : ( m_comp(m_first[r], m_first[l]) ? r : l ) );
    }

    difference_type pseudo_median_of_nine() const {
        difference_type n = m_last - m_first;
        difference_type offset = n / difference_type(8);
        return median_of_three(median_of_three(0, offset, offset * 2),
                               median_of_three(offset * 3, offset * 4, offset * 5),
                               median_of_three(offset * 6, offset * 7, n - 1));
    }

    void split_checked(quick_sort_range& target_range) {
        // Range can stop being divisible after partitioning
        // Produce an empty range in this case
        if (!is_divisible()) {
            target_range.m_first = m_last;
            target_range.m_last = m_last;
            target_range.m_leftmost = false;
        } else {
            split_unchecked(target_range);
        }
    }

    void split_unchecked(quick_sort_range& target_range) {
        __TBB_ASSERT(is_divisible(), "Range that is split is not divisible");

        // Choose the pivot
        difference_type pivot_index = pseudo_median_of_nine();

        // Place the pivot at the beginning of the range
        if (pivot_index != 0) std::iter_swap(m_first, m_first + pivot_index);

        // Move the pivot value out of the range to speed up the comparisons
        value_type pivot = std::move(*m_first);

        // Optimization for equivalent pivots
        // If the partitioned range is not a leftmost range, the element before m_first is
        // a pivot from the previous partition. If two pivots are equal, partition the range
        // using the >= pivot predicate to place all elements equivalent to pivot at their
        // final positions in the sorted range
        if (!m_leftmost && !m_comp(*(m_first - 1), pivot)) {
            auto equal_to_pivot = [&pivot, this](const value_type& x) {
                // It is enough to run the predicate once since all elements in the
                // range are guaranteed to be >= pivot by the previous partition
                return !m_comp(pivot, x);
            };

            RandomAccessIterator equal_end = parallel_partition(std::next(m_first), m_last, equal_to_pivot, m_ctx);

            // Move the pivot back to its place
            *m_first = std::move(pivot);

            // Skip the elements equal to pivot and rerun the splitting
            m_first = equal_end;
            split_checked(target_range);
        } else { // The range is a leftmost range, or pivots are different
            auto less_than_pivot = [&pivot, this](const value_type& x) {
                return m_comp(x, pivot);
            };

            auto it = parallel_partition(std::next(m_first), m_last, less_than_pivot, m_ctx);

            RandomAccessIterator pivot_pos;
            *m_first = std::move(pivot);
            if (it == std::next(m_first)) {
                pivot_pos = m_first;
            } else {
                std::iter_swap(m_first, std::prev(it));
                pivot_pos = std::prev(it);
            }

            difference_type left_size = pivot_pos - m_first;
            difference_type right_size = m_last - (pivot_pos + 1);

            // Rerun the splitting if no elements were partitioned
            if (left_size == 0) {
                m_first = pivot_pos + 1;
                m_leftmost = false;
                split_checked(target_range);
            } else if (right_size == 0) {
                m_last = pivot_pos;
                split_checked(target_range);
            } else {
                // There are elements to feed both subranges
                // Keep the smaller part in this range object
                if (left_size > right_size) {
                    target_range.m_first = m_first;
                    target_range.m_last = pivot_pos;
                    target_range.m_leftmost = m_leftmost;

                    m_first = pivot_pos + 1;
                    m_leftmost = false;
                } else {
                    target_range.m_first = pivot_pos + 1;
                    target_range.m_last = m_last;
                    target_range.m_leftmost = false;

                    m_last = pivot_pos;
                }
            }
        }
    }

}; // class quick_sort_range

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
                             task_group_context& ctx ) {
    __TBB_ASSERT(!ctx.is_group_execution_cancelled(), "Running do_parallel_quick_sort on a cancelled context");
    using range_type = quick_sort_range<RandomAccessIterator, Compare>;

    parallel_for(range_type{begin, end, comp, ctx},
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
    constexpr int min_parallel_size = 500;
    if( end > begin ) {
        if( end - begin < min_parallel_size ) {
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
