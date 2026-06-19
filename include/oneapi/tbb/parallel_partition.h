#include "detail/_task.h"
#include "task_group.h"
#include "task_arena.h"
#include "parallel_for.h"
#include "partitioner.h"
#include <array>

namespace tbb {
namespace detail {
namespace d1 {

template <typename DifferenceType>
struct Partial {
    DifferenceType block_begin;
    DifferenceType chunk_begin;
    bool           is_valid;
};

template <typename DiffType>
bool try_get_left_block(DiffType& chunk_begin, DiffType block_size, std::atomic<DiffType>& g_distance, std::atomic<DiffType>& g_head) {
    auto rem = g_distance.fetch_sub(block_size, std::memory_order_relaxed);
    if (rem < block_size) {
        g_distance.fetch_add(block_size, std::memory_order_relaxed);
        return false;
    }
    chunk_begin = g_head.fetch_add(block_size, std::memory_order_relaxed);
    return true;
}

template <typename DiffType>
bool try_get_right_block(DiffType& chunk_begin, DiffType block_size, std::atomic<DiffType>& g_distance, std::atomic<DiffType>& g_tail) {
    auto rem = g_distance.fetch_sub(block_size, std::memory_order_relaxed);
    if (rem < block_size) {
        g_distance.fetch_add(block_size, std::memory_order_relaxed);
        return false;
    }
    auto chunk_end = g_tail.fetch_sub(block_size, std::memory_order_relaxed);
    chunk_begin = chunk_end - block_size;
    return true;
}

template <typename RandomAccessIterator, typename Predicate,
          typename DiffType = typename std::iterator_traits<RandomAccessIterator>::difference_type>
void parallel_partition_task_body(std::size_t index, DiffType block_size, Predicate& pred, RandomAccessIterator g_begin,
                                  std::atomic<DiffType>& g_distance, std::atomic<DiffType>& g_head, std::atomic<DiffType>& g_tail,
                                  std::vector<Partial<DiffType>>& g_left_partitions, std::vector<Partial<DiffType>>& g_right_partitions)
{
    DiffType left_begin = 0, right_begin = 0;
    bool have_left_block  = try_get_left_block(left_begin, block_size, g_distance, g_head);
    bool have_right_block = try_get_right_block(right_begin, block_size, g_distance, g_tail);

    DiffType left_index = left_begin;
    DiffType left_end   = left_begin + block_size;
    DiffType right_end  = right_begin + block_size;

    while (have_left_block && have_right_block) {
        while (left_index < left_end &&  pred(g_begin[left_index])) {
            ++left_index;
        }
        while (right_end > right_begin && !pred(g_begin[right_end - 1])) {
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
            have_left_block = try_get_left_block(left_begin, block_size, g_distance, g_head);
            if (have_left_block) {
                left_index = left_begin;
                left_end   = left_begin + block_size;
            }
        }

        // Right block fully processed
        if (right_end == right_begin) {
            have_right_block = try_get_right_block(right_begin, block_size, g_distance, g_tail);
            if (have_right_block) {
                right_end = right_begin + block_size;
            }
        }
    }

    // Store dirty blocks
    if (have_left_block) {
        g_left_partitions[index]  = Partial<DiffType>{left_begin, left_index, true};
    }
    if (have_right_block) {
        g_right_partitions[index] = Partial<DiffType>{right_begin, right_end, true};
    }
}

template <typename RandomAccessIterator, typename Predicate>
class ParallelPartitionTask : public task {
public:
    using diff_type = typename std::iterator_traits<RandomAccessIterator>::difference_type;
    using partial_type = Partial<diff_type>;

private:
    const std::size_t   m_index;
    const diff_type     m_block_size;
    Predicate&          m_pred;
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
                          Predicate& pred, wait_context& wait_ctx,
                          small_object_allocator& allocator,
                          RandomAccessIterator global_begin,
                          std::atomic<diff_type>& global_distance,
                          std::atomic<diff_type>& global_head,
                          std::atomic<diff_type>& global_tail,
                          std::vector<partial_type>& global_left_partitions,
                          std::vector<partial_type>& global_right_partitions)
        : m_index(index)
        , m_block_size(block_size)
        , m_pred(pred)
        , m_wait_ctx(wait_ctx)
        , m_allocator(allocator)
        , g_begin(global_begin)
        , g_distance(global_distance)
        , g_head(global_head)
        , g_tail(global_tail)
        , g_left_partitions(global_left_partitions)
        , g_right_partitions(global_right_partitions)
    {}

    task* execute(execution_data& ed) override {
        parallel_partition_task_body(m_index, m_block_size, m_pred, g_begin, g_distance,
                                     g_head, g_tail, g_left_partitions, g_right_partitions);

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
    return 1024;
}

template <typename DiffType>
std::size_t choose_task_count(DiffType n, std::size_t budget, std::size_t block_size) {
    std::size_t grain = 4 * block_size;
    std::size_t feasible = std::size_t(n) / grain;

    return std::min<std::size_t>(budget, feasible);
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

template <typename RandomAccessIterator, typename Compare, typename DifferenceType, typename PartialsType, typename FinalPartition>
RandomAccessIterator finalize_partition(RandomAccessIterator first, Compare comp, DifferenceType block_size,
                                        DifferenceType remainder_begin, DifferenceType remainder_end,
                                        PartialsType& left_block_map, PartialsType& right_block_map,
                                        FinalPartition& final_partition)
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

    return final_partition(first + left_slab_begin, first + right_slab_end, comp);
}

// First element is a pivot element
template <typename RandomAccessIterator, typename Predicate>
RandomAccessIterator parallel_partition(RandomAccessIterator first, RandomAccessIterator last, Predicate pred,
                                        std::size_t num_tasks, task_group_context& ctx)
{
    __TBB_ASSERT(num_tasks > 1, nullptr);

    using traits = std::iterator_traits<RandomAccessIterator>;
    using diff_type = typename traits::difference_type;
    using reference = typename traits::reference;
    using value_type = typename traits::value_type;
    using task_type = ParallelPartitionTask<RandomAccessIterator, Predicate>;
    using partial_type = typename task_type::partial_type;

    const diff_type n = std::distance(first, last);

    const std::size_t block_size = choose_block_size(n);

    if (n < diff_type(4 * block_size * num_tasks)) {
        return std::partition(first, last, pred);
    }

    alignas(64) std::atomic<diff_type> g_distance{n};
    alignas(64) std::atomic<diff_type> g_head{0};
    alignas(64) std::atomic<diff_type> g_tail{n};

    std::vector<partial_type> g_left_partials (num_tasks, partial_type{0, 0, false});
    std::vector<partial_type> g_right_partials(num_tasks, partial_type{0, 0, false});

    small_object_allocator allocator;
    wait_context wait_ctx(num_tasks - 1);

    for (std::size_t i = 0; i < num_tasks - 1; ++i) {
        spawn(*allocator.new_object<task_type>(i, diff_type(block_size), pred, wait_ctx, allocator,
                                               first, g_distance, g_head, g_tail, g_left_partials, g_right_partials), ctx);
    }

    parallel_partition_task_body(num_tasks - 1, diff_type(block_size), pred, first,
                                 g_distance, g_head, g_tail, g_left_partials, g_right_partials);

    wait(wait_ctx, ctx);

    auto serial_partition = [](RandomAccessIterator first, RandomAccessIterator last, Predicate& pred) {
        return std::partition(first, last, pred);
    };

    return finalize_partition(first, pred, diff_type(block_size),
                              g_head.load(std::memory_order_relaxed), g_tail.load(std::memory_order_relaxed),
                              g_left_partials, g_right_partials,
                              serial_partition);
}

template <typename RandomAccessIterator, typename Predicate>
class ParallelPartitionTaskNew : public task {
public:
    using diff_type = typename std::iterator_traits<RandomAccessIterator>::difference_type;
    using partial_type = Partial<diff_type>;
private:
    const std::size_t      m_index;
    const diff_type        m_block_size;
    const diff_type        m_spawn_threshold;
    Predicate&             m_pred;
    wait_context&          m_wait_ctx;
    task_group_context&    m_ctx;
    small_object_allocator m_allocator;

    const RandomAccessIterator g_begin;
    std::atomic<diff_type>&    g_distance;
    std::atomic<diff_type>&    g_head;
    std::atomic<diff_type>&    g_tail;
    std::vector<partial_type>& g_left_block_map;
    std::vector<partial_type>& g_right_block_map;

public:
    ParallelPartitionTaskNew(std::size_t index, diff_type block_size, diff_type spawn_threshold,
                             Predicate& pred, wait_context& wait_ctx, task_group_context& ctx,
                             small_object_allocator& allocator,
                             RandomAccessIterator global_begin,
                             std::atomic<diff_type>& global_distance,
                             std::atomic<diff_type>& global_head, std::atomic<diff_type>& global_tail,
                             std::vector<partial_type>& global_left_block_map,
                             std::vector<partial_type>& global_right_block_map)
        : m_index(index)
        , m_block_size(block_size)
        , m_spawn_threshold(spawn_threshold)
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

    ~ParallelPartitionTaskNew() {
        m_wait_ctx.release();
    }

    task* execute(execution_data& ed) override {
        // Spawn new participant if there is enough work to share
        if (m_index + 1 < g_left_block_map.size() &&
            g_distance.load(std::memory_order_relaxed) > m_spawn_threshold)
        {
            spawn(*m_allocator.new_object<ParallelPartitionTaskNew>(m_index + 1, m_block_size, m_spawn_threshold,
                                                                    m_pred, m_wait_ctx, m_ctx, m_allocator,
                                                                    g_begin, g_distance, g_head, g_tail,
                                                                    g_left_block_map, g_right_block_map),
                  m_ctx);
        }

        parallel_partition_task_body(m_index, m_block_size, m_pred, g_begin, g_distance, g_head, g_tail,
                                     g_left_block_map, g_right_block_map);

        m_allocator.delete_object(this, ed);
        return nullptr;
    }

    task* cancel(execution_data& ed) override {
        m_allocator.delete_object(this, ed);
        return nullptr;
    }
};

template <typename RandomAccessIterator, typename Predicate>
RandomAccessIterator parallel_partition_new(RandomAccessIterator first, RandomAccessIterator last, Predicate pred,
                                            task_group_context& ctx, std::size_t block_size = 0)
{
    using traits = std::iterator_traits<RandomAccessIterator>;
    using diff_type = typename traits::difference_type;
    using task_type = ParallelPartitionTaskNew<RandomAccessIterator, Predicate>;
    using partial_type = typename task_type::partial_type;

    const diff_type n = std::distance(first, last);
    if (block_size == 0) block_size = choose_block_size(n);

    if (n < diff_type(4 * block_size)) {
        return std::partition(first, last, pred);
    }

    const std::size_t max_dirty_blocks_count = std::min<std::size_t>(this_task_arena::max_concurrency(),
                                                                     std::size_t(n / (4 * block_size)));

    if (max_dirty_blocks_count <= 1) {
        return std::partition(first, last, pred);
    }

    alignas(64) std::atomic<diff_type> g_distance{n};
    alignas(64) std::atomic<diff_type> g_head{0};
    alignas(64) std::atomic<diff_type> g_tail{n};

    std::vector<partial_type> g_left_block_map (max_dirty_blocks_count, partial_type{0, 0, false});
    std::vector<partial_type> g_right_block_map(max_dirty_blocks_count, partial_type{0, 0, false});

    small_object_allocator allocator;
    wait_context wait_ctx{0};

    const diff_type spawn_threshold = diff_type(2 * block_size);

    spawn(*allocator.new_object<task_type>(/*index = */1, diff_type(block_size), spawn_threshold,
                                           pred, wait_ctx, ctx, allocator,
                                           first, g_distance, g_head, g_tail,
                                           g_left_block_map, g_right_block_map),
          ctx);

    parallel_partition_task_body(0, diff_type(block_size), pred, first,
                                 g_distance, g_head, g_tail, g_left_block_map, g_right_block_map);

    wait(wait_ctx, ctx);

    auto parallel_partition = [&ctx, block_size](RandomAccessIterator first, RandomAccessIterator last, Predicate& pred) {
        std::size_t next_level_block_size = std::max<std::size_t>(block_size / 2, 128);
        return parallel_partition_new(first, last, pred, ctx, next_level_block_size);
    };

    return finalize_partition(first, pred, diff_type(block_size),
                              g_head.load(std::memory_order_relaxed), g_tail.load(std::memory_order_relaxed),
                              g_left_block_map, g_right_block_map,
                              parallel_partition);
}

template <typename RandomAccessIterator, typename Compare>
void parallel_quick_sort(RandomAccessIterator first, RandomAccessIterator last, Compare comp,
                         wait_context& wait_ctx, task_group_context& ctx, std::size_t budget, bool leftmost);

template <typename RandomAccessIterator, typename Compare>
class ParallelQuickSortTask : public task {
    RandomAccessIterator   m_first;
    RandomAccessIterator   m_last;
    Compare                m_comp;
    wait_context&          m_wait_ctx;
    task_group_context&    m_ctx;
    std::size_t            m_budget;
    bool                   m_leftmost;
    small_object_allocator m_allocator;
public:
    ParallelQuickSortTask(RandomAccessIterator first, RandomAccessIterator last, Compare comp,
                          wait_context& wait_ctx, task_group_context& ctx, std::size_t budget,
                          bool leftmost, small_object_allocator& allocator)
        : m_first(first)
        , m_last(last)
        , m_comp(comp)
        , m_wait_ctx(wait_ctx)
        , m_ctx(ctx)
        , m_budget(budget)
        , m_leftmost(leftmost)
        , m_allocator(allocator)
    {
        m_wait_ctx.reserve();
    }
        

    ~ParallelQuickSortTask() {
        m_wait_ctx.release();
    }

    task* execute(execution_data& ed) override {
        parallel_quick_sort(m_first, m_last, m_comp, m_wait_ctx, m_ctx, m_budget, m_leftmost);
        m_allocator.delete_object(this, ed);
        return nullptr;
    }

    task* cancel(execution_data& ed) override {
        m_allocator.delete_object(this, ed);
        return nullptr;
    }
};

inline constexpr std::size_t serial_sort_cutoff = 2000;

template <typename RandomAccessIterator, typename DifferenceType, typename Compare>
DifferenceType median_of_three( RandomAccessIterator array, DifferenceType l, DifferenceType m, DifferenceType r, Compare comp ) {
    return comp(array[l], array[m]) ? ( comp(array[m], array[r]) ? m : ( comp(array[l], array[r]) ? r : l ) )
                                    : ( comp(array[r], array[m]) ? m : ( comp(array[r], array[l]) ? r : l ) );
}

template <typename RandomAccessIterator, typename DifferenceType, typename Compare>
DifferenceType pseudo_median_of_nine( RandomAccessIterator array, DifferenceType n, Compare comp ) {
    DifferenceType offset = n / 8u;
    return median_of_three(array,
                        median_of_three(array, DifferenceType(0) , offset, offset * 2, comp),
                        median_of_three(array, offset * 3, offset * 4, offset * 5, comp),
                        median_of_three(array, offset * 6, offset * 7, n - 1, comp),
                        comp);

}

template <typename RandomAccessIterator, typename Compare>
void parallel_quick_sort(RandomAccessIterator first, RandomAccessIterator last, Compare comp,
                         wait_context& wait_ctx, task_group_context& ctx,
                         std::size_t budget, bool leftmost)
{
    using diff_type = typename std::iterator_traits<RandomAccessIterator>::difference_type;
    using value_type = typename std::iterator_traits<RandomAccessIterator>::value_type;

    while (last - first > diff_type(serial_sort_cutoff)) {
        // Choose pivot
        diff_type pivot = pseudo_median_of_nine(first, last - first, comp);

        // Place pivot at the beginning of the range
        if (pivot != 0) std::iter_swap(first, first + pivot);

        if (!leftmost && !comp(*(first - 1), *first)) {
            // Different pivot?
            value_type pivot = *first;
            auto leq = [&](const value_type& x) { return !comp(pivot, x); };

            std::size_t ptasks = choose_task_count(last - first, budget, choose_block_size(last - first));

            auto it = ptasks >= 2 ? parallel_partition(first, last, leq, ptasks, ctx)
                                  : std::partition(first, last, leq);
            first = it;
        } else {
            value_type pivot = *first;
            auto less = [&](const value_type& x) { return comp(x, pivot); };

            std::size_t ptasks = choose_task_count(last - first, budget, choose_block_size(last - first));

            auto it = ptasks >= 2 ? parallel_partition(std::next(first), last, less, ptasks, ctx)
                                  : std::partition(std::next(first), last, less);

            RandomAccessIterator pivot_pos;
            if (it == std::next(first)) {
                pivot_pos = first;
            } else {
                std::iter_swap(std::prev(it), first);
                pivot_pos = std::prev(it);
            }

            diff_type left_size = pivot_pos - first;
            diff_type right_size = last - (pivot_pos + 1);

            if (left_size == 0) {
                first = pivot_pos + 1;
                leftmost = false;
                continue;
            }

            if (right_size == 0) {
                last = pivot_pos;
                continue;
            }

            diff_type total = left_size + right_size;

            std::size_t left_budget = std::max<std::size_t>(1, budget * left_size / total);
            std::size_t right_budget = std::max<std::size_t>(1, budget - left_budget);

            // Spawn the bigger part, continue with the smaller part
            small_object_allocator allocator;
            using quick_sort_task = ParallelQuickSortTask<RandomAccessIterator, Compare>;

            if (left_size > right_size) {
                spawn(*allocator.new_object<quick_sort_task>(first, pivot_pos, comp, wait_ctx, ctx, left_budget, leftmost, allocator), ctx);
                first = pivot_pos + 1;
                budget = right_budget;
                leftmost = false;
            } else {
                spawn(*allocator.new_object<quick_sort_task>(pivot_pos + 1, last, comp, wait_ctx, ctx, right_budget, /*leftmost = */false, allocator), ctx);
                last = pivot_pos;
                budget = left_budget;
            }
        }
    }

    std::sort(first, last, comp);
}

template <typename RandomAccessIterator, typename Compare>
struct qsort_range {
    RandomAccessIterator m_first;
    RandomAccessIterator m_last;
    const Compare&       m_comp;
    task_group_context&  m_ctx;
    std::size_t          m_budget;
    bool                 m_leftmost;

    qsort_range(RandomAccessIterator first, RandomAccessIterator last, const Compare& comp,
                task_group_context& ctx, std::size_t budget)
        : m_first(first)
        , m_last(last)
        , m_comp(comp)
        , m_ctx(ctx)
        , m_budget(budget)
        , m_leftmost(true)
    {}

    qsort_range(qsort_range& range, split)
        : m_comp(range.m_comp)
        , m_ctx(range.m_ctx)
    {
        range.split_range(*this);
    }

    bool empty() const { return m_first == m_last; }
    bool is_divisible() const { return (m_last - m_first) >= serial_sort_cutoff; }

    void split_range(qsort_range& range) {
        if (!is_divisible()) { // Can happen during the recursive calls to split_range
            range.m_first = m_last;
            range.m_last  = m_last;
            range.m_budget = m_budget;
            range.m_leftmost = false;
            return;
        }

        using diff_type = typename std::iterator_traits<RandomAccessIterator>::difference_type;
        using value_type = typename std::iterator_traits<RandomAccessIterator>::value_type;

        diff_type pivot = pseudo_median_of_nine(m_first, m_last - m_first, m_comp);

        if (pivot != 0) std::iter_swap(m_first, m_first + pivot);

        if (!m_leftmost && !m_comp(*(m_first - 1), *m_first)) {
            value_type pivot = *m_first;
            auto leq = [&](const value_type& x) { return !m_comp(pivot, x); };

            std::size_t ptasks = choose_task_count(m_last - m_first, m_budget, choose_block_size(m_last - m_first));

            auto it = ptasks >= 2 ? parallel_partition(m_first, m_last, leq, ptasks, m_ctx)
                                  : std::partition(m_first, m_last, leq);
            m_first = it;
            split_range(range);
        } else {
            value_type pivot = *m_first;
            auto less = [&](const value_type& x) { return m_comp(x, pivot); };

            std::size_t ptasks = choose_task_count(m_last - m_first, m_budget, choose_block_size(m_last - m_first));

            auto it = ptasks >= 2 ? parallel_partition(std::next(m_first), m_last, less, ptasks, m_ctx)
                                  : std::partition(std::next(m_first), m_last, less);

            RandomAccessIterator pivot_pos;
            if (it == std::next(m_first)) {
                pivot_pos = m_first;
            } else {
                std::iter_swap(std::prev(it), m_first);
                pivot_pos = std::prev(it);
            }

            diff_type left_size = pivot_pos - m_first;
            diff_type right_size = m_last - (pivot_pos + 1);

            if (left_size == 0) {
                m_first = pivot_pos + 1;
                m_leftmost = false;
                split_range(range);
                return;
            }

            if (right_size == 0) {
                m_last = pivot_pos;
                split_range(range);
                return;
            }

            diff_type total = left_size + right_size;

            std::size_t left_budget = std::max<std::size_t>(1, m_budget * left_size / total);
            std::size_t right_budget = std::max<std::size_t>(1, m_budget - left_budget);

            if (left_size > right_size) {
                range.m_first = m_first;
                range.m_last = pivot_pos;
                range.m_budget = left_budget;
                range.m_leftmost = m_leftmost;

                m_first = pivot_pos + 1;
                m_budget = right_budget;
                m_leftmost = false;
            } else {
                range.m_first = pivot_pos + 1;
                range.m_last = m_last;
                range.m_budget = right_budget;
                range.m_leftmost = false;

                m_last = pivot_pos;
                m_budget = left_budget;
            }
        }
    }
};

template <typename RandomAccessIterator, typename Compare>
void parallel_for_quick_sort(RandomAccessIterator first, RandomAccessIterator last, Compare comp,
                             task_group_context& ctx, std::size_t budget)
{
    using range_type = qsort_range<RandomAccessIterator, Compare>;
    range_type range(first, last, comp, ctx, budget);

    auto leaf_sort = [](const range_type& r) {
        std::sort(r.m_first, r.m_last, r.m_comp);
    };

    parallel_for(range, leaf_sort, auto_partitioner(), ctx);
}

template <typename RandomAccessIterator, typename Compare>
void parallel_qsort(RandomAccessIterator first, RandomAccessIterator last, Compare comp) {
    using diff_type = typename std::iterator_traits<RandomAccessIterator>::difference_type;
    
    diff_type n = last - first;

    if (n < serial_sort_cutoff) {
        std::sort(first, last, comp);
    } else {
        std::size_t parallel_partition_budget = this_task_arena::max_concurrency();

        task_group_context ctx;
        wait_context wait_ctx(0);

        parallel_quick_sort(first, last, comp, wait_ctx, ctx, parallel_partition_budget, /*leftmost = */true);
        wait(wait_ctx, ctx);
    }
}

template <typename RandomAccessIterator, typename Compare>
void parallel_for_qsort(RandomAccessIterator first, RandomAccessIterator last, Compare comp) {
    auto n = last - first;

    if (n < serial_sort_cutoff) {
        std::sort(first, last, comp);
    } else {
        std::size_t parallel_partition_budget = this_task_arena::max_concurrency();
        task_group_context ctx;

        parallel_for_quick_sort(first, last, comp, ctx, parallel_partition_budget);
    }
}

template <typename RandomAccessIterator, typename Compare>
void parallel_for_qsort_precheck(RandomAccessIterator first, RandomAccessIterator last, Compare comp) {
    auto n = last - first;

    if (n < serial_sort_cutoff) {
        std::sort(first, last, comp);
    } else {
        constexpr std::size_t first_touch_cutoff = 9;

        RandomAccessIterator k = first;
        while (k != first + first_touch_cutoff) {
            if (comp(*(k + 1), *k)) {
                std::size_t parallel_partition_budget = this_task_arena::max_concurrency();
                task_group_context ctx;

                parallel_for_quick_sort(first, last, comp, ctx, parallel_partition_budget);
                return;
            }
            ++k;
        }

        task_group_context check_context;

        // Full check
        using range_type = blocked_range<RandomAccessIterator>;
        parallel_for(range_type(k + 1, last),
                     [&](const range_type& range) {
                        RandomAccessIterator my_end = range.end();

                        std::size_t i = 0;
                        for (RandomAccessIterator k = range.begin(); k != my_end; ++k, ++i) {
                            if (i % 64 == 0 && check_context.is_group_execution_cancelled()) break;

                            if (comp(*(k), *(k - 1))) {
                                check_context.cancel_group_execution();
                                break;
                            }
                        }
                     }, auto_partitioner(), check_context);

        if (check_context.is_group_execution_cancelled()) {
            std::size_t parallel_partition_budget = this_task_arena::max_concurrency();
            task_group_context ctx;

            parallel_for_quick_sort(first, last, comp, ctx, parallel_partition_budget);
        }
    }
}

template <typename RandomAccessIterator, typename Compare>
struct qsort_range_new {
    RandomAccessIterator m_first;
    RandomAccessIterator m_last;
    const Compare&       m_comp;
    task_group_context&  m_ctx;
    bool                 m_leftmost;

    qsort_range_new(RandomAccessIterator first, RandomAccessIterator last, const Compare& comp,
                    task_group_context& ctx)
        : m_first(first)
        , m_last(last)
        , m_comp(comp)
        , m_ctx(ctx)
        , m_leftmost(true)
    {}

    qsort_range_new(qsort_range_new& range, split)
        : m_comp(range.m_comp)
        , m_ctx(range.m_ctx)
    {
        range.split_range(*this);
    }

    bool empty() const { return m_first == m_last; }
    bool is_divisible() const { return (m_last - m_first) >= serial_sort_cutoff; }

    void split_range(qsort_range_new& range) {
        if (!is_divisible()) {  // Can happen during the recursive calls to split_range
            range.m_first = m_last;
            range.m_last = m_last;
            range.m_leftmost = false;
            return;
        }

        using traits = std::iterator_traits<RandomAccessIterator>;
        using diff_type = typename traits::difference_type;
        using value_type = typename traits::value_type;

        diff_type pivot = pseudo_median_of_nine(m_first, m_last - m_first, m_comp);

        if (pivot != 0) std::iter_swap(m_first, m_first + pivot);

        if (!m_leftmost && !m_comp(*(m_first - 1), *m_first)) {
            // Pivot is the same as the previous pivot
            value_type pivot = *m_first;
            auto leq = [&](const value_type& x) {
                return !m_comp(pivot, x);
            };

            auto it = parallel_partition_new(m_first, m_last, leq, m_ctx);
            m_first = it;
            split_range(range);
        } else {
            value_type pivot = *m_first;

            auto less = [&](const value_type& x) {
                return m_comp(x, pivot);
            };

            auto it = parallel_partition_new(std::next(m_first), m_last, less, m_ctx);

            RandomAccessIterator pivot_pos;
            if (it == std::next(m_first)) {
                pivot_pos = m_first;
            } else {
                std::iter_swap(std::prev(it), m_first);
                pivot_pos = std::prev(it);
            }

            diff_type left_size = pivot_pos - m_first;
            diff_type right_size = m_last - (pivot_pos + 1);

            if (left_size == 0) {
                m_first = pivot_pos + 1;
                m_leftmost = false;
                split_range(range);
                return;
            }

            if (right_size == 0) {
                m_last = pivot_pos;
                split_range(range);
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
void parallel_for_quick_sort_new(RandomAccessIterator first, RandomAccessIterator last, Compare comp,
                                 task_group_context& ctx)
{
    using range_type = qsort_range_new<RandomAccessIterator, Compare>;
    range_type range(first, last, comp, ctx);

    auto leaf_sort = [](const range_type& r) {
        std::sort(r.m_first, r.m_last, r.m_comp);
    };

    parallel_for(range, leaf_sort, auto_partitioner{}, ctx);
}

template <typename RandomAccessIterator, typename Compare>
void parallel_for_qsort_new(RandomAccessIterator first, RandomAccessIterator last, Compare comp) {
    auto n = last - first;

    if (n < serial_sort_cutoff) {
        std::sort(first, last, comp);
    } else {
        task_group_context ctx;
        parallel_for_quick_sort_new(first, last, comp, ctx);
    }
}

} // namespace d1
} // namespace detail
} // namespace tbb
