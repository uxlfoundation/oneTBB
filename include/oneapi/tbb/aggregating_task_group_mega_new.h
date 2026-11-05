#ifndef __TBB_aggregating_task_group_mega_new_H
#define __TBB_aggregating_task_group_mega_new_H

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
#define DIRECT_SPAWN_THRESHOLD 8

class task_forest;

class tree_task : public d1::task {
    task_forest*                    m_grab_target;
    tree_task*                      m_next_root;
    tree_task*                      m_left_child;
    tree_task*                      m_right_child;
    std::size_t                     m_num_elements;
    d1::wait_context_vertex*        m_tg_wait_context_vertex;
    d1::wait_tree_vertex_interface* m_wait_tree_vertex;
    task_group_context&             m_ctx;

    static void recursive_spawn(tree_task* head, d1::wait_tree_vertex_interface* vertex) {
        if (head != nullptr) {
            tree_task* left = head->left_child();
            tree_task* right = head->right_child();
            head->left_child() = nullptr;
            head->right_child() = nullptr;
            head->num_elements() = 1;
            head->wait_tree_vertex() = vertex;
            r1::spawn(*head, head->ctx());
            recursive_spawn(left, vertex);
            recursive_spawn(right, vertex);
        }
    }

    static void spawn_root(tree_task* root, d1::wait_tree_vertex_interface* vertex) {
        if (root->num_elements() > TASK_TREE_GRAINSIZE) {
            root->wait_tree_vertex() = vertex;
            vertex->reserve();
            r1::spawn(*root, root->ctx());
        } else {
            vertex->reserve(static_cast<std::uint32_t>(root->num_elements()));
            recursive_spawn(root, vertex);
        }
    }

    // Thieves steal from the oldest end of the deque, so the biggest trees have to be spawned
    // first. The spine is built smallest-first, so it is walked to the tail and spawned on the
    // way back. Depth is the spine length, O(log n)
    static void spawn_spine(tree_task* root, d1::wait_tree_vertex_interface* vertex) {
        if (root != nullptr) {
            tree_task* next = root->next_root();
            root->next_root() = nullptr;
            spawn_spine(next, vertex);
            spawn_root(root, vertex);
        }
    }

    static void spawn_child(tree_task*& child, d1::wait_tree_vertex_interface* vertex) {
        if (child != nullptr) {
            if (child->num_elements() > TASK_TREE_GRAINSIZE) {
                child->wait_tree_vertex() = vertex;
                vertex->reserve();
                r1::spawn(*child, child->ctx());
            } else {
                vertex->reserve(static_cast<std::uint32_t>(child->num_elements()));
                recursive_spawn(child, vertex);
            }
            child = nullptr;
        }
    }
protected:
    d1::small_object_allocator      m_allocator;

    void distribute_tasks() {
        if (m_grab_target == nullptr && m_left_child == nullptr && m_right_child == nullptr) return;

        d1::wait_tree_vertex_interface* vertex = r1::get_thread_reference_vertex(m_tg_wait_context_vertex);
        grab_and_redistribute_tasks(vertex);
        spawn_child(m_left_child, vertex);
        spawn_child(m_right_child, vertex);
    }
public:
    tree_task(d1::wait_context_vertex* tg_wait_context_vertex,
              task_group_context& ctx, d1::small_object_allocator& allocator)
        : m_grab_target(nullptr)
        , m_next_root(nullptr)
        , m_left_child(nullptr)
        , m_right_child(nullptr)
        , m_num_elements(1) // the tree initially contains only this task
        , m_tg_wait_context_vertex(tg_wait_context_vertex)
        , m_wait_tree_vertex(nullptr)
        , m_ctx(ctx)
        , m_allocator(allocator)
    {}

    ~tree_task() override {
        __TBB_ASSERT(m_wait_tree_vertex != nullptr, nullptr);
        m_wait_tree_vertex->release();
    }

    tree_task*& next_root()   { return m_next_root; }
    tree_task*& left_child()  { return m_left_child; }
    tree_task*& right_child() { return m_right_child; }
    
    std::size_t& num_elements() { return m_num_elements; }
    
    task_group_context& ctx() { return m_ctx; }

    d1::wait_tree_vertex_interface*& wait_tree_vertex() { return m_wait_tree_vertex; }
    task_forest*& grab_target() { return m_grab_target; }

    void grab_and_redistribute_tasks(d1::wait_tree_vertex_interface* vertex);
};

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

    d1::task* execute(d1::execution_data& ed) override {
        __TBB_ASSERT(ed.context == &this->ctx(), "The task_group context should be used for all tasks");
        this->distribute_tasks();

        m_function();
        m_allocator.delete_object(this, ed);
        return nullptr;
    }
    
    d1::task* cancel(d1::execution_data& ed) override {
        this->distribute_tasks();

        m_allocator.delete_object(this, ed);
        return nullptr;
    }
};

class task_forest {
    std::atomic<tree_task*> m_head;
    std::size_t m_num_direct_spawns;

    static tree_task* locked_flag() {
        static constexpr std::uintptr_t locked_flag_value = ~std::uintptr_t(1);
        return reinterpret_cast<tree_task*>(locked_flag_value);
    }

    static tree_task* grabber_spawned_flag() {
        static constexpr std::uintptr_t grabber_spawned_flag_value = ~std::uintptr_t(2);
        return reinterpret_cast<tree_task*>(grabber_spawned_flag_value);
    }

    tree_task* wait_while_locked() {
        return spin_wait_while_eq(m_head, locked_flag());
    }

    tree_task* lock() {
        return m_head.exchange(locked_flag());
    }

    void unlock(tree_task* head) {
        __TBB_ASSERT(head != locked_flag(), nullptr);
        tree_task* prev = m_head.exchange(head);
        __TBB_ASSERT(prev == locked_flag(), nullptr);
        suppress_unused_warning(prev);
    }
public:
    task_forest() : m_head(nullptr), m_num_direct_spawns(0) {}

    template <typename Function>
    void push(Function&& function, d1::wait_context_vertex& wait_vertex, task_group_context& ctx) {
        d1::small_object_allocator alloc;

        using task_type = function_tree_task<typename std::decay<Function>::type>;
        task_type* new_task = alloc.new_object<task_type>(std::forward<Function>(function), &wait_vertex, ctx, alloc);

        // While no batch is in flight and the backlog is still small, publish the task exactly as
        // task_group::run would: no trigger, no forest, no lock
        if (m_num_direct_spawns < DIRECT_SPAWN_THRESHOLD &&
            m_head.load(std::memory_order_relaxed) == nullptr)
        {
            ++m_num_direct_spawns;
            new_task->wait_tree_vertex() = r1::get_thread_reference_vertex(&wait_vertex);
            new_task->wait_tree_vertex()->reserve();
            r1::spawn(*new_task, ctx);
            return;
        }

        // Lock the forest to prevent races with grab_all
        tree_task* current_head = lock();
        __TBB_ASSERT(current_head != locked_flag(), "The task tree is already locked");

        if (current_head == nullptr) {
            m_num_direct_spawns = 0;
            new_task->grab_target() = this;
            new_task->wait_tree_vertex() = r1::get_thread_reference_vertex(&wait_vertex);
            new_task->wait_tree_vertex()->reserve();
            unlock(grabber_spawned_flag());
            r1::spawn(*new_task, ctx);
        } else {
            if (current_head == grabber_spawned_flag()) {
                // First task in the forest
                current_head = new_task;
            } else if (current_head->next_root() == nullptr ||
                       current_head->next_root()->num_elements() != current_head->num_elements()) {
                // Only one root or two roots of different size
                // Append new root
                new_task->next_root() = current_head;
                current_head = new_task;
            } else { // Two roots of equivalent size
                // Merge two trees
                tree_task* next_tree = current_head->next_root();
                new_task->left_child() = current_head;
                new_task->right_child() = next_tree;
                new_task->next_root() = next_tree->next_root();

                current_head->next_root() = nullptr;
                next_tree->next_root() = nullptr;

                new_task->num_elements() += current_head->num_elements();
                new_task->num_elements() += next_tree->num_elements();

                current_head = new_task;
            }
            unlock(current_head);
        }
    }

    tree_task* grab_all() {
        tree_task* current_head = wait_while_locked();

        while (!m_head.compare_exchange_strong(current_head, nullptr)) {
            current_head = wait_while_locked();
        }
        __TBB_ASSERT(current_head != locked_flag(), "Incorrect grab_all logic");
        return current_head != grabber_spawned_flag() ? current_head : nullptr;
    }
};

inline void tree_task::grab_and_redistribute_tasks(d1::wait_tree_vertex_interface* vertex) {
    if (m_grab_target != nullptr) {
        tree_task* head = m_grab_target->grab_all();
        m_grab_target = nullptr;

        spawn_spine(head, vertex);
    }
}

class aggregating_task_group {
private:
    d1::wait_context_vertex m_wait_vertex;
    task_group_context m_context;
    static thread_local task_forest m_task_forest;

    task_group_context& context() noexcept {
        return m_context.actual_context();
    }
public:
    aggregating_task_group()
        : m_wait_vertex(0)
        , m_context(task_group_context::bound,
                    task_group_context::default_traits | task_group_context::concurrent_wait)
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
        m_task_forest.push(std::forward<F>(f), m_wait_vertex, context());
    }
}; // class aggregating_task_group

inline thread_local task_forest aggregating_task_group::m_task_forest;

} // namespace d2
} // namespace detail

inline namespace v1 {
using detail::d2::aggregating_task_group;
} // inline namespace v1

} // namespace tbb

#endif // __TBB_aggregating_task_group_mega_new_H
