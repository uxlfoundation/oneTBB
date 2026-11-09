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

// Set to 1 to count references per task through a parent chain, as in the original prototype, so
// that a whole batch holds a single reference on the shared wait tree vertex instead of one per task
#ifndef __TBB_AGG_PARENT_REFCOUNT
#define __TBB_AGG_PARENT_REFCOUNT 0
#endif

class task_forest;

class tree_task : public d1::task {
#if __TBB_AGG_PARENT_REFCOUNT
    // A task is either the grabber or a member of a tree, never both, so a single word holds either
    // the forest to grab or the parent. Both pointers are aligned, so the tag bit is free
    static constexpr std::uintptr_t parent_tag = 1;
    std::uintptr_t                  m_link;
    std::atomic<std::size_t>        m_ref_count;
#else
    task_forest*                    m_grab_target;
    d1::wait_context_vertex*        m_tg_wait_context_vertex;
#endif
    tree_task*                      m_next_root;
    tree_task*                      m_left_child;
    tree_task*                      m_right_child;
    std::size_t                     m_num_elements;
    d1::wait_tree_vertex_interface* m_wait_tree_vertex;
    task_group_context&             m_ctx;

    static void recursive_spawn(tree_task* head, d1::wait_tree_vertex_interface* vertex) {
        if (head != nullptr) {
            tree_task* left = head->left_child();
            tree_task* right = head->right_child();
            head->left_child() = nullptr;
            head->right_child() = nullptr;
            head->num_elements() = 1;
#if !__TBB_AGG_PARENT_REFCOUNT
            head->wait_tree_vertex() = vertex;
#endif
            r1::spawn(*head, head->ctx());
            recursive_spawn(left, vertex);
            recursive_spawn(right, vertex);
        }
    }

    static void spawn_root(tree_task* root, d1::wait_tree_vertex_interface* vertex) {
        if (root->num_elements() > TASK_TREE_GRAINSIZE) {
#if !__TBB_AGG_PARENT_REFCOUNT
            root->wait_tree_vertex() = vertex;
            vertex->reserve();
#endif
            r1::spawn(*root, root->ctx());
        } else {
#if !__TBB_AGG_PARENT_REFCOUNT
            vertex->reserve(static_cast<std::uint32_t>(root->num_elements()));
#endif
            recursive_spawn(root, vertex);
        }
    }

    // The forest roots become children of the grabber, which owns the batch's single vertex reference
    static void spawn_spine(tree_task* root, tree_task* parent, d1::wait_tree_vertex_interface* vertex) {
        if (root != nullptr) {
            tree_task* next = root->next_root();
            root->next_root() = nullptr;
            spawn_spine(next, parent, vertex);
#if __TBB_AGG_PARENT_REFCOUNT
            parent->reserve(1);
            root->set_parent(parent);
#endif
            suppress_unused_warning(parent);
            spawn_root(root, vertex);
        }
    }

    static void spawn_task(tree_task* task, d1::wait_tree_vertex_interface* vertex) {
        if (task != nullptr) {
            if (task->num_elements() > TASK_TREE_GRAINSIZE) {
#if !__TBB_AGG_PARENT_REFCOUNT
                task->wait_tree_vertex() = vertex;
                vertex->reserve();
#endif
                r1::spawn(*task, task->ctx());
            } else {
#if !__TBB_AGG_PARENT_REFCOUNT
                vertex->reserve(static_cast<std::uint32_t>(task->num_elements()));
#endif
                recursive_spawn(task, vertex);
            }
        }
    }
protected:
    d1::small_object_allocator      m_allocator;

    void distribute_tasks() {
        if (grab_target() == nullptr && m_left_child == nullptr && m_right_child == nullptr) return;

#if __TBB_AGG_PARENT_REFCOUNT
        // Every published piece is already parented and accounted for, so no vertex is needed here
        d1::wait_tree_vertex_interface* vertex = nullptr;
#else
        d1::wait_tree_vertex_interface* vertex = r1::get_thread_reference_vertex(m_tg_wait_context_vertex);
#endif
        // Distribute tasks if we are the grabber
        grab_and_redistribute_tasks(vertex);

        // Distribute tasks if we are the tree
        while (m_num_elements > TASK_TREE_GRAINSIZE) {
            // TODO: only the first split may have a perfect tree. Consider moving it out the loop
            __TBB_ASSERT(m_left_child != nullptr, "Broken tree structure");
            if (m_right_child == nullptr) {
                // We are the balanced tree
                // Split itself into two balanced trees
                tree_task* old_left_child = m_left_child;
                m_left_child = m_left_child->m_right_child;
                old_left_child->m_right_child = nullptr;

                // Updated before the spawn: the task may already be executed and destroyed after it
                old_left_child->m_num_elements = m_num_elements / 2;
                m_num_elements -= old_left_child->m_num_elements;

                spawn_task(old_left_child, vertex);
            } else {
                // We are the perfect tree
                // Split itself into a balanced tree and a perfect tree
                tree_task* old_right_child = m_right_child;
                m_right_child = nullptr;
                m_num_elements -= old_right_child->m_num_elements;

                spawn_task(old_right_child, vertex);
            }
        }

        if (m_num_elements > 1) {
#if !__TBB_AGG_PARENT_REFCOUNT
            vertex->reserve(static_cast<std::uint32_t>(m_num_elements - 1));
#endif

            tree_task* left = m_left_child;
            tree_task* right = m_right_child;
            m_left_child = nullptr;
            m_right_child = nullptr;
            m_num_elements = 1;

            recursive_spawn(left, vertex);
            recursive_spawn(right, vertex);
        }
    }
public:
    tree_task(d1::wait_context_vertex* tg_wait_context_vertex,
              task_group_context& ctx, d1::small_object_allocator& allocator)
        :
#if __TBB_AGG_PARENT_REFCOUNT
          m_link(0)
        , m_ref_count(1) // self-reference, dropped when the body completes
#else
          m_grab_target(nullptr)
        , m_tg_wait_context_vertex(tg_wait_context_vertex)
#endif
        , m_next_root(nullptr)
        , m_left_child(nullptr)
        , m_right_child(nullptr)
        , m_num_elements(1) // the tree initially contains only this task
        , m_wait_tree_vertex(nullptr)
        , m_ctx(ctx)
        , m_allocator(allocator)
    {
        suppress_unused_warning(tg_wait_context_vertex);
    }

    ~tree_task() override {
#if __TBB_AGG_PARENT_REFCOUNT
        // Only the grabber and the directly spawned tasks hold a vertex reference
        if (m_wait_tree_vertex != nullptr) m_wait_tree_vertex->release();
#else
        __TBB_ASSERT(m_wait_tree_vertex != nullptr, nullptr);
        m_wait_tree_vertex->release();
#endif
    }

#if __TBB_AGG_PARENT_REFCOUNT
    void reserve(std::size_t delta = 1) {
        __TBB_ASSERT(m_ref_count.load(std::memory_order_relaxed) != 0, "Cannot reserve without other references");
        m_ref_count.fetch_add(delta);
    }

    void set_parent(tree_task* parent) {
        __TBB_ASSERT(m_link == 0, "The task is already a grabber or a child");
        m_link = reinterpret_cast<std::uintptr_t>(parent) | parent_tag;
    }

    void release(d1::execution_data* ed = nullptr) {
        if (m_ref_count.fetch_sub(1) == 1) {
            tree_task* parent = (m_link & parent_tag) != 0
                              ? reinterpret_cast<tree_task*>(m_link & ~parent_tag) : nullptr;
            destroy(ed);
            if (parent != nullptr) parent->release();
        }
    }

    virtual void destroy(d1::execution_data* ed) = 0;

    void set_grab_target(task_forest* target) {
        __TBB_ASSERT(m_link == 0, "The task is already a grabber or a child");
        m_link = reinterpret_cast<std::uintptr_t>(target);
    }
    task_forest* grab_target() {
        return (m_link & parent_tag) != 0 ? nullptr : reinterpret_cast<task_forest*>(m_link);
    }
    void clear_grab_target() {
        __TBB_ASSERT((m_link & parent_tag) == 0, "The task is a child, not a grabber");
        m_link = 0;
    }
#else
    void set_grab_target(task_forest* target) { m_grab_target = target; }
    task_forest* grab_target() { return m_grab_target; }
    void clear_grab_target() { m_grab_target = nullptr; }
#endif

    tree_task*& next_root()   { return m_next_root; }
    tree_task*& left_child()  { return m_left_child; }
    tree_task*& right_child() { return m_right_child; }
    
    std::size_t& num_elements() { return m_num_elements; }
    
    task_group_context& ctx() { return m_ctx; }

    d1::wait_tree_vertex_interface*& wait_tree_vertex() { return m_wait_tree_vertex; }

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
#if __TBB_AGG_PARENT_REFCOUNT
        this->release(&ed);
#else
        m_allocator.delete_object(this, ed);
#endif
        return nullptr;
    }
    
    d1::task* cancel(d1::execution_data& ed) override {
        this->distribute_tasks();

#if __TBB_AGG_PARENT_REFCOUNT
        this->release(&ed);
#else
        m_allocator.delete_object(this, ed);
#endif
        return nullptr;
    }

#if __TBB_AGG_PARENT_REFCOUNT
private:
    void destroy(d1::execution_data* ed) override {
        if (ed != nullptr) {
            m_allocator.delete_object(this, *ed);
        } else {
            m_allocator.delete_object(this);
        }
    }
#endif
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
            new_task->set_grab_target(this);
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

#if __TBB_AGG_PARENT_REFCOUNT
                // The parent graph is fixed here and stays valid however the tree is later split
                current_head->set_parent(new_task);
                next_tree->set_parent(new_task);
                new_task->reserve(2);
#endif
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
    if (task_forest* target = grab_target()) {
        tree_task* head = target->grab_all();
        clear_grab_target();
        spawn_spine(head, this, vertex);
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
