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

#ifndef TRY_SPIN_UNTIL_MIN_SIZE
#define TRY_SPIN_UNTIL_MIN_SIZE 0
#endif

#define TASK_TREE_GRAINSIZE           128
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

    void split_and_spawn();

    static void recursive_spawn(tree_task* tree_head) {
        if (tree_head != nullptr) {
            tree_task* left = tree_head->left();
            tree_task* right = tree_head->right();
            tree_head->left() = nullptr;
            tree_head->right() = nullptr;
            r1::spawn(*tree_head, tree_head->ctx());
            recursive_spawn(left);
            recursive_spawn(right);
        }
    }
};

template <typename Function>
class function_tree_task : public tree_task {
private:
    // TODO: EBO
    const Function m_function;
public:
    template <typename F>
    function_tree_task(F&& function, task_group_context& ctx, small_object_allocator& allocator)
        : tree_task(ctx, allocator)
        , m_function(std::forward<F>(function))
    {}

    ~function_tree_task() {}

    d1::task* execute(execution_data& ed) override {
        __TBB_ASSERT(ed.context == &this->ctx(), "The task group context should be used for all tasks");
        split_and_spawn();
        m_function();
        release(1, &ed); // release self-reference
        return nullptr;
    }

    d1::task* cancel(execution_data& ed) override {
        __TBB_ASSERT(false, "Task cancellation not supported");
        return nullptr;
    }
}; // class function_tree_task

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
    static constexpr std::uintptr_t locked_flag = ~std::uintptr_t(1);

    std::atomic<tree_task*> m_head;

    tree_task* lock() {
        return m_head.exchange(reinterpret_cast<tree_task*>(locked_flag));
    }

    void unlock(tree_task* head) {
        tree_task* prev = m_head.exchange(head);
        __TBB_ASSERT(is_locked(prev), nullptr);
        suppress_unused_warning(prev);
    }

    tree_task* wait_while_locked() {
        return spin_wait_while_eq(m_head, reinterpret_cast<tree_task*>(locked_flag));
    }

    static void add_task_to_subtree(tree_task* subtree_head, tree_task* new_task) {
        __TBB_ASSERT(subtree_head != nullptr, nullptr);
        if (subtree_head->left() == nullptr) {
            subtree_head->left() = new_task;
            subtree_head->reserve(1);
            new_task->set_parent(subtree_head);
        } else if (subtree_head->right() == nullptr) {
            subtree_head->right() = new_task;
            subtree_head->reserve(1);
            new_task->set_parent(subtree_head);
        } else if (subtree_head->left()->num_subtree_elements() <= subtree_head->right()->num_subtree_elements()) {
            add_task_to_subtree(subtree_head->left(), new_task);
        } else {
            add_task_to_subtree(subtree_head->right(), new_task);
        }
        ++subtree_head->num_subtree_elements();
    }
public:
    static bool is_locked(tree_task* head) { return head == reinterpret_cast<tree_task*>(locked_flag); }

    template <typename Function>
    void add_task(Function&& function, wait_context_vertex& wait_vertex, task_group_context& ctx) {
        small_object_allocator alloc;

        using task_type = function_tree_task<typename std::decay<Function>::type>;
        task_type* new_task = alloc.new_object<task_type>(std::forward<Function>(function), ctx, alloc);
        
        // Lock the task tree to prevent races with grab_all
        tree_task* current_task_tree = lock();

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

                current_task_tree->reserve(1);
                new_task->set_parent(current_task_tree);
            } else {
                add_task_to_subtree(current_task_tree->left(), new_task);
            }
            ++current_task_tree->num_subtree_elements();
        }

        // Release ownership of the tree
        unlock(current_task_tree);
    }

    tree_task* grab_all() {
        tree_task* current_head = wait_while_locked();

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
            __TBB_ASSERT(is_locked(current_head), nullptr);
            current_head = wait_while_locked();
        }
        __TBB_ASSERT(!is_locked(current_head), "Incorrect tree grabbed");
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

inline void tree_task::split_and_spawn() {
    while (num_subtree_elements() + 1 > TASK_TREE_GRAINSIZE) {
        tree_task* splitted_tree_head = binary_task_tree::split(this);
        r1::spawn(*splitted_tree_head, ctx());
    }

    recursive_spawn(left());
    recursive_spawn(right());
}

inline task* grab_task::execute(execution_data& ed) {
    __TBB_ASSERT(ed.context == &ctx(), "The task group context should be used for all tasks");
    tree_task* task_tree = m_task_tree.grab_all();
    __TBB_ASSERT(task_tree != nullptr, "Grab task should task at least one task");

    task_tree->set_parent(this);
    return task_tree;
}

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
