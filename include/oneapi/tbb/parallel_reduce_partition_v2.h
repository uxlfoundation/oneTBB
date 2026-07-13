/*
    Copyright (c) 2026 Intel Corporation

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

#ifndef PARALLEL_REDUCE_PARTITION_V2_H
#define PARALLEL_REDUCE_PARTITION_V2_H

#include "parallel_reduce.h"
#include "parallel_for.h"
#include "blocked_range.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace tbb {
namespace detail {
namespace d1 {

template <typename RandomAccessIterator, typename Predicate>
class parallel_partition_body {
    using iterator_traits = std::iterator_traits<RandomAccessIterator>;
    using difference_type = typename iterator_traits::difference_type;

    difference_type      m_left_chunk_begin;
    difference_type      m_left_chunk_end;
    difference_type      m_right_chunk_begin;
    difference_type      m_right_chunk_end;

    difference_type      m_false_leftover;
    difference_type      m_true_leftover;

    Predicate&           m_pred;
    task_group_context&  m_ctx;
    RandomAccessIterator g_first;
    difference_type      g_problem_size;
public:
    bool has_false_leftover() const { return m_false_leftover != m_left_chunk_end; }
    bool has_true_leftover() const { return m_true_leftover != m_right_chunk_begin; }

    difference_type get_true_leftover() const { return m_true_leftover; }
    difference_type get_false_leftover() const { return m_false_leftover; }

    parallel_partition_body(RandomAccessIterator first, difference_type problem_size, Predicate& pred,
                            task_group_context& ctx)
        : m_left_chunk_begin(0), m_left_chunk_end(0)
        , m_right_chunk_begin(0), m_right_chunk_end(0)
        , m_false_leftover(0), m_true_leftover(0) // Filled by operator()
        , m_pred(pred), m_ctx(ctx)
        , g_first(first), g_problem_size(problem_size)
    {}

    parallel_partition_body(parallel_partition_body& src, tbb::split)
        : m_left_chunk_begin(0), m_left_chunk_end(0)
        , m_right_chunk_begin(0), m_right_chunk_end(0)
        , m_false_leftover(0), m_true_leftover(0) // Filled by operator()
        , m_pred(src.m_pred), m_ctx(src.m_ctx)
        , g_first(src.g_first), g_problem_size(src.g_problem_size)
    {}

    void operator()(const tbb::blocked_range<difference_type>& range) {
        difference_type left_chunk_begin = range.begin();
        difference_type left_chunk_end = range.end();
        difference_type right_chunk_begin = g_problem_size - left_chunk_end;
        difference_type right_chunk_end = g_problem_size - left_chunk_begin;

        // Partition the pair of chunks
        difference_type left_index = left_chunk_begin;
        difference_type right_index = right_chunk_end;

        while (true) {
            while (left_index < left_chunk_end && m_pred(g_first[left_index])) {
                ++left_index;
            }

            while (right_index > right_chunk_begin && !m_pred(g_first[right_index - 1])) {
                --right_index;
            }

            if (left_index < left_chunk_end && right_index > right_chunk_begin) {
                std::iter_swap(g_first + left_index, g_first + (right_index - 1));
                ++left_index;
                --right_index;
                continue;
            } else {
                break;
            }
        }

        __TBB_ASSERT((left_index == left_chunk_end) || (right_index == right_chunk_begin), nullptr);

        difference_type false_leftover = left_chunk_end;
        difference_type true_leftover = right_chunk_begin;

        if (left_index != left_chunk_end) {
            auto it = std::partition(g_first + left_index, g_first + left_chunk_end, m_pred);
            false_leftover = it - g_first;
        } else {
            auto it = std::partition(g_first + right_chunk_begin, g_first + right_index, m_pred);
            true_leftover = it - g_first;
        }

        // Store the result
        if (m_left_chunk_begin == m_left_chunk_end) {
            // First sub-range assigned to this body
            m_left_chunk_begin = left_chunk_begin;
            m_left_chunk_end = left_chunk_end;
            m_right_chunk_begin = right_chunk_begin;
            m_right_chunk_end = right_chunk_end;
            m_false_leftover = false_leftover;
            m_true_leftover = true_leftover;
        } else {
            // operator() was re-executed on the same body instance
            parallel_partition_body tmp(*this, tbb::split{});
            tmp.m_left_chunk_begin = left_chunk_begin;
            tmp.m_left_chunk_end = left_chunk_end;
            tmp.m_right_chunk_begin = right_chunk_begin;
            tmp.m_right_chunk_end = right_chunk_end;
            tmp.m_false_leftover = false_leftover;
            tmp.m_true_leftover = true_leftover;
            join(tmp);
        }
    }

    void join(parallel_partition_body& src) {
        if (!has_false_leftover() && !src.has_false_leftover() && !has_true_leftover() && !src.has_true_leftover()) {
            // Case 1 - we are lucky, no leftovers at all, nothing extra
            m_left_chunk_end = src.m_left_chunk_end;
            m_right_chunk_begin = src.m_right_chunk_begin;
            m_false_leftover = m_left_chunk_end;
            m_true_leftover = m_right_chunk_begin;
            return;
        }

        if (!has_false_leftover() && !has_true_leftover()) {
            // This body contributed no leftover; src's leftover is already middle-adjacent,
            // so adopt it directly (covers this-none / src-false and this-none / src-true).
            m_left_chunk_end = src.m_left_chunk_end;
            m_right_chunk_begin = src.m_right_chunk_begin;
            m_false_leftover = src.m_false_leftover;
            m_true_leftover = src.m_true_leftover;
            return;
        }

        if (has_false_leftover() && src.has_false_leftover()) {
            // Case 2 - two false leftovers
            __TBB_ASSERT(!has_true_leftover() && !src.has_true_leftover(), nullptr);

            // Move this false leftover closer to the middle to merge with src's
            m_false_leftover = move_right(m_false_leftover, m_left_chunk_end, /*region_end = */src.m_false_leftover);
            m_left_chunk_end = src.m_left_chunk_end;
            m_right_chunk_begin = src.m_right_chunk_begin;
            m_true_leftover = m_right_chunk_begin;
            return;
        }

        if (has_true_leftover() && src.has_true_leftover()) {
            // Case 3 - two true leftovers
            __TBB_ASSERT(!has_false_leftover() && !src.has_false_leftover(), nullptr);

            // Move this true leftover closer to the middle to merge with src's
            m_true_leftover = move_left(m_right_chunk_begin, m_true_leftover, /*region_begin = */src.m_true_leftover);
            m_left_chunk_end = src.m_left_chunk_end;
            m_right_chunk_begin = src.m_right_chunk_begin;
            m_false_leftover = m_left_chunk_end;
            return;
        }

        if (has_false_leftover()) {
            // Case 4 - false leftover on this (left), true leftover on src (right)
            __TBB_ASSERT(!has_true_leftover() && !src.has_false_leftover(), nullptr);

            difference_type false_leftover_size = m_left_chunk_end - m_false_leftover;
            difference_type true_leftover_size = src.m_true_leftover - src.m_right_chunk_begin;

            if (false_leftover_size == true_leftover_size) {
                // Case 4.1 - equal sizes, both consumed by the swap
                parallel_swap_ranges(m_false_leftover, m_left_chunk_end, src.m_right_chunk_begin);
                m_left_chunk_end = src.m_left_chunk_end;
                m_right_chunk_begin = src.m_right_chunk_begin;
                m_false_leftover = m_left_chunk_end;
                m_true_leftover = m_right_chunk_begin;
                return;
            }

            if (false_leftover_size < true_leftover_size) {
                // Case 4.2 - false is smaller; consumed by swap, remaining true is already in place
                difference_type gap = src.m_true_leftover - false_leftover_size;
                parallel_swap_ranges(m_false_leftover, m_left_chunk_end, gap);
                m_left_chunk_end = src.m_left_chunk_end;
                m_right_chunk_begin = src.m_right_chunk_begin;
                m_false_leftover = m_left_chunk_end;
                m_true_leftover = gap;
            } else {
                // Case 4.3 - true is smaller; consumed by swap, move remaining false to the middle
                difference_type gap = m_false_leftover + true_leftover_size;
                parallel_swap_ranges(m_false_leftover, gap, src.m_right_chunk_begin);

                m_false_leftover = move_right(gap, m_left_chunk_end, /*region_end = */src.m_left_chunk_end);
                m_left_chunk_end = src.m_left_chunk_end;
                m_right_chunk_begin = src.m_right_chunk_begin;
                m_true_leftover = m_right_chunk_begin;
            }
        } else {
            // Case 5 - true leftover on this (right), false leftover on src (left)
            __TBB_ASSERT(has_true_leftover() && !src.has_true_leftover(), nullptr);

            difference_type false_leftover_size = src.m_left_chunk_end - src.m_false_leftover;
            difference_type true_leftover_size = m_true_leftover - m_right_chunk_begin;

            if (false_leftover_size == true_leftover_size) {
                // Case 5.1 - equal sizes, both consumed by the swap
                parallel_swap_ranges(src.m_false_leftover, src.m_left_chunk_end, m_right_chunk_begin);
                m_left_chunk_end = src.m_left_chunk_end;
                m_right_chunk_begin = src.m_right_chunk_begin;
                m_false_leftover = m_left_chunk_end;
                m_true_leftover = m_right_chunk_begin;
                return;
            }

            if (false_leftover_size < true_leftover_size) {
                // Case 5.2 - false is smaller; consumed by swap, move remaining true to the middle
                difference_type gap = m_true_leftover - false_leftover_size;
                parallel_swap_ranges(src.m_false_leftover, src.m_left_chunk_end, gap);

                m_true_leftover = move_left(m_right_chunk_begin, gap, /*region_begin = */src.m_right_chunk_begin);
                m_left_chunk_end = src.m_left_chunk_end;
                m_right_chunk_begin = src.m_right_chunk_begin;
                m_false_leftover = m_left_chunk_end;
            } else {
                // Case 5.3 - true is smaller; consumed by swap, remaining false is already in place
                difference_type gap = src.m_false_leftover + true_leftover_size;
                parallel_swap_ranges(src.m_false_leftover, gap, m_right_chunk_begin);

                m_left_chunk_end = src.m_left_chunk_end;
                m_right_chunk_begin = src.m_right_chunk_begin;
                m_false_leftover = gap;
                m_true_leftover = m_right_chunk_begin;
            }
        }
    }

private:
    void parallel_swap_ranges(difference_type first1, difference_type last1, difference_type first2) {
        static constexpr difference_type serial_swap_cutoff = 8192;
        difference_type len = last1 - first1;
        if (len < serial_swap_cutoff) {
            std::swap_ranges(g_first + first1, g_first + last1, g_first + first2);
            return;
        }
        RandomAccessIterator a = g_first + first1;
        RandomAccessIterator b = g_first + first2;
        tbb::parallel_for(
            tbb::blocked_range<difference_type>(difference_type(0), len, serial_swap_cutoff),
            [a, b](const tbb::blocked_range<difference_type>& r) {
                std::swap_ranges(a + r.begin(), a + r.end(), b + r.begin());
            }, m_ctx);
    }

    difference_type move_right(difference_type block_begin, difference_type block_end, difference_type region_end) {
        difference_type block = block_end - block_begin;
        difference_type gap = region_end - block_end;

        if (block <= gap) {
            parallel_swap_ranges(block_begin, block_end, region_end - block);
            return region_end - block;
        }

        parallel_swap_ranges(block_begin, block_begin + gap, block_end);
        return block_begin + gap;
    }

    difference_type move_left(difference_type block_begin, difference_type block_end, difference_type region_begin) {
        difference_type block = block_end - block_begin;
        difference_type gap = block_begin - region_begin;
        if (block <= gap) {
            parallel_swap_ranges(block_begin, block_end, region_begin);
            return region_begin + block;
        }
        parallel_swap_ranges(region_begin, block_begin, block_end - gap);
        return block_end - gap;
    }
};

template <typename RandomAccessIterator, typename Predicate>
RandomAccessIterator parallel_reduce_partition(RandomAccessIterator first, RandomAccessIterator last, Predicate pred,
                                               task_group_context& ctx)
{
    static constexpr std::size_t serial_partition_cutoff = 100000;

    using difference_type = typename std::iterator_traits<RandomAccessIterator>::difference_type;

    difference_type n = last - first;
    if (last - first < serial_partition_cutoff) {
        return std::partition(first, last, pred);
    }

    parallel_partition_body<RandomAccessIterator, Predicate> body(first, n, pred, ctx);

    tbb::parallel_reduce(tbb::blocked_range<difference_type>(difference_type(0), n / 2), body, ctx);

    __TBB_ASSERT(!(body.has_true_leftover() && body.has_false_leftover()), nullptr);

    difference_type p = body.has_true_leftover() ? body.get_true_leftover()
                                                 : body.get_false_leftover();

    // For odd number of elements, the exact middle one is not covered
    if (n % 2 != 0) {
        difference_type mid = n / 2;
        if (body.has_true_leftover()) {
            if (!pred(first[mid])) {
                std::iter_swap(first + mid, first + (p - 1));
                --p;
            }
        } else {
            if (pred(first[mid])) {
                std::iter_swap(first + p, first + mid);
                ++p;
            }
        }
    }

    return first + p;
}

} // namespace d1
} // namespace detail
} // namespace tbb

#endif // PARALLEL_REDUCE_PARTITION_V2_H
