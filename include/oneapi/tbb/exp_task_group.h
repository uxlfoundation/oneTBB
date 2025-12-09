#ifndef __TBB_exp_task_group_H
#define __TBB_exp_task_group_H

#include "detail/_config.h"
#include "detail/_namespace_injection.h"
#include "detail/_assert.h"
#include "detail/_utils.h"
#include "detail/_task.h"
#include "detail/_small_object_pool.h"
#include "task_group.h"
#include <atomic>

#ifndef TRY_REUSE_CURRENT_TASK
#define TRY_REUSE_CURRENT_TASK 0
#endif

#ifndef TRY_SPIN_UNTIL_MIN_SIZE
#define TRY_SPIN_UNTIL_MIN_SIZE 0
#endif

#ifndef TRY_AVOID_SPLIT_TASK
#define TRY_AVOID_SPLIT_TASK 0
#endif

#define TASK_TREE_GRAINSIZE           4
#define RECOMMENDED_MINIMAL_TREE_SIZE 2048
#define NUM_SIZE_RETRIES              10

namespace tbb {
namespace detail {
namespace d1 {

// TODO: try using wait_tree_vertex instead
class task_with_ref_counter : public task {
    std::atomic<std::size_t> m_ref_count;
    task_with_ref_counter*   m_parent;
    task_group_context&      m_ctx;
    small_object_allocator   m_allocator;

public:
    task_with_ref_counter(task_group_context& ctx, small_object_allocator& allocator)
        : m_ref_count(1) // self-reference
        , m_parent(nullptr)
        , m_ctx(ctx)
        , m_allocator(allocator)
    {}

    ~task_with_ref_counter() {}

    void reserve(std::size_t diff = 1) {
        __TBB_ASSERT(m_ref_count.load(std::memory_order_relaxed) != 0, "Cannot reserve if there are no other references");
        m_ref_count.fetch_add(diff);
    }

    // TODO: should ed be propagated?
    void release(std::size_t diff = 1, execution_data* ed = nullptr) {
        __TBB_ASSERT(m_ref_count.load(std::memory_order_relaxed) >= diff, "Overflow detected");
        if (m_ref_count.fetch_sub(diff) == diff) {
            task_with_ref_counter* parent = m_parent;

            if (ed) {
                m_allocator.delete_object(this, *ed);
            } else {
                m_allocator.delete_object(this);
            }

            if (parent) {
                // TODO: ed
                parent->release(1);
            }
        }
    }

    task_group_context& ctx() { return m_ctx; }

    void set_parent(task_with_ref_counter* parent) {
        __TBB_ASSERT(m_parent == nullptr, "Parent already set");
        m_parent = parent;
    }
}; // class task_with_ref_counter

#if TRY_AVOID_SPLIT_TASK
class tree_task : public task_with_ref_counter {
private:
    tree_task*   m_left_task;
    tree_task*   m_right_task;
    std::size_t  m_num_subtree_elements;

public:
    tree_task(task_group_context& ctx, small_object_allocator& allocator)
        : task_with_ref_counter(ctx, allocator)
        , m_left_task(nullptr)
        , m_right_task(nullptr)
        , m_num_subtree_elements(0)
    {}

    ~tree_task() override {}

    tree_task*& left() { return m_left_task; }
    tree_task*& right() { return m_right_task; }
    std::size_t& num_subtree_elements() { return m_num_subtree_elements; }

    // TODO: should it be static
    static bool try_split_and_spawn(tree_task* tree_head);
};
#else
class tree_task : public task {
private:
    tree_task*             m_left_task;
    tree_task*             m_right_task;
    std::size_t            m_num_subtree_elements;
    task_group_context&    m_ctx;
protected:
    task_with_ref_counter* m_parent;
    small_object_allocator m_allocator;

public:
    tree_task(task_group_context& ctx, small_object_allocator& allocator)
        : m_left_task(nullptr)
        , m_right_task(nullptr)
        , m_num_subtree_elements(0)
        , m_ctx(ctx)
        , m_parent(nullptr)
        , m_allocator(allocator)
    {}

    ~tree_task() override {}

    tree_task*& left() { return m_left_task; }
    tree_task*& right() { return m_right_task; }
    std::size_t& num_subtree_elements() { return m_num_subtree_elements; }
    task_group_context& ctx() { return m_ctx; }

    void set_parent(task_with_ref_counter* parent) {
        __TBB_ASSERT(m_parent == nullptr, "Parent already set");
        m_parent = parent;
    }
}; // class tree_task
#endif

template <typename Function>
class function_tree_task : public tree_task {
private:
    // TODO: EBO
    const Function m_function;

#if !TRY_AVOID_SPLIT_TASK
    void destroy(execution_data& ed) {
        task_with_ref_counter* parent = m_parent;
        m_allocator.delete_object(this, ed);
        if (parent) {
            parent->release(1); // TODO: ed?
        }
    }
#endif
public:
    template <typename F>
    function_tree_task(F&& function, task_group_context& ctx, small_object_allocator& allocator)
        : tree_task(ctx, allocator)
        , m_function(std::forward<F>(function))
    {}

    ~function_tree_task() {}

    d1::task* execute(execution_data& ed) override {
        __TBB_ASSERT(ed.context == &this->ctx(), "The task group context should be used for all tasks");
#if TRY_AVOID_SPLIT_TASK
        if (!tree_task::try_split_and_spawn(this)) {
            m_function();
            release(1, &ed); // release self-reference
            return nullptr;
        } else {
            return this; // re-execute
        }
#else
        destroy(ed);
#endif
        return nullptr;
    }

    d1::task* cancel(execution_data& ed) override {
        __TBB_ASSERT(false, "Task cancellation not supported");
#if !TRY_AVOID_SPLIT_TASK
        destroy(ed);
#endif
        return nullptr;
    }
}; // class function_tree_task

#if !TRY_AVOID_SPLIT_TASK
class split_task : public task_with_ref_counter {
    tree_task* m_task_tree;
public:
    split_task(tree_task* tree, task_group_context& ctx, small_object_allocator& allocator)
        : task_with_ref_counter(ctx, allocator)
        , m_task_tree(tree)
    {
        __TBB_ASSERT(m_task_tree != nullptr, nullptr);
    }

    ~split_task() {}

    d1::task* execute(execution_data& ed) override {
        __TBB_ASSERT(ed.context == &ctx(), "The task group context should be used for all tasks");
        task* t = split_and_bypass(m_task_tree, this, ed);
#if TRY_REUSE_CURRENT_TASK
        if (t != this) {
            release(); // release self-reference
        }
#else
        release(); // release self-reference
#endif
        return t;
    }

    d1::task* cancel(execution_data& ed) override {
        __TBB_ASSERT(false, "Task cancellation not supported yet");
        return nullptr;
    }

    static task* split_and_bypass(tree_task* tree, task_with_ref_counter* requester_task, execution_data& ed);

    static void recursive_spawn(tree_task* tree, task_with_ref_counter* requester_task) {
        if (tree != nullptr) {
            tree_task* left = tree->left();
            tree_task* right = tree->right();

            // Spawn current task
            tree->set_parent(requester_task);
            r1::spawn(*tree, tree->ctx());

            recursive_spawn(left, requester_task);
            recursive_spawn(right, requester_task);
        }
    }
};
#endif

class binary_task_tree;

class grab_task : public task_with_ref_counter {
    binary_task_tree&           m_task_tree;
    wait_tree_vertex_interface* m_wait_tree_vertex;
public:
    grab_task(binary_task_tree& task_tree, wait_tree_vertex_interface* wait_tree_vertex,
              task_group_context& ctx, small_object_allocator& allocator)
        : task_with_ref_counter(ctx, allocator)
        , m_task_tree(task_tree)
        , m_wait_tree_vertex(wait_tree_vertex)
    {
        __TBB_ASSERT(m_wait_tree_vertex != nullptr, nullptr);
        m_wait_tree_vertex->reserve(1);
    }

    ~grab_task() {
        m_wait_tree_vertex->release(1);
    }

    d1::task* execute(execution_data& ed) override;
    
    d1::task* cancel(execution_data& ed) override {
        __TBB_ASSERT(false, "Task cancellation is not supported");
        return nullptr;
    }
}; // class grab_task

class binary_task_tree {
private:
    static constexpr std::uintptr_t busy_flag = ~std::uintptr_t(1);

    std::atomic<tree_task*> m_head;

    tree_task* mark_busy() {
        return m_head.exchange(reinterpret_cast<tree_task*>(busy_flag));
    }

    tree_task* wait_while_busy() {
        return spin_wait_while_eq(m_head, reinterpret_cast<tree_task*>(busy_flag));
    }

    static void add_task_to_subtree(tree_task* subtree_head, tree_task* new_task) {
        __TBB_ASSERT(subtree_head != nullptr, nullptr);
        if (subtree_head->left() == nullptr) {
            subtree_head->left() = new_task;
#if TRY_AVOID_SPLIT_TASK
            subtree_head->reserve(1);
            new_task->set_parent(subtree_head);
#endif
        } else if (subtree_head->right() == nullptr) {
            subtree_head->right() = new_task;
#if TRY_AVOID_SPLIT_TASK
            subtree_head->reserve(1);
            new_task->set_parent(subtree_head);
#endif
        } else if (subtree_head->left()->num_subtree_elements() <= subtree_head->right()->num_subtree_elements()) {
            add_task_to_subtree(subtree_head->left(), new_task);
        } else {
            add_task_to_subtree(subtree_head->right(), new_task);
        }
        ++subtree_head->num_subtree_elements();
    }
public:
    static bool is_busy(tree_task* head) { return head == reinterpret_cast<tree_task*>(busy_flag); }

    template <typename Function>
    void add_task(Function&& function, wait_context_vertex& wait_vertex, task_group_context& ctx) {
        small_object_allocator alloc;

        using task_type = function_tree_task<typename std::decay<Function>::type>;
        task_type* new_task = alloc.new_object<task_type>(std::forward<Function>(function), ctx, alloc);
        
        // Mark current task tree as busy to prevent races with grab_all
        tree_task* current_task_tree = mark_busy();

        if (current_task_tree == nullptr) {
            current_task_tree = new_task;

            // The first item in the tree, spawn the grab task
            grab_task* gt = alloc.new_object<grab_task>(*this, r1::get_thread_reference_vertex(&wait_vertex), ctx, alloc);
            r1::spawn(*gt, ctx);
        } else {
            // Special handling for the first layer of the tree
            __TBB_ASSERT(current_task_tree->right() == nullptr, "Broken tree structure");
            
            if (current_task_tree->left() == nullptr) {
                current_task_tree->left() = new_task;
#if TRY_AVOID_SPLIT_TASK
                current_task_tree->reserve(1);
                new_task->set_parent(current_task_tree);
#endif
            } else {
                add_task_to_subtree(current_task_tree->left(), new_task);
            }
            ++current_task_tree->num_subtree_elements();
        }

        // Release ownership of the tree
        tree_task* prev_state = m_head.exchange(current_task_tree);
        __TBB_ASSERT(is_busy(prev_state), "Incorrect tree state after releasing");
        suppress_unused_warning(prev_state);
    }

    tree_task* grab_all() {
        tree_task* current_head = wait_while_busy();

#if TRY_SPIN_UNTIL_MIN_SIZE
        std::size_t num_retries = 0;
        // Controlled data race on integer
        while (current_head->num_subtree_elements() + 1 < RECOMMENDED_MINIMAL_TREE_SIZE &&
               num_retries++ < NUM_SIZE_RETRIES)
        {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            if (num_retries > NUM_SIZE_RETRIES / 2) d0::yield();
        }
#endif

        while (!m_head.compare_exchange_strong(current_head, nullptr)) {
            __TBB_ASSERT(is_busy(current_head), nullptr);
            current_head = wait_while_busy();
        }
        __TBB_ASSERT(!is_busy(current_head), "Incorrect tree grabbed");
        return current_head;
    }

    static tree_task* split(tree_task* head) {
        __TBB_ASSERT(head != nullptr, "Tree is not divisible");
        __TBB_ASSERT(head->num_subtree_elements() + 1 > TASK_TREE_GRAINSIZE, "Tree is not divisible");
        __TBB_ASSERT(head->left() != nullptr && head->left()->left() && head->left()->right(), "Broken tree structure");
        __TBB_ASSERT(head->right() == nullptr, "Broken tree structure");
#ifdef TBB_USE_DEBUG
        std::size_t num_elements = head->num_subtree_elements();
#endif

        tree_task* second_tree = head->left();

        head->left() = second_tree->right();

        head->num_subtree_elements() = head->left()->num_subtree_elements() + 1;
        second_tree->num_subtree_elements() = second_tree->left()->num_subtree_elements() + 1;
        second_tree->right() = nullptr;

        // TODO: eliminate +- 1 in various places
        __TBB_ASSERT(head->num_subtree_elements() + second_tree->num_subtree_elements() == num_elements - 1, "Incorrect split");
        return second_tree;
    }
}; // class binary_task_tree

#if TRY_AVOID_SPLIT_TASK
inline bool tree_task::try_split_and_spawn(tree_task* tree_head) {
    __TBB_ASSERT(tree_head != nullptr, nullptr);
    bool is_divisible = tree_head->num_subtree_elements() + 1 > TASK_TREE_GRAINSIZE;

    if (!is_divisible) {
        if (tree_head->left()) {
            r1::spawn(*tree_head->left(), tree_head->ctx());
        }
        if (tree_head->right()) {
            r1::spawn(*tree_head->right(), tree_head->ctx());
        }
    } else {
        tree_task* splitted_tree_head = binary_task_tree::split(tree_head);
        r1::spawn(*splitted_tree_head, tree_head->ctx());
    }

    return is_divisible;
}
#endif

inline task* grab_task::execute(execution_data& ed) {
    __TBB_ASSERT(ed.context == &ctx(), "The task group context should be used for all tasks");
    tree_task* task_tree = m_task_tree.grab_all();
    __TBB_ASSERT(task_tree != nullptr, "Grab task should task at least one task");

#if TRY_AVOID_SPLIT_TASK
    task_tree->set_parent(this);
    return task_tree;
    // tree_task::try_split_and_spawn(task_tree);
    // return task_tree;
#else
    task* t = split_task::split_and_bypass(task_tree, this, ed);
#if TRY_REUSE_CURRENT_TASK
    if (t == this) {
        // grab_task cannot be re-executed, substitute split_task instead
        small_object_allocator alloc;
        split_task* split = alloc.new_object<split_task>(task_tree, ctx(), alloc);
        split->set_parent(this);
        t = split;
    } else {
        release(); // release self-reference
    }
#else
    release(); // release self-reference
#endif
    return t;
#endif // TRY_AVOID_SPLIT_TASK
}

#if !TRY_AVOID_SPLIT_TASK
task* split_task::split_and_bypass(tree_task* tree, task_with_ref_counter* requester_task, execution_data& ed) {
    __TBB_ASSERT(tree != nullptr, nullptr);
    task* bypass_task = nullptr;

    // Check if the tree is divisible
    if (tree->num_subtree_elements() + 1 <= TASK_TREE_GRAINSIZE) {
        __TBB_ASSERT(tree->right() == nullptr, "Broken tree structure");

        requester_task->reserve(tree->num_subtree_elements() + 1);
        bypass_task = tree;
        tree->set_parent(requester_task);
        recursive_spawn(tree->left(), requester_task);
    } else {
        d1::small_object_allocator alloc;

        tree_task* splitted_tree = binary_task_tree::split(tree);

#if TRY_REUSE_CURRENT_TASK
        requester_task->reserve(1);

        split_task* right = alloc.new_object<split_task>(splitted_tree, requester_task->ctx(), alloc);
        right->set_parent(requester_task);
        bypass_task = requester_task;
        r1::spawn(*right, requester_task->ctx());
#else
        // TODO: measure performance if the current task is bypassed instead, but separate ref counter is not created
        requester_task->reserve(2);
        
        split_task* left = alloc.new_object<split_task>(tree, requester_task->ctx(), alloc);
        split_task* right = alloc.new_object<split_task>(splitted_tree, requester_task->ctx(), alloc);

        left->set_parent(requester_task);
        right->set_parent(requester_task);
        bypass_task = left;
        r1::spawn(*right, requester_task->ctx());
#endif
    }

    return bypass_task;
}
#endif

class exp_task_group {
private:
    wait_context_vertex m_wait_vertex;
    task_group_context  m_context;
    static thread_local binary_task_tree m_task_tree;

    task_group_context& context() noexcept {
        return m_context.actual_context();
    }
public:
    exp_task_group()
        : m_wait_vertex(0)
        , m_context(task_group_context::bound, task_group_context::default_traits | task_group_context::concurrent_wait)
    {}

    exp_task_group(task_group_context& ctx)
        : m_wait_vertex(0)
        , m_context(&ctx)
    {}

    ~exp_task_group() noexcept(false) {
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
}; // class exp_task_group

thread_local binary_task_tree exp_task_group::m_task_tree;

} // namespace d1
} // namespace detail

inline namespace v1 {
using detail::d1::exp_task_group;
} // inline namespace v1

} // namespace tbb

#endif // __TBB_exp_task_group_H
