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

#ifndef __TBB_detail__parallel_partition_H
#define __TBB_detail__parallel_partition_H

#include "_assert.h"
#include "../parallel_for.h"
#include "../parallel_reduce.h"
#include "../blocked_range.h"
#include "../task_group.h"
#include <algorithm>
#include <iterator>

namespace tbb {
namespace detail {
namespace d1 {

// Mirror-Reduce Partition Algorithm
// ---------------------------------
// The input [first, last) is interpreted as two mirrored halves:
// - a real half [first, first + n / 2)
// - a mirror half [last - n / 2, last)
// Parallel reduction runs over the real half.
// Each leaf pairs a real chunk with its mirror chunk and partitions
// the pair by swapping the misplaced elements. Once one side is exhausted,
// it partitions the remaining tail on one side. This yields at most one leftover
// per leaf: either a false-leftover (elements for which the predicate is FALSE)
// on real side, or a true-leftover (elements for which the predicate is TRUE)
// on the mirror side, never both.
//
// Merge combines adjacent leaf ranges by moving leftovers toward the middle
// while preserving that invariant. Same-kind leftovers are shifted together,
// and a false/true pair is neutralized by swapping, with any remainder
// moved inward.
//
// After reduction, the surviving leftover determines the partition point. For odd n,
// the uncovered middle element is placed separately.

template <typename RandomAccessIterator, typename Predicate>
class parallel_partition_body {
    using iterator_traits = std::iterator_traits<RandomAccessIterator>;
    using difference_type = typename iterator_traits::difference_type;

    RandomAccessIterator m_real_chunk_begin;
    RandomAccessIterator m_real_chunk_end;
    RandomAccessIterator m_mirror_chunk_begin;
    RandomAccessIterator m_mirror_chunk_end;

    RandomAccessIterator m_false_leftover;
    RandomAccessIterator m_true_leftover;

    Predicate&           m_pred;
    task_group_context&  m_ctx;
    RandomAccessIterator g_first;
    RandomAccessIterator g_last;

    void parallel_swap_ranges(RandomAccessIterator first, RandomAccessIterator last,
                              RandomAccessIterator target_begin) {
        static constexpr difference_type serial_cutoff = 8192;

        if ((last - first) < serial_cutoff) {
            std::swap_ranges(first, last, target_begin);
        } else {
            parallel_for(blocked_range<RandomAccessIterator>(first, last, serial_cutoff),
                [=](const blocked_range<RandomAccessIterator>& range) {
                    RandomAccessIterator chunk_target = target_begin + (range.begin() - first);
                    std::swap_ranges(range.begin(), range.end(), chunk_target);
                },
                auto_partitioner(), m_ctx);
        }
    }

    RandomAccessIterator move_left(RandomAccessIterator first, RandomAccessIterator last,
                                   RandomAccessIterator target_region_begin) {
        difference_type block_size = last - first;
        difference_type gap = first - target_region_begin;

        RandomAccessIterator result;

        if (block_size <= gap) {
            result = target_region_begin + block_size;
            parallel_swap_ranges(first, last, target_region_begin);
        } else {
            result = last - gap;
            parallel_swap_ranges(target_region_begin, first, last - gap);
        }
        return result;
    }

    RandomAccessIterator move_right(RandomAccessIterator first, RandomAccessIterator last,
                                    RandomAccessIterator target_region_end) {
        difference_type block_size = last - first;
        difference_type gap = target_region_end - last;

        RandomAccessIterator result;

        if (block_size <= gap) {
            result = target_region_end - block_size;
            parallel_swap_ranges(first, last, result);
        } else {
            result = first + gap;
            parallel_swap_ranges(first, result, last);
        }
        return result;
    }

public:
    bool has_false_leftover() const { return m_false_leftover != m_real_chunk_end; }
    bool has_true_leftover() const { return m_true_leftover != m_mirror_chunk_begin; }

    RandomAccessIterator get_false_leftover() const { return m_false_leftover; }
    RandomAccessIterator get_true_leftover() const { return m_true_leftover; }

    parallel_partition_body(RandomAccessIterator first, RandomAccessIterator last,
                            Predicate& pred, task_group_context& ctx)
        : m_real_chunk_begin(last), m_real_chunk_end(last)
        , m_mirror_chunk_begin(last), m_mirror_chunk_end(last)
        , m_false_leftover(last), m_true_leftover(last) // Will be overwritten by operator()
        , m_pred(pred), m_ctx(ctx)
        , g_first(first), g_last(last)
    {}

    parallel_partition_body(parallel_partition_body& src, split)
        : m_real_chunk_begin(src.g_last), m_real_chunk_end(src.g_last)
        , m_mirror_chunk_begin(src.g_last), m_mirror_chunk_end(src.g_last)
        , m_false_leftover(src.g_last), m_true_leftover(src.g_last) // Will be overwritten by operator()
        , m_pred(src.m_pred), m_ctx(src.m_ctx)
        , g_first(src.g_first), g_last(src.g_last)
    {}

    void operator()(const blocked_range<RandomAccessIterator>& range) {
        RandomAccessIterator real_chunk_begin = range.begin();
        RandomAccessIterator real_chunk_end = range.end();

        RandomAccessIterator mirror_chunk_begin = g_last - (real_chunk_end - g_first);
        RandomAccessIterator mirror_chunk_end = g_last - (real_chunk_begin - g_first);

        // Partition the pair of chunks
        RandomAccessIterator left = real_chunk_begin;
        RandomAccessIterator right = mirror_chunk_end;

        while (true) {
            while (left != real_chunk_end && m_pred(*left))
                ++left;
            while (right != mirror_chunk_begin && !m_pred(*(right - 1)))
                ++right;

            if (left != real_chunk_end && right != mirror_chunk_begin) {
                std::iter_swap(left, std::prev(right));
                ++left;
                --right;
            } else {
                break;
            }
        } // while (true)

        RandomAccessIterator false_leftover = real_chunk_end;
        RandomAccessIterator true_leftover = mirror_chunk_begin;

        if (left != real_chunk_end) {
            false_leftover = std::partition(left, real_chunk_end, m_pred);
        } else {
            true_leftover = std::partition(mirror_chunk_begin, right, m_pred);
        }

        // Store the result
        if (m_real_chunk_begin == m_real_chunk_end) {
            // First sub-range assignment to this body
            m_real_chunk_begin = real_chunk_begin;
            m_real_chunk_end = real_chunk_end;
            m_mirror_chunk_begin = mirror_chunk_begin;
            m_mirror_chunk_end = mirror_chunk_end;
            m_false_leftover = false_leftover;
            m_true_leftover = true_leftover;
        } else {
            // operator() was re-executed on the same body instance
            parallel_partition_body tmp(*this, split{});
            tmp.m_real_chunk_begin = real_chunk_begin;
            tmp.m_real_chunk_end = real_chunk_end;
            tmp.m_mirror_chunk_begin = mirror_chunk_begin;
            tmp.m_mirror_chunk_end = mirror_chunk_end;
            tmp.m_false_leftover = false_leftover;
            tmp.m_true_leftover = true_leftover;
            join(tmp);
        }
    }

    void join(parallel_partition_body& src) {
        __TBB_ASSERT(m_real_chunk_begin != m_real_chunk_end, "join() accumulates an empty range");
        __TBB_ASSERT(m_mirror_chunk_begin != m_mirror_chunk_end, "join() accumulates an empty range");

        // Use leftovers from src since they are adjacent to the middle
        // If *this has no leftover, the initial state is already a correct result
        RandomAccessIterator false_leftover = src.m_false_leftover;
        RandomAccessIterator true_leftover = src.m_true_leftover;

        if (has_false_leftover()) {
            __TBB_ASSERT(!has_true_leftover(), "Broken MRP algorithm invariant");
            if (src.has_false_leftover()) {
                __TBB_ASSERT(!src.has_true_leftover(), "Broken MRP algorithm invariant");
                
                // Two false leftovers in the real side
                // Move *this false leftover closer to the middle
                false_leftover = move_right(m_false_leftover, m_real_chunk_end,
                                            /*target_region_end = */src.m_false_leftover);
            } else {
                // False leftover in *this, true leftover (or none) in src
                difference_type false_leftover_size = m_real_chunk_end - m_false_leftover;
                difference_type true_leftover_size = src.m_true_leftover - src.m_mirror_chunk_begin;

                if (false_leftover_size <= true_leftover_size) {
                    // False leftover is smaller and will be consumed by the swap
                    // Remaining true leftover (if any) is already in place
                    true_leftover = src.m_true_leftover - false_leftover_size;
                    parallel_swap_ranges(m_false_leftover, m_real_chunk_end, true_leftover);
                } else {
                    // True leftover is smaller and will be consumed by the swap
                    RandomAccessIterator swap_end = m_false_leftover + true_leftover_size;
                    parallel_swap_ranges(m_false_leftover, swap_end, src.m_mirror_chunk_begin);

                    true_leftover = m_mirror_chunk_begin;

                    // Move the remaining part of the false leftover closer to the middle
                    false_leftover = move_right(swap_end, m_real_chunk_end,
                                                /*target_region_end =*/src.m_real_chunk_end);
                }
            }
        } else if (has_true_leftover()) {
            if (src.has_true_leftover()) {
                __TBB_ASSERT(!src.has_false_leftover(), "Broken MRP algorithm invariant");

                // Two true leftovers in the mirror side
                // Move this range's true leftover closer to the middle
                true_leftover = move_left(m_mirror_chunk_begin, m_true_leftover,
                                          /*target_region_begin = */src.m_true_leftover);
            } else {
                // True leftover in *this, false leftover (or none) in src
                difference_type false_leftover_size = src.m_real_chunk_end - src.m_false_leftover;
                difference_type true_leftover_size = m_true_leftover - m_mirror_chunk_begin;

                if (false_leftover_size < true_leftover_size) {
                    // False leftover is smaller and will be consumed by swap
                    RandomAccessIterator swap_begin = m_true_leftover - false_leftover_size;
                    parallel_swap_ranges(src.m_false_leftover, src.m_real_chunk_end, swap_begin);

                    false_leftover = src.m_real_chunk_end;

                    // Move remaining part of the true leftover closer to the middle
                    true_leftover = move_left(m_mirror_chunk_begin, swap_begin,
                                              /*target_region_begin = */src.m_mirror_chunk_begin);
                } else {
                    // True leftover is smaller and will be consumed by the swap
                    // Remaining false leftover (if any) is already in place
                    false_leftover = src.m_false_leftover + true_leftover_size;
                    parallel_swap_ranges(src.m_false_leftover, false_leftover, m_mirror_chunk_begin);
                }
            }
        }

        m_real_chunk_end = src.m_real_chunk_end;
        m_mirror_chunk_begin = src.m_mirror_chunk_begin;
        m_false_leftover = false_leftover;
        m_true_leftover = true_leftover;
    }
};

template <typename RandomAccessIterator, typename Predicate>
RandomAccessIterator parallel_partition(RandomAccessIterator first, RandomAccessIterator last,
                                        Predicate pred, task_group_context& ctx) {
    static constexpr std::size_t serial_partition_cutoff = 100000;

    using difference_type = typename std::iterator_traits<RandomAccessIterator>::difference_type;

    difference_type n = last - first;
    if (n < difference_type(serial_partition_cutoff)) {
        return std::partition(first, last, pred);
    }

    parallel_partition_body<RandomAccessIterator, Predicate> body(first, last, pred, ctx);
    difference_type mid = n / 2;

    parallel_reduce(blocked_range<RandomAccessIterator>(first, first + mid), body, ctx);

    __TBB_ASSERT(!(body.has_true_leftover() && body.has_false_leftover()),
                 "At most one leftover can remain after the reduction phase");

    RandomAccessIterator partition = body.has_true_leftover() ? body.get_true_leftover()
                                                              : body.get_false_leftover();

    // For odd number of elements, the exact middle one is not covered by mirror-reduce
    if (n % 2 != 0) {
        if (body.has_true_leftover()) {
            if (!pred(first[mid])) {
                --partition;
                std::iter_swap(first + mid, partition);
            }
        } else {
            if (pred(first[mid])) {
                std::iter_swap(partition, first + mid);
                ++partition;
            }
        }
    }

    return partition;
}

} // namespace d1
} // namespace detail
} // namespace tbb

#endif // __TBB_detail__parallel_partition_H
