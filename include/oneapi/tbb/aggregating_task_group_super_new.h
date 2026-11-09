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
#define DIRECT_SPAWN_THRESHOLD 8

// Set to 1 to count references per task through a parent chain, as in the original prototype, so
// that a whole batch holds a single reference on the shared wait tree vertex instead of one per task
#ifndef __TBB_AGG_PARENT_REFCOUNT
#define __TBB_AGG_PARENT_REFCOUNT 0
#endif

class binary_task_tree;

class tree_task : public d1::task {
#if __TBB_AGG_PARENT_REFCOUNT
    // A task is either the head of a tree or a member of one, never both, so a single word holds
    // either the tree to grab or the parent. Both pointers are aligned, so the tag bit is free
    static constexpr std::uintptr_t parent_tag = 1;
    std::uintptr_t                  m_link;
    std::atomic<std::size_t>        m_ref_count;
#else
    binary_task_tree*               m_grab_target;
    d1::wait_context_vertex*        m_tg_wait_context_vertex;
#endif
    tree_task*                      m_left_child;
    tree_task*                      m_right_child;
    std::size_t                     m_num_subtree_elements;
    d1::wait_tree_vertex_interface* m_wait_tree_vertex;
    task_group_context&             m_ctx;
protected:
    d1::small_object_allocator      m_allocator;
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
        , m_left_child(nullptr)
        , m_right_child(nullptr)
        , m_num_subtree_elements(0)
        , m_wait_tree_vertex(nullptr)
        , m_ctx(ctx)
        , m_allocator(allocator)
    {
        suppress_unused_warning(tg_wait_context_vertex);
    }

    ~tree_task() override {
#if __TBB_AGG_PARENT_REFCOUNT
        // Only the head and the directly spawned tasks hold a vertex reference
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
        __TBB_ASSERT(m_link == 0, "The task is already a head or a child");
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

    void set_grab_target(binary_task_tree* target) {
        __TBB_ASSERT(m_link == 0, "The task is already a head or a child");
        m_link = reinterpret_cast<std::uintptr_t>(target);
    }
    binary_task_tree* grab_target() {
        return (m_link & parent_tag) != 0 ? nullptr : reinterpret_cast<binary_task_tree*>(m_link);
    }
    void clear_grab_target() {
        __TBB_ASSERT((m_link & parent_tag) == 0, "The task is a child, not a head");
        m_link = 0;
    }
#else
    void set_grab_target(binary_task_tree* target) { m_grab_target = target; }
    binary_task_tree* grab_target() { return m_grab_target; }
    void clear_grab_target() { m_grab_target = nullptr; }
#endif

    tree_task*& left_child() { return m_left_child; }
    tree_task*& right_child() { return m_right_child; }
    std::size_t& num_subtree_elements() { return m_num_subtree_elements; }
    task_group_context& ctx() { return m_ctx; }
    d1::wait_tree_vertex_interface*& wait_tree_vertex() { return m_wait_tree_vertex; }

    void grab_and_redistribute_children();

    static void recursive_spawn(tree_task* tree_head, d1::wait_tree_vertex_interface* vertex) {
        if (tree_head != nullptr) {
            tree_task* left = tree_head->left_child();
            tree_task* right = tree_head->right_child();
            tree_head->left_child() = nullptr;
            tree_head->right_child() = nullptr;
            // The children are detached, so the task must not try to split itself when executed
            tree_head->num_subtree_elements() = 0;

#if !__TBB_AGG_PARENT_REFCOUNT
            // The reference on the vertex is already reserved in bulk by the caller
            tree_head->wait_tree_vertex() = vertex;
#endif

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
#if __TBB_AGG_PARENT_REFCOUNT
        this->release(&ed);
#else
        m_allocator.delete_object(this, ed);
#endif
        return nullptr;
    }

    d1::task* cancel(d1::execution_data& ed) override {
        // The buffered tasks are still published so that the scheduler cancels them in turn
        this->grab_and_redistribute_children();
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

class binary_task_tree {
    static tree_task* locked_flag() {
        static constexpr std::uintptr_t locked_flag_value = ~std::uintptr_t(1);
        return reinterpret_cast<tree_task*>(locked_flag_value);
    }

    std::atomic<tree_task*> m_head;
    // The tree is thread-local, so only the owner thread touches the counter and no lock is needed
    std::size_t m_num_direct_spawns;

    // TODO: O(log n) on the producer hot path. Consider an O(1) intrusive list push here and
    // building the balanced tree on the consumer side, in grab_all
    static void add_task_to_subtree(tree_task* subtree_head, tree_task* new_task) {
        __TBB_ASSERT(subtree_head != nullptr, nullptr);
        if (subtree_head->left_child() == nullptr) {
            subtree_head->left_child() = new_task;
#if __TBB_AGG_PARENT_REFCOUNT
            subtree_head->reserve(1);
            new_task->set_parent(subtree_head);
#endif
        } else if (subtree_head->right_child() == nullptr) {
            subtree_head->right_child() = new_task;
#if __TBB_AGG_PARENT_REFCOUNT
            subtree_head->reserve(1);
            new_task->set_parent(subtree_head);
#endif
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

        // While no batch is in flight and the backlog is still small, publish the task exactly as
        // task_group::run would: no head, no tree, no lock
        if (m_num_direct_spawns < DIRECT_SPAWN_THRESHOLD &&
            m_head.load(std::memory_order_relaxed) == nullptr)
        {
            ++m_num_direct_spawns;
            new_task->wait_tree_vertex() = r1::get_thread_reference_vertex(&wait_vertex);
            new_task->wait_tree_vertex()->reserve();
            r1::spawn(*new_task, ctx);
            return;
        }

        // Lock the task tree to prevent races with grab_all.
        // The tree is thread-local, so only the owner thread takes the lock and it is never contended here
        tree_task* current_task_tree = lock();
        __TBB_ASSERT(current_task_tree != locked_flag(), "The task tree is already locked by the owner thread");

        bool spawn_new_task = current_task_tree == nullptr;

        if (spawn_new_task) {
            current_task_tree = new_task;
            new_task->set_grab_target(this);
            m_num_direct_spawns = 0;
        } else {
            // Aggregating
            // Special handling for the first layer of the tree
            __TBB_ASSERT(current_task_tree->right_child() == nullptr, "Broken structure of the tree");

            if (current_task_tree->left_child() == nullptr) {
                current_task_tree->left_child() = new_task;
#if __TBB_AGG_PARENT_REFCOUNT
                current_task_tree->reserve(1);
                new_task->set_parent(current_task_tree);
#endif
            } else {
                add_task_to_subtree(current_task_tree->left_child(), new_task);
            }
            ++current_task_tree->num_subtree_elements();
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
    if (binary_task_tree* target = grab_target()) {
        // Current task is the head of the initial task tree
        // Grab all tasks in the tree
        tree_task* head = target->grab_all();
        __TBB_ASSERT(head == this, "Incorrect task tree structure");
        suppress_unused_warning(head);
        clear_grab_target();
    }

    if (num_subtree_elements() == 0) return;

#if __TBB_AGG_PARENT_REFCOUNT
    // Every task is already parented and accounted for, so no vertex is needed here
    d1::wait_tree_vertex_interface* vertex = nullptr;
#else
    // Looked up once for the whole batch, the tasks share it and each releases one reference when destroyed
    d1::wait_tree_vertex_interface* vertex = r1::get_thread_reference_vertex(m_tg_wait_context_vertex);
#endif

    // Redistribution stage
    while (num_subtree_elements() + 1 > TASK_TREE_GRAINSIZE) {
        tree_task* splitted_tree_head = binary_task_tree::split(this);
#if !__TBB_AGG_PARENT_REFCOUNT
        splitted_tree_head->wait_tree_vertex() = vertex;
        // The elements carried away by the split are reserved by the split head itself
        vertex->reserve();
#endif
        r1::spawn(*splitted_tree_head, m_ctx);
    }

    std::size_t num_children = num_subtree_elements();

    if (num_children != 0) {
#if !__TBB_AGG_PARENT_REFCOUNT
        vertex->reserve(static_cast<std::uint32_t>(num_children));
#endif

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
