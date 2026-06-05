#include "detail/_task.h"
#include "task_group.h"
#include "task_arena.h"

namespace tbb {
namespace detail {
namespace d1 {

template <typename DifferenceType>
struct Partial {
    DifferenceType block_begin;
    DifferenceType chunk_begin;
    bool           is_valid;
};

template <typename RandomAccessIterator, typename Compare>
class ParallelPartitionTask : public task {
public:
    using diff_type = typename std::iterator_traits<RandomAccessIterator>::difference_type;
    using value_type = typename std::iterator_traits<RandomAccessIterator>::value_type;
    using partial_type = Partial<diff_type>;

private:
    const std::size_t   m_index;
    const diff_type     m_block_size;
    Compare&            m_compare;
    value_type&         m_pivot;
    wait_context&       m_wait_ctx;
    small_object_allocator m_allocator;

    const RandomAccessIterator    g_begin;
    std::atomic<diff_type>&       g_distance;
    std::atomic<diff_type>&       g_head;
    std::atomic<diff_type>&       g_tail;
    std::vector<partial_type>&    g_left_partitions;
    std::vector<partial_type>&    g_right_partitions;
public:
    ParallelPartitionTask(std::size_t index, diff_type block_size,
                          Compare& compare, value_type& pivot, wait_context& wait_ctx,
                          small_object_allocator& allocator,
                          RandomAccessIterator global_begin,
                          std::atomic<diff_type>& global_distance,
                          std::atomic<diff_type>& global_head,
                          std::atomic<diff_type>& global_tail,
                          std::vector<partial_type>& global_left_partitions,
                          std::vector<partial_type>& global_right_partitions)
        : m_index(index)
        , m_block_size(block_size)
        , m_compare(compare)
        , m_pivot(pivot)
        , m_wait_ctx(wait_ctx)
        , m_allocator(allocator)
        , g_begin(global_begin)
        , g_distance(global_distance)
        , g_head(global_head)
        , g_tail(global_tail)
        , g_left_partitions(global_left_partitions)
        , g_right_partitions(global_right_partitions)
    {}

    bool try_get_left_block(diff_type& chunk_begin) {
        diff_type block_size = diff_type(m_block_size);
        auto rem = g_distance.fetch_sub(block_size, std::memory_order_relaxed);
        if (rem < block_size) {
            g_distance.fetch_add(block_size, std::memory_order_relaxed);
            return false;
        }
        chunk_begin = g_head.fetch_add(block_size, std::memory_order_relaxed);
        return true;
    }

    bool try_get_right_block(diff_type& chunk_begin) {
        diff_type block_size = diff_type(m_block_size);
        auto rem = g_distance.fetch_sub(block_size, std::memory_order_relaxed);
        if (rem < block_size) {
            g_distance.fetch_add(block_size, std::memory_order_relaxed);
            return false;
        }
        auto chunk_end = g_tail.fetch_sub(block_size, std::memory_order_relaxed);
        chunk_begin = chunk_end - block_size;
        return true;
    }

    task* execute(execution_data& ed) override {
        diff_type left_begin = 0, right_begin = 0;
        bool have_left_block  = try_get_left_block(left_begin);
        bool have_right_block = try_get_right_block(right_begin);

        diff_type left_index = left_begin;
        diff_type left_end   = left_begin + m_block_size;
        diff_type right_end  = right_begin + m_block_size;

        while (have_left_block && have_right_block) {
            while (left_index < left_end &&  m_compare(g_begin[left_index],     m_pivot)) {
                ++left_index;
            }
            while (right_end > right_begin && !m_compare(g_begin[right_end - 1], m_pivot)) {
                --right_end;
            }

            if (left_index < left_end && right_end > right_begin) {
                std::iter_swap(g_begin + left_index, g_begin + (right_end - 1));
                ++left_index;
                --right_end;
                continue;
            }

            // Left block fully processed
            if (left_index == left_end) {
                have_left_block = try_get_left_block(left_begin);
                if (have_left_block) {
                    left_index = left_begin;
                    left_end   = left_begin + m_block_size;
                }
            }

            // Right block fully processed
            if (right_end == right_begin) {
                have_right_block = try_get_right_block(right_begin);
                if (have_right_block) {
                    right_end = right_begin + m_block_size;
                }
            }
        }

        if (have_left_block) {
            g_left_partitions[m_index]  = partial_type{left_begin, left_index, true};
        }
        if (have_right_block) {
            g_right_partitions[m_index] = partial_type{right_begin, right_end, true};
        }

        m_wait_ctx.release();
        m_allocator.delete_object(this, ed);
        return nullptr;
    }

    task* cancel(execution_data& ed) override {
        m_allocator.delete_object(this, ed);
        return nullptr;
    }
};

std::size_t choose_block_size(std::size_t) {
    return 4096;
}

std::size_t choose_num_tasks(std::size_t n, std::size_t block_size) {
    return this_task_arena::max_concurrency();
    // return std::max<std::size_t>(1, n / (2 * block_size));
}
enum class side_kind {
    left, right
};

template <side_kind S>
struct side_tag {};

template <typename DifferenceType, typename PartialType>
void identify_dirty_range(const PartialType& dirty_block, DifferenceType block_size, DifferenceType dst_block_begin,
                          DifferenceType& begin, DifferenceType& end, DifferenceType& dst_begin, side_tag<side_kind::left>)
{
    // Left side - dirty block is a suffix
    begin = dirty_block.chunk_begin;
    end = dirty_block.block_begin + block_size;
    dst_begin = dst_block_begin + (dirty_block.chunk_begin - dirty_block.block_begin);
}

template <typename DifferenceType, typename PartialType>
void identify_dirty_range(const PartialType& dirty_block, DifferenceType, DifferenceType dst_block_begin,
                          DifferenceType& begin, DifferenceType& end, DifferenceType& dst_begin, side_tag<side_kind::right>)
{
    // Right side - dirty block is a prefix
    begin = dirty_block.block_begin;
    end = dirty_block.chunk_begin;
    dst_begin = dst_block_begin;
}

template <typename PartialsType, typename DifferenceType>
bool slot_is_dirty(const PartialsType& dirty_blocks, DifferenceType slot) {
    for (auto& dirty_block : dirty_blocks) {
        if (dirty_block.block_begin == slot) return true;
    }
    return false;
}

template <side_kind S, typename RandomAccessIterator, typename DifferenceType, typename PartialsType>
void relocate_side(RandomAccessIterator first, DifferenceType block_size,
                   DifferenceType slab_begin, DifferenceType slab_end,
                   const PartialsType& dirty_blocks)
{
    DifferenceType slot = slab_begin;
    for (auto dirty_block : dirty_blocks) {
        // dirty block already in slab - keep it as is
        if (slab_begin <= dirty_block.block_begin && dirty_block.block_begin < slab_end) {
            continue;
        }

        // Advance to next empty slab slot
        while (slot < slab_end && slot_is_dirty(dirty_blocks, slot)) {
            slot += block_size;
        }

        DifferenceType begin, end, dst_begin;
        identify_dirty_range(dirty_block, block_size, slot, begin, end, dst_begin, side_tag<S>{});

        // Move dirty block to slab
        std::swap_ranges(first + begin, first + end, first + dst_begin);
        slot += block_size;
    }
}

template <typename RandomAccessIterator, typename Compare, typename DifferenceType, typename PartialsType>
RandomAccessIterator finalize_partition(RandomAccessIterator first, Compare comp, DifferenceType block_size,
                                        DifferenceType remainder_begin, DifferenceType remainder_end,
                                        PartialsType& left_block_map, PartialsType& right_block_map)
{
    PartialsType left_dirty_blocks, right_dirty_blocks;
    left_dirty_blocks.reserve(left_block_map.size());
    right_dirty_blocks.reserve(right_block_map.size());

    for (auto& partial : left_block_map) {
        if (partial.is_valid) {
            left_dirty_blocks.emplace_back(partial);
        }
    }

    for (auto& partial : right_block_map) {
        if (partial.is_valid) {
            right_dirty_blocks.emplace_back(partial);
        }
    }

    std::size_t left_dirty_block_count = left_dirty_blocks.size();
    std::size_t right_dirty_block_count = right_dirty_blocks.size();

    DifferenceType left_slab_end = remainder_begin;
    DifferenceType left_slab_begin = left_slab_end - (left_dirty_block_count * block_size);

    DifferenceType right_slab_begin = remainder_end;
    DifferenceType right_slab_end = right_slab_begin + (right_dirty_block_count * block_size);

    relocate_side<side_kind::left >(first, block_size, left_slab_begin, left_slab_end, left_dirty_blocks);
    relocate_side<side_kind::right>(first, block_size, right_slab_begin, right_slab_end, right_dirty_blocks);

    return std::partition(first + left_slab_begin, first + right_slab_end, comp);
}

// First element is a pivot element
template <typename RandomAccessIterator, typename Compare>
RandomAccessIterator parallel_partition(RandomAccessIterator first, RandomAccessIterator last, Compare comp)
{
    using traits = std::iterator_traits<RandomAccessIterator>;
    using diff_type = typename traits::difference_type;
    using reference = typename traits::reference;
    using value_type = typename traits::value_type;
    using task_type = ParallelPartitionTask<RandomAccessIterator, Compare>;
    using partial_type = typename task_type::partial_type;

    const diff_type n = std::distance(first, last);

    const std::size_t block_size = choose_block_size(n);
    const std::size_t num_tasks  = choose_num_tasks(n, block_size);

    auto compare_with_pivot = [&](reference x) {
        return comp(x, *first);
    };

    // if (n < diff_type(4 * block_size * num_tasks)) {
    //     return std::partition(std::next(first), last, compare_with_pivot);
    // }

    value_type pivot = *first; // TODO: move later
    alignas(64) std::atomic<diff_type> g_distance{n - 1};
    alignas(64) std::atomic<diff_type> g_head{0};
    alignas(64) std::atomic<diff_type> g_tail{n - 1};

    std::vector<partial_type> g_left_partials (num_tasks, partial_type{0, 0, false});
    std::vector<partial_type> g_right_partials(num_tasks, partial_type{0, 0, false});

    small_object_allocator allocator;
    wait_context wait_ctx(num_tasks);
    task_group_context ctx;

    for (std::size_t i = 0; i < num_tasks; ++i) {
        spawn(*allocator.new_object<task_type>(
            i, static_cast<diff_type>(block_size), comp, pivot, wait_ctx, allocator,
            std::next(first), g_distance, g_head, g_tail, g_left_partials, g_right_partials), ctx);
    }

    wait(wait_ctx, ctx);

    return finalize_partition(std::next(first), compare_with_pivot, diff_type(block_size),
                              g_head.load(std::memory_order_relaxed), g_tail.load(std::memory_order_relaxed),
                              g_left_partials, g_right_partials);
}

} // namespace d1
} // namespace detail
} // namespace tbb
