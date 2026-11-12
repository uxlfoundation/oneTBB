#ifndef __TBB_aggregating_task_group_ultra_new_H
#define __TBB_aggregating_task_group_ultra_new_H

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
#define TASK_FOREST_GRAINSIZE 7 // Log2(TASK_TREE_GRAINSIZE)

class task_forest;

class tree_task : public d1::task {
public:
    tree_task* next_root;
    tree_task* left_child;
    tree_task* right_child;

    std::size_t num_elements;

    d1::wait_tree_vertex_interface* wait_tree_vertex;
private:
    task_group_context& m_ctx;
    d1::wait_context_vertex*        m_tg_wait_context_vertex;
protected:
    d1::small_object_allocator m_allocator;
public:
    tree_task(d1::wait_context_vertex* tg_wait_context_vertex, task_group_context& ctx,
              d1::small_object_allocator& allocator)
        : next_root(nullptr)
        , left_child(nullptr)
        , right_child(nullptr)
        , num_elements(1)
        , wait_tree_vertex(nullptr)
        , m_ctx(ctx)
        , m_tg_wait_context_vertex(tg_wait_context_vertex)
        , m_allocator(allocator)
    {}

    ~tree_task() override {
        __TBB_ASSERT(wait_tree_vertex != nullptr, "wait_tree_vertex not set");
        wait_tree_vertex->release(1);
    }

    task_group_context& ctx() { return m_ctx; }

    static tree_task* split(tree_task* source) {
        __TBB_ASSERT(source->left_child, "Tree is not splittable");
        tree_task* second_tree = source->left_child;
        source->left_child = second_tree->right_child;
        second_tree->right_child = nullptr;

        source->num_elements /= 2;
        second_tree->num_elements = source->num_elements;
        return second_tree;
    }

    static void recursive_spawn(tree_task* head, d1::wait_tree_vertex_interface* this_thread_vertex) {
        if (head != nullptr) {
            tree_task* left = head->left_child;
            tree_task* right = head->right_child;
            head->left_child = nullptr;
            head->right_child = nullptr;
            head->num_elements = 1;
            head->wait_tree_vertex = this_thread_vertex;

            r1::spawn(*head, head->ctx());
            recursive_spawn(left, this_thread_vertex);
            recursive_spawn(right, this_thread_vertex);
        }
    }

    void distribute_subtasks() {
        if (left_child != nullptr) {
            d1::wait_tree_vertex_interface* this_thread_vertex = r1::get_thread_reference_vertex(m_tg_wait_context_vertex);

            while (num_elements > TASK_TREE_GRAINSIZE) {
                __TBB_ASSERT(left_child != nullptr, "Broken structure");

                tree_task* right_task = split(this);
                right_task->wait_tree_vertex = this_thread_vertex;
                this_thread_vertex->reserve(1);
                r1::spawn(*right_task, right_task->ctx());
            }

            if (num_elements > 1) {
                this_thread_vertex->reserve(num_elements - 1);

                tree_task* left = left_child;
                tree_task* right = right_child;
                left_child = nullptr;
                right_child = nullptr;

                recursive_spawn(left, this_thread_vertex);
                recursive_spawn(right, this_thread_vertex);
            }
        }
    }
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
        this->distribute_subtasks();
        m_function();
        m_allocator.delete_object(this, ed);
        return nullptr;
    }

    d1::task* cancel(d1::execution_data& ed) override {
        this->distribute_subtasks();
        m_allocator.delete_object(this, ed);
        return nullptr;
    }
};

class grab_task : public d1::task {
    task_forest*                    m_target;
    d1::wait_tree_vertex_interface* m_wait_tree_vertex;
    d1::wait_context_vertex*        m_tg_wait_context_vertex;
    task_group_context&             m_ctx;
    d1::small_object_allocator      m_allocator;
public:
    grab_task(task_forest* target, d1::wait_tree_vertex_interface* wait_tree_vertex,
              d1::wait_context_vertex* tg_wait_context_vertex,
              task_group_context& ctx, d1::small_object_allocator& allocator)
        : m_target(target)
        , m_wait_tree_vertex(wait_tree_vertex)
        , m_tg_wait_context_vertex(tg_wait_context_vertex)
        , m_ctx(ctx)
        , m_allocator(allocator)
    {
        m_wait_tree_vertex->reserve(1);
    }

    ~grab_task() {
        m_wait_tree_vertex->release();
    }

    d1::task* execute(d1::execution_data& ed) override;

    d1::task* cancel(d1::execution_data& ed) override {
        return execute(ed);
    }

    static void distribute_spine_recursion(std::size_t total_num_references, tree_task* current,
                                           d1::wait_tree_vertex_interface* this_thread_vertex) {
        current->wait_tree_vertex = this_thread_vertex;
        if (current->next_root != nullptr) {
            distribute_spine_recursion(total_num_references + 1, current->next_root, this_thread_vertex);
            current->next_root = nullptr;
        } else {
            this_thread_vertex->reserve(static_cast<std::uint32_t>(total_num_references));
        }
        r1::spawn(*current, current->ctx());
    }

    d1::task* distribute_spine(tree_task* spine) {
        __TBB_ASSERT(spine != nullptr, nullptr);
        d1::wait_tree_vertex_interface* this_thread_vertex = r1::get_thread_reference_vertex(m_tg_wait_context_vertex);

        tree_task* rest = spine->next_root;
        spine->next_root = nullptr;
        spine->wait_tree_vertex = this_thread_vertex;

        if (rest != nullptr) {
            distribute_spine_recursion(2, rest, this_thread_vertex);
        } else {
            this_thread_vertex->reserve(1);
        }

        return spine;
    }
};

class task_forest {
    std::atomic<tree_task*> m_head;
    std::size_t m_num_elements;

    static tree_task* locked_flag() {
        static constexpr std::uintptr_t locked_flag_value = ~std::uintptr_t(1);
        return reinterpret_cast<tree_task*>(locked_flag_value);
    }

    tree_task* lock() {
        atomic_backoff backoff;
        tree_task* current_head = locked_flag();

        while (m_head.load(std::memory_order_relaxed) == locked_flag() ||
               (current_head = m_head.exchange(locked_flag())) == locked_flag())
        {
            backoff.pause();
        }
        __TBB_ASSERT(current_head != locked_flag(), "Forest was not locked");
        return current_head;
    }

    void unlock(tree_task* head) {
        __TBB_ASSERT(head != locked_flag(), nullptr);
        __TBB_ASSERT(m_head.load(std::memory_order_relaxed) == locked_flag(), "Unlocking the forest that is not locked");
        m_head.store(head, std::memory_order_release);
    }
public:
    task_forest() : m_head(nullptr), m_num_elements(0) {}

    template <typename Function>
    void push(Function&& function, d1::wait_context_vertex& wait_vertex, task_group_context& ctx) {
        d1::small_object_allocator alloc;

        using task_type = function_tree_task<typename std::decay<Function>::type>;
        task_type* new_task = alloc.new_object<task_type>(std::forward<Function>(function), &wait_vertex, ctx, alloc);

        grab_task* grabber = nullptr;

        // Lock the forest to prevent races with grabbers
        tree_task* next_head = lock();
        ++m_num_elements;
        tree_task* current_head = new_task;
        new_task->next_root = next_head;

        if (next_head == nullptr) {
            grabber = alloc.new_object<grab_task>(this, r1::get_thread_reference_vertex(&wait_vertex),
                                                  &wait_vertex, ctx, alloc);
        } else {
            tree_task* prev_head = current_head;

            while (next_head && prev_head->num_elements == next_head->num_elements) {
                prev_head->next_root = nullptr;
                prev_head->right_child = next_head->left_child;
                next_head->left_child = prev_head;
                
                next_head->num_elements += prev_head->num_elements;

                prev_head = next_head;
                next_head = next_head->next_root;
            }
            current_head = prev_head;
        }

        unlock(current_head);
        if (grabber) r1::spawn(*grabber, ctx);
    }

    tree_task* grab(d1::wait_context_vertex* tg_wait_context_vertex, task_group_context& ctx) {
        tree_task* head = lock();
        tree_task* new_head = nullptr;
        tree_task* producer_head = head;

        if (head != nullptr) {
            if (m_num_elements <= TASK_FOREST_GRAINSIZE) {
                // Another grab hop would cost more than the redistribution it buys
                new_head = head;
                producer_head = nullptr;
                m_num_elements = 0;
            } else {
                tree_task* new_prev = nullptr;

                // Sizes strictly increase along the spine, so only the head can be indivisible
                if (head->num_elements == 1) {
                    producer_head = head->next_root;
                    new_head = new_prev = head;
                    --m_num_elements;
                }

                for (tree_task* curr = producer_head; curr != nullptr; curr = curr->next_root) {
                    tree_task* half = tree_task::split(curr);
                    m_num_elements -= half->num_elements;

                    if (new_prev) {
                        new_prev = new_prev->next_root = half;
                    } else {
                        new_head = new_prev = half;
                    }
                }

                __TBB_ASSERT(new_prev != nullptr, nullptr);
                new_prev->next_root = nullptr;
            }
        }

        // TODO: if there are more tasks to grab - spawn another grabber

        unlock(producer_head);

        // Whatever is left behind must stay reachable, so this grabber owns the hand-off
        if (producer_head != nullptr) {
            d1::small_object_allocator alloc;
            grab_task* successor = alloc.new_object<grab_task>(
                this, r1::get_thread_reference_vertex(tg_wait_context_vertex),
                tg_wait_context_vertex, ctx, alloc);
            r1::spawn(*successor, ctx);
        }

        return new_head;
    }

    // Used by the producer on wait(): nothing is left behind, so no successor is needed
    tree_task* grab_all() {
        tree_task* head = lock();
        m_num_elements = 0;
        unlock(nullptr);
        return head;
    }
};

inline d1::task* grab_task::execute(d1::execution_data& ed) {
    __TBB_ASSERT(ed.context == &m_ctx, nullptr);
    d1::task* next_task = nullptr;
    tree_task* grabbed_tree = m_target->grab(m_tg_wait_context_vertex, m_ctx);

    if (grabbed_tree != nullptr) {
        next_task = distribute_spine(grabbed_tree);
    }

    m_allocator.delete_object(this, ed);
    return next_task;
}

class aggregating_task_group {
private:
    d1::wait_context_vertex m_wait_vertex;
    task_group_context      m_context;

    static thread_local task_forest m_task_forest;

    task_group_context& context() noexcept {
        return m_context.actual_context();
    }
public:
    aggregating_task_group()
        : m_wait_vertex(0)
        , m_context(task_group_context::bound, task_group_context::default_traits | task_group_context::concurrent_wait)
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
        // Drain the local forest instead of leaving the producer to steal its own tasks back.
        // An already spawned grabber may find nothing left, which is the one wasted grabber per producer
        if (tree_task* grabbed = m_task_forest.grab_all()) {
            grab_task::distribute_spine_recursion(1, grabbed, r1::get_thread_reference_vertex(&m_wait_vertex));
        }

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
};

inline thread_local task_forest aggregating_task_group::m_task_forest;

} // namespace d2
} // namespace detail

inline namespace v1 {
using detail::d2::aggregating_task_group;
} // inline namespace v1

} // namespace tbb

#endif // __TBB_aggregating_task_group_ultra_new_H
