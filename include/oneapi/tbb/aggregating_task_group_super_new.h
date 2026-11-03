#ifndef __TBB_aggregating_task_group_super_new_H
#define __TBB_aggregating_task_group_super_new_H

#include "detail/_config.h"
#include "detail/_namespace_injection.h"
#include "detail/_assert.h"
#include "detail/_utils.h"
#include "detail/_task.h"
#include "detail/_small_object_pool.h"
#include "detail/_task_handle.h"
#include "task_group.h"
#include "task_arena.h"
#include <atomic>

namespace tbb {
namespace detail {
namespace d2 {

#define TASK_TREE_GRAINSIZE 128
#define DIRECT_SPAWN_THRESHOLD 4

class binary_task_tree;

class tree_task : public d1::task {
    binary_task_tree*               m_grab_target;
    tree_task*                      m_left_child;
    tree_task*                      m_right_child;
    std::size_t                     m_num_subtree_elements;
    d1::wait_context_vertex*        m_tg_wait_context_vertex;
    d1::wait_tree_vertex_interface* m_wait_tree_vertex;
    task_group_context&             m_ctx;
protected:
    d1::small_object_allocator      m_allocator;
public:
    tree_task(d1::wait_context_vertex* tg_wait_context_vertex,
              task_group_context& ctx, d1::small_object_allocator& allocator)
        : m_grab_target(nullptr)
        , m_left_child(nullptr)
        , m_right_child(nullptr)
        , m_num_subtree_elements(0)
        , m_tg_wait_context_vertex(tg_wait_context_vertex)
        , m_wait_tree_vertex(nullptr)
        , m_ctx(ctx)
        , m_allocator(allocator)
    {}

    ~tree_task() override {
        __TBB_ASSERT(m_wait_tree_vertex != nullptr, nullptr);
        m_wait_tree_vertex->release();
    }

    tree_task*& left_child() { return m_left_child; }
    tree_task*& right_child() { return m_right_child; }
    std::size_t& num_subtree_elements() { return m_num_subtree_elements; }
    task_group_context& ctx() { return m_ctx; }
    d1::wait_tree_vertex_interface*& wait_tree_vertex() { return m_wait_tree_vertex; }
    binary_task_tree*& grab_target() { return m_grab_target; }

    void grab_and_redistribute_children();

    static void recursive_spawn(tree_task* tree_head, d1::wait_tree_vertex_interface* vertex) {
        if (tree_head != nullptr) {
            tree_task* left = tree_head->left_child();
            tree_task* right = tree_head->right_child();
            tree_head->left_child() = nullptr;
            tree_head->right_child() = nullptr;
            // The children are detached, so the task must not try to split itself when executed
            tree_head->num_subtree_elements() = 0;

            // The reference on the vertex is already reserved in bulk by the caller
            tree_head->wait_tree_vertex() = vertex;

            r1::spawn(*tree_head, tree_head->ctx());
            recursive_spawn(left, vertex);
            recursive_spawn(right, vertex);
        }
    }
}; // class tree_task

template <typename Function>
class function_tree_task : public tree_task {
    const Function m_function; // TODO: EBO
public:
    template <typename F>
    function_tree_task(F&& function, d1::wait_context_vertex* tg_wait_context_vertex,
                       task_group_context& ctx, d1::small_object_allocator& allocator)
        : tree_task(tg_wait_context_vertex, ctx, allocator)
        , m_function(std::forward<F>(function))
    {}

    ~function_tree_task() {}

    d1::task* execute(d1::execution_data& ed) override {
        __TBB_ASSERT(ed.context == &this->ctx(), "The task group context should be used for all tasks");

        this->grab_and_redistribute_children();
        // If the body throws, the dispatcher cancels the group and calls cancel() on this same
        // task, which performs the cleanup. Destroying the task here would be a use after free
        m_function();
        m_allocator.delete_object(this, ed);
        return nullptr;
    }

    d1::task* cancel(d1::execution_data& ed) override {
        // The buffered tasks are still published so that the scheduler cancels them in turn
        this->grab_and_redistribute_children();
        m_allocator.delete_object(this, ed);
        return nullptr;
    }
};

class binary_task_tree {
    static tree_task* locked_flag() {
        static constexpr std::uintptr_t locked_flag_value = ~std::uintptr_t(1);
        return reinterpret_cast<tree_task*>(locked_flag_value);
    }

    std::atomic<tree_task*> m_head;
    // Accessed only under the lock, reset on every new head
    std::size_t m_num_direct_spawns;

    // TODO: O(log n) on the producer hot path. Consider an O(1) intrusive list push here and
    // building the balanced tree on the consumer side, in grab_all
    static void add_task_to_subtree(tree_task* subtree_head, tree_task* new_task) {
        __TBB_ASSERT(subtree_head != nullptr, nullptr);
        if (subtree_head->left_child() == nullptr) {
            subtree_head->left_child() = new_task;
        } else if (subtree_head->right_child() == nullptr) {
            subtree_head->right_child() = new_task;
        } else if (subtree_head->left_child()->num_subtree_elements() <=
                   subtree_head->right_child()->num_subtree_elements()) {
            add_task_to_subtree(subtree_head->left_child(), new_task);
        } else {
            add_task_to_subtree(subtree_head->right_child(), new_task);
        }
        ++subtree_head->num_subtree_elements();
    }

    tree_task* wait_while_locked() { return spin_wait_while_eq(m_head, locked_flag()); }

    tree_task* lock() { return m_head.exchange(locked_flag()); }

    void unlock(tree_task* head) {
        tree_task* prev = m_head.exchange(head);
        __TBB_ASSERT(prev == locked_flag(), nullptr);
        suppress_unused_warning(prev);
    }
public:
    binary_task_tree() : m_head(nullptr), m_num_direct_spawns(0) {}

    template <typename Function>
    void add_task(Function&& function, d1::wait_context_vertex& wait_vertex, task_group_context& ctx) {
        d1::small_object_allocator alloc;

        using task_type = function_tree_task<typename std::decay<Function>::type>;
        task_type* new_task = alloc.new_object<task_type>(std::forward<Function>(function), &wait_vertex, ctx, alloc);

        // Lock the task tree to prevent races with grab_all.
        // The tree is thread-local, so only the owner thread takes the lock and it is never contended here
        tree_task* current_task_tree = lock();
        __TBB_ASSERT(current_task_tree != locked_flag(), "The task tree is already locked by the owner thread");

        bool spawn_new_task = true;

        if (current_task_tree == nullptr) {
            current_task_tree = new_task;
            new_task->grab_target() = this;
            m_num_direct_spawns = 0;
        } else if (m_num_direct_spawns < DIRECT_SPAWN_THRESHOLD) {
            // The backlog is still small, so publish the task as is to give the thieves more entry points
            ++m_num_direct_spawns;
        } else {
            // Aggregating
            // Special handling for the first layer of the tree
            __TBB_ASSERT(current_task_tree->right_child() == nullptr, "Broken structure of the tree");

            if (current_task_tree->left_child() == nullptr) {
                current_task_tree->left_child() = new_task;
            } else {
                add_task_to_subtree(current_task_tree->left_child(), new_task);
            }
            ++current_task_tree->num_subtree_elements();
            spawn_new_task = false;
        }

        if (spawn_new_task) {
            new_task->wait_tree_vertex() = r1::get_thread_reference_vertex(&wait_vertex);
            new_task->wait_tree_vertex()->reserve();
        }

        unlock(current_task_tree);

        // Spawned after the unlock: a thief executing the head calls grab_all, which would spin until then
        if (spawn_new_task) {
            r1::spawn(*new_task, ctx);
        }
    }

    tree_task* grab_all() {
        tree_task* current_head = wait_while_locked();

        while (!m_head.compare_exchange_strong(current_head, nullptr)) {
            __TBB_ASSERT(current_head == locked_flag(), nullptr);
            current_head = wait_while_locked();
        }
        __TBB_ASSERT(current_head != locked_flag(), "Incorrect grab_all");
        return current_head;
    }

    static tree_task* split(tree_task* head) {
        __TBB_ASSERT(head != nullptr, "Tree is not divisible");
        __TBB_ASSERT(head->num_subtree_elements() + 1 > TASK_TREE_GRAINSIZE, "Tree is not divisible");
        __TBB_ASSERT(head->left_child() != nullptr && head->left_child()->left_child() != nullptr &&
                     head->left_child()->right_child() != nullptr, "Broken structure of the tree");
        __TBB_ASSERT(head->right_child() == nullptr, "Broken structure of the tree");

#if TBB_USE_ASSERT
        std::size_t initial_num_elements = head->num_subtree_elements();
#endif

        tree_task* second_tree = head->left_child();
        head->left_child() = second_tree->right_child();

        head->num_subtree_elements() = head->left_child()->num_subtree_elements() + 1;
        second_tree->num_subtree_elements() = second_tree->left_child()->num_subtree_elements() + 1;
        second_tree->right_child() = nullptr;

        __TBB_ASSERT(head->num_subtree_elements() + second_tree->num_subtree_elements() == initial_num_elements - 1, "Incorrect split");
        return second_tree;
    }
};

// TODO: the tasks are redistributed into the arena the grabbing task happens to run in, which
// may differ from the arena they were submitted from. Define the expected behavior here
inline void tree_task::grab_and_redistribute_children() {
    if (m_grab_target != nullptr) {
        // Current task is the head of the initial task tree
        // Grab all tasks in the tree
        tree_task* head = m_grab_target->grab_all();
        __TBB_ASSERT(head == this, "Incorrect task tree structure");
        suppress_unused_warning(head);
        m_grab_target = nullptr;
    }

    if (num_subtree_elements() == 0) return;

    // Looked up once for the whole batch, the tasks share it and each releases one reference when destroyed
    d1::wait_tree_vertex_interface* vertex = r1::get_thread_reference_vertex(m_tg_wait_context_vertex);

    // Redistribution stage
    while (num_subtree_elements() + 1 > TASK_TREE_GRAINSIZE) {
        tree_task* splitted_tree_head = binary_task_tree::split(this);
        splitted_tree_head->wait_tree_vertex() = vertex;
        // The elements carried away by the split are reserved by the split head itself
        vertex->reserve();
        r1::spawn(*splitted_tree_head, m_ctx);
    }

    std::size_t num_children = num_subtree_elements();

    if (num_children != 0) {
        vertex->reserve(static_cast<std::uint32_t>(num_children));

        tree_task* left = left_child();
        tree_task* right = right_child();
        left_child() = nullptr;
        right_child() = nullptr;
        num_subtree_elements() = 0;

        recursive_spawn(left, vertex);
        recursive_spawn(right, vertex);
    }
}

class aggregating_task_group {
private:
    d1::wait_context_vertex m_wait_vertex;
    task_group_context  m_context;
    static thread_local binary_task_tree m_task_tree;

    task_group_context& context() noexcept {
        return m_context.actual_context();
    }
public:
    aggregating_task_group()
        : m_wait_vertex(0)
        , m_context(task_group_context::bound, task_group_context::default_traits | task_group_context::concurrent_wait)
    {}

    aggregating_task_group(task_group_context& ctx)
        : m_wait_vertex(0)
        , m_context(&ctx)
    {}

    ~aggregating_task_group() noexcept(false) {
        if (m_wait_vertex.continue_execution()) {
#if __TBB_CPP17_UNCAUGHT_EXCEPTIONS_PRESENT
            bool stack_unwinding_in_progress = std::uncaught_exceptions() > 0;
#else
            bool stack_unwinding_in_progress = std::uncaught_exception();
#endif

            // Always attempt to do proper cleanup to avoid inevitable memory corruption
            // in case of missing wait (for the sake of better testability and debuggability)
            if (!context().is_group_execution_cancelled()) {
                cancel();
            }
            r1::wait(m_wait_vertex.get_context(), context());
            if (!stack_unwinding_in_progress) {
                throw_exception(exception_id::missing_wait);
            }
        }
    }

    void cancel() {
        context().cancel_group_execution();
    }

    task_group_status wait() {
        bool cancellation_status = false;
        try_call([&] {
            r1::wait(m_wait_vertex.get_context(), context());
        }).on_completion([&] {
            cancellation_status = context().is_group_execution_cancelled();
        });
        return cancellation_status ? canceled : complete;
    }

    template <typename F>
    void run(F&& f) {
        m_task_tree.add_task(std::forward<F>(f), m_wait_vertex, context());
    }
}; // class aggregating_task_group

inline thread_local binary_task_tree aggregating_task_group::m_task_tree;

} // namespace d2
} // namespace detail

inline namespace v1 {
using detail::d2::aggregating_task_group;
} // inline namespace v1

} // namespace tbb

#endif // __TBB_aggregating_task_group_super_new_H
