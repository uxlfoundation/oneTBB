#include "oneapi/tbb/detail/_config.h"
#include "oneapi/tbb/detail/_utils.h"
#include "oneapi/tbb/detail/_template_helpers.h"
#include "oneapi/tbb/detail/_task.h"
#include "oneapi/tbb/detail/_small_object_pool.h"
#include "oneapi/tbb/task_group.h"
#include "oneapi/tbb/task_arena.h"

#include <atomic>
#include <cstdint>
#include <iostream>

namespace tbb {
namespace detail {
namespace r1 {

static constexpr std::uintptr_t locked_flag = ~std::uintptr_t(1);

class tree_task_wrapper;

inline tree_task_wrapper* lock_task(std::atomic<tree_task_wrapper*>& task);
inline tree_task_wrapper* wait_while_task_locked(std::atomic<tree_task_wrapper*>& task);
inline void unlock_task(std::atomic<tree_task_wrapper*>& task, tree_task_wrapper* value);

class tree_task_wrapper : public d1::task {
public:
    d1::task*                       m_real_task;
    std::atomic<tree_task_wrapper*> m_left_task;
    std::atomic<tree_task_wrapper*> m_right_task;
    std::size_t                     m_num_elements;
    d1::task_group_context&         m_ctx;
    d1::small_object_allocator      m_allocator;

    tree_task_wrapper(d1::task* real_task, d1::task_group_context& ctx, d1::small_object_allocator& allocator)
        : m_real_task(real_task)
        , m_left_task(nullptr)
        , m_right_task(nullptr)
        , m_num_elements(1)
        , m_ctx(ctx)
        , m_allocator(allocator)
    {}

    static std::atomic<std::size_t> split_counter;

    ~tree_task_wrapper() override = default;

    // static void validate_subtree_structure(tree_task_wrapper* subtree) {
    //     if (subtree == nullptr) return;

    //     std::size_t num_elements = subtree->m_num_elements;

    //     tree_task_wrapper* left = wait_while_task_locked(subtree->m_left_task);
    //     tree_task_wrapper* right = wait_while_task_locked(subtree->m_right_task);

    //     std::size_t left_elements = left == nullptr ? 0 : left->m_num_elements;
    //     std::size_t right_elements = right == nullptr ? 0 : right->m_num_elements;

    //     __TBB_ASSERT(left_elements + right_elements + 1 == num_elements, nullptr);

    //     validate_subtree_structure(left);
    //     validate_subtree_structure(right);
    // }

    // static void validate_tree_structure(tree_task_wrapper* tree) {
    //     __TBB_ASSERT(tree != nullptr, nullptr);

    //     std::size_t num_elements = tree->m_num_elements;
    //     tree_task_wrapper* left = wait_while_task_locked(tree->m_left_task);

    //     if (left == nullptr) {
    //         __TBB_ASSERT(num_elements == 1, nullptr);
    //         return;
    //     }

    //     __TBB_ASSERT(left->m_num_elements + 1 == num_elements, nullptr);

    //     tree_task_wrapper* left_left = wait_while_task_locked(left->m_left_task);
    //     tree_task_wrapper* left_right = wait_while_task_locked(left->m_right_task);

    //     validate_subtree_structure(left_left);
    //     validate_subtree_structure(left_right);
    // }

    d1::task* execute(d1::execution_data& ed) override {
        // printf("Thread %u is splitting the tree of size %lu\n", tbb::this_task_arena::current_thread_index(), m_num_elements);
        while (m_num_elements >= 4) {
            // validate_tree_structure(this);

#if TBB_USE_DEBUG
            std::size_t num_elements = m_num_elements;
#endif
            // Waiting for pending inserters to pass through required nodes
            tree_task_wrapper* second_tree_head = wait_while_task_locked(m_left_task);
            __TBB_ASSERT(m_right_task.load(std::memory_order_relaxed) == nullptr, nullptr);
            tree_task_wrapper* second_tree_left_task = wait_while_task_locked(second_tree_head->m_left_task);
            tree_task_wrapper* second_tree_right_task = wait_while_task_locked(second_tree_head->m_right_task);

            m_left_task.store(second_tree_right_task, std::memory_order_relaxed);
            m_num_elements -= (second_tree_left_task->m_num_elements + 1);
            second_tree_head->m_num_elements -= second_tree_right_task->m_num_elements;
            second_tree_head->m_right_task.store(nullptr, std::memory_order_relaxed);
            
            __TBB_ASSERT(m_num_elements + second_tree_head->m_num_elements == num_elements, nullptr);

            d1::spawn(*second_tree_head, second_tree_head->m_ctx);
        }

        // validate_tree_structure(this);
        // Non-divisible, spawn real tasks
        d1::task* real_task = m_real_task;
        tree_task_wrapper* left_task = wait_while_task_locked(m_left_task);
        __TBB_ASSERT(m_right_task.load(std::memory_order_relaxed) == nullptr, nullptr);

        if (left_task != nullptr) {
            d1::spawn(*left_task->m_real_task, left_task->m_ctx);
            tree_task_wrapper* left_left_task = wait_while_task_locked(left_task->m_left_task);
            __TBB_ASSERT(left_task->m_right_task.load(std::memory_order_relaxed) == nullptr, nullptr);
            
            left_task->m_allocator.delete_object(left_task, ed);

            if (left_left_task != nullptr) {
                d1::spawn(*left_left_task->m_real_task, left_left_task->m_ctx);
                __TBB_ASSERT(left_left_task->m_left_task.load(std::memory_order_relaxed) == nullptr, nullptr);
                __TBB_ASSERT(left_left_task->m_right_task.load(std::memory_order_relaxed) == nullptr, nullptr);

                left_left_task->m_allocator.delete_object(left_left_task, ed);
            }
        }
        return real_task;
    }

    d1::task* cancel(d1::execution_data& ed) override {
        return execute(ed);
    }
};

inline tree_task_wrapper* lock_task(std::atomic<tree_task_wrapper*>& task) {
    return task.exchange(reinterpret_cast<tree_task_wrapper*>(locked_flag));
}

inline tree_task_wrapper* wait_while_task_locked(std::atomic<tree_task_wrapper*>& task) {
    return spin_wait_while_eq(task, reinterpret_cast<tree_task_wrapper*>(locked_flag));
}

inline void unlock_task(std::atomic<tree_task_wrapper*>& task, tree_task_wrapper* value) {
    task.exchange(value);
}

class grab_task : public d1::task {
    std::atomic<tree_task_wrapper*>& m_task_tree_head;
    d1::small_object_allocator       m_allocator;
public:
    grab_task(std::atomic<tree_task_wrapper*>& task_tree_head, d1::small_object_allocator& allocator)
        : m_task_tree_head(task_tree_head)
        , m_allocator(allocator)
    {}

    ~grab_task() override = default;

    d1::task* execute(d1::execution_data& ed) override {
        tree_task_wrapper* current_task_tree_head = wait_while_task_locked(m_task_tree_head);

        while (!m_task_tree_head.compare_exchange_strong(current_task_tree_head, nullptr)) {
            current_task_tree_head = wait_while_task_locked(m_task_tree_head);
        }

        m_allocator.delete_object(this, ed);

        return current_task_tree_head;
    }

    d1::task* cancel(d1::execution_data& ed) override {
        return execute(ed);
    }
};

class task_aggregator {
    std::atomic<tree_task_wrapper*> m_tree_head;

public:
    task_aggregator() : m_tree_head(nullptr) {}

    static void add_to_subtree(std::atomic<tree_task_wrapper*>& subtree_head,
                               tree_task_wrapper* subtree_head_value,
                               tree_task_wrapper* new_tree_task)
    {
        // subtree_head is locked
        ++subtree_head_value->m_num_elements;
        tree_task_wrapper* left = lock_task(subtree_head_value->m_left_task);

        if (left == nullptr) {
            // task is a new left
            unlock_task(subtree_head_value->m_left_task, new_tree_task);
            unlock_task(subtree_head, subtree_head_value);
        } else {
            tree_task_wrapper* right = lock_task(subtree_head_value->m_right_task);

            if (right == nullptr) {
                // task is a new right
                unlock_task(subtree_head_value->m_right_task, new_tree_task);
                unlock_task(subtree_head_value->m_left_task, left);
                unlock_task(subtree_head, subtree_head_value);
            } else {
                // Both left and right are present
                std::size_t left_count = left->m_num_elements;
                std::size_t right_count = right->m_num_elements;
                if (left_count <= right_count) {
                    // Add to left subtree
                    unlock_task(subtree_head_value->m_right_task, right);
                    auto& left_subtree_head = subtree_head_value->m_left_task;
                    unlock_task(subtree_head, subtree_head_value);
                    add_to_subtree(left_subtree_head, left, new_tree_task);
                } else {
                    // Add to right subtree
                    unlock_task(subtree_head_value->m_left_task, left);
                    auto& right_subtree_head = subtree_head_value->m_right_task;
                    unlock_task(subtree_head, subtree_head_value);
                    add_to_subtree(right_subtree_head, right, new_tree_task);
                }
            }
        }
    }

    void add_task(d1::task& real_task, d1::task_group_context& ctx) {
        d1::small_object_allocator allocator;

        tree_task_wrapper* new_tree_task = allocator.new_object<tree_task_wrapper>(&real_task, ctx, allocator);

        tree_task_wrapper* tree_head = lock_task(m_tree_head);

        if (tree_head == nullptr) {
            // new_tree_task is a new head
            unlock_task(m_tree_head, new_tree_task);
            grab_task* gt = allocator.new_object<grab_task>(m_tree_head, allocator);
            d1::spawn(*gt, ctx);
        } else {
            ++tree_head->m_num_elements;
            auto& left = tree_head->m_left_task;
            tree_task_wrapper* left_task = lock_task(left);
            unlock_task(m_tree_head, tree_head);

            __TBB_ASSERT(tree_head->m_right_task.load(std::memory_order_relaxed) == nullptr, nullptr);

            if (left_task == nullptr) {
                // new_task is the new head->left
                unlock_task(left, new_tree_task);
            } else {
                add_to_subtree(left, left_task, new_tree_task);
            }
        }
    }
}; // class task_aggregator

} // namespace r1
} // namespace detail
} // namespace tbb
