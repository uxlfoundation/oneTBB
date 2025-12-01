#ifndef __TBB_aggregating_task_group_H
#define __TBB_aggregating_task_group_H

#include "detail/_config.h"
#include "detail/_namespace_injection.h"
#include "detail/_assert.h"
#include "detail/_utils.h"
#include "detail/_task.h"
#include "detail/_small_object_pool.h"
#include "detail/_task_handle.h"
#include "task_group.h"
#include "task_arena.h"
#include "concurrent_vector.h"
#include <atomic>

#include <iostream>

namespace tbb {
namespace detail {
namespace d2 {

template <typename T>
void destroy_task(T* task, const d1::execution_data* ed, d1::small_object_allocator& alloc) {
    // TODO: fix me
    if (ed) {
        task->~T();
        // alloc.delete_object(task, *ed);
    } else {
        alloc.delete_object(task);
    }
}

class task_with_ref_counter : public d1::task {
    std::atomic<std::size_t>   m_ref_count;
    task_with_ref_counter*     m_parent;
    d1::task_group_context&    m_ctx;
    d1::small_object_allocator m_allocator;

public:
    task_with_ref_counter(d1::task_group_context& ctx, d1::small_object_allocator& allocator)
        : m_ref_count(1)
        , m_parent(nullptr)
        , m_ctx(ctx)
        , m_allocator(allocator)
    {}

    ~task_with_ref_counter() {}

    void reserve(std::size_t diff = 1) {
        __TBB_ASSERT(m_ref_count.load(std::memory_order_relaxed) != 0, "Cannot reserve if there are no other references");
        m_ref_count.fetch_add(diff);
    }

    void release(std::size_t diff = 1, d1::execution_data* ed_ptr = nullptr) {
        __TBB_ASSERT(m_ref_count.load(std::memory_order_relaxed) >= diff, "Overflow detected");
        if (m_ref_count.fetch_sub(diff) == diff) {
            task_with_ref_counter* parent = m_parent;
            destroy_task(this, ed_ptr, m_allocator);
            if (parent) {
                parent->release(1, ed_ptr);
            }
        }
    }

    void set_parent(task_with_ref_counter* parent) {
        __TBB_ASSERT(m_parent == nullptr, nullptr);
        m_parent = parent;
    }

    d1::task_group_context& ctx() { return m_ctx; }
};

class listed_task : public d1::task {
protected:
    listed_task*                      m_next_task;
#if COUNT_NUMS
    std::size_t                     m_num_elements_before;
#endif
    d1::task_group_context&         m_ctx;
    task_with_ref_counter*          m_parent;
    d1::small_object_allocator      m_allocator;

public:
    listed_task(d1::task_group_context& ctx, d1::small_object_allocator& allocator)
        : m_next_task(nullptr)
#if COUNT_NUMS
        , m_num_elements_before(0)
#endif
        , m_ctx(ctx)
        , m_allocator(allocator)
    {}

    ~listed_task() override {}

    virtual void destroy(d1::execution_data& ed) = 0;

    d1::task_group_context& ctx() const { return m_ctx; }
    listed_task*& next() { return m_next_task; }

    void set_parent(task_with_ref_counter* parent) {
        __TBB_ASSERT(m_parent == nullptr, "Parent already set");
        m_parent = parent;
    }
};

template <typename F>
class listed_function_task : public listed_task {
    // TODO: enable EBO for this task before releasing
    const F m_function;

public:
    template <typename FF>
    listed_function_task(FF&& function, d1::task_group_context& ctx, d1::small_object_allocator& alloc)
        : listed_task(ctx, alloc)
        , m_function(std::forward<FF>(function))
    {}

    d1::task* execute(d1::execution_data& ed) override {
        __TBB_ASSERT(ed.context == &this->ctx(), "The task group context should be used for all tasks");
        m_function();
        destroy(ed);
        return nullptr;
    }

    d1::task* cancel(d1::execution_data& ed) override {
        __TBB_ASSERT(false, "Cancel should not be called yet");
        return nullptr;
    }

    void destroy(d1::execution_data& ed) override {
        task_with_ref_counter* parent = m_parent;
        destroy_task(this, &ed, m_allocator);
        __TBB_ASSERT(parent != nullptr, nullptr);
        parent->release(1, &ed);
    }
};

class crop_task : public task_with_ref_counter {
    listed_task* m_list;
    std::size_t  m_num_prev_cropped;
public:
    crop_task(listed_task* list, d1::task_group_context& ctx, d1::small_object_allocator& allocator, std::size_t num_prev_cropped)
        : task_with_ref_counter(ctx, allocator)
        , m_list(list)
        , m_num_prev_cropped(num_prev_cropped)
    {
        __TBB_ASSERT(list != nullptr, nullptr);

    }

    ~crop_task() {}

     // TODO: experiment with other (possibly dynamic) values
    static constexpr std::size_t min_num_tasks_to_crop = 4;
    static d1::task* crop_and_bypass(listed_task* list_ptr, task_with_ref_counter* requester_task, d1::execution_data& ed, std::size_t num_prev_cropped) {
        std::size_t num_tasks_to_crop = execution_slot(ed) == original_slot(ed) ? num_prev_cropped * 2 : min_num_tasks_to_crop;

        d1::small_object_allocator alloc;
        
        std::size_t num_cropped_tasks = 0;
        listed_task* remainder = list_ptr;

        while (num_cropped_tasks < num_tasks_to_crop && remainder != nullptr) {
            ++num_cropped_tasks;
            remainder = remainder->next();
        }

        if (remainder != nullptr) {
            requester_task->reserve(1);
            crop_task* remainder_task = alloc.new_object<crop_task>(remainder, requester_task->ctx(), alloc, num_tasks_to_crop);
            remainder_task->set_parent(requester_task);
            r1::spawn(*remainder_task, requester_task->ctx());
        }

        requester_task->reserve(num_cropped_tasks);
        listed_task* bypass_task = list_ptr;
        bypass_task->set_parent(requester_task);
        list_ptr = list_ptr->next();

        while (list_ptr != remainder) {
            listed_task* next = list_ptr->next();
            list_ptr->set_parent(requester_task);
            r1::spawn(*list_ptr, list_ptr->ctx());
            list_ptr = next;
        }
        return bypass_task;
    }

    d1::task* execute(d1::execution_data& ed) override {
        __TBB_ASSERT(ed.context == &ctx(), "The task group context should be used for all tasks");

        d1::task* t = crop_and_bypass(m_list, this, ed, m_num_prev_cropped);
        release(); // release self-reference
        return t;
    }

    d1::task* cancel(d1::execution_data& ed) override {
        __TBB_ASSERT(false, "Should not be called");
        return nullptr;
    }
};

class grab_task : public task_with_ref_counter {
    std::atomic<listed_task*>&      m_list_to_grab;
    d1::wait_tree_vertex_interface* m_wait_tree_vertex;
public:
    grab_task(std::atomic<listed_task*>& list_to_grab, d1::wait_tree_vertex_interface* wait_tree_vertex,
              d1::task_group_context& ctx, d1::small_object_allocator& allocator)
        : task_with_ref_counter(ctx, allocator)
        , m_list_to_grab(list_to_grab)
        , m_wait_tree_vertex(wait_tree_vertex)
    {
        __TBB_ASSERT(m_wait_tree_vertex != nullptr, nullptr);
        m_wait_tree_vertex->reserve(1);
    }

    ~grab_task() {
        m_wait_tree_vertex->release(1);
    }

    d1::task* execute(d1::execution_data& ed) override {
        __TBB_ASSERT(ed.context == &ctx(), "The task group context should be used for all tasks");
        listed_task* task_ptr = m_list_to_grab.exchange(nullptr);
        __TBB_ASSERT(task_ptr != nullptr, "Grab task should take at least one task");

        d1::task* t = crop_task::crop_and_bypass(task_ptr, this, ed, crop_task::min_num_tasks_to_crop);

        release(); // release self-reference
        return t;
    }

    d1::task* cancel(d1::execution_data& ed) override {
        __TBB_ASSERT(false, "Should not be called");
        return nullptr;
    }
}; // class grab_task

#ifndef NUM_PER_THREAD_MICRO_LISTS
#define NUM_PER_THREAD_MICRO_LISTS 1
#endif

struct task_list {
    static constexpr std::size_t num_micro_lists = NUM_PER_THREAD_MICRO_LISTS;

    task_list()
        : push_index(0)
    {
        for (std::size_t i = 0; i < num_micro_lists; ++i) {
            micro_lists[i].store(nullptr, std::memory_order_relaxed);
        }
    }

    template <typename F>
    void push(F&& f, d1::wait_context_vertex& group_wait_context_vertex, d1::task_group_context& group_context) {
        d1::small_object_allocator alloc;
        using task_type = listed_function_task<typename std::decay<F>::type>;
        listed_task* t = alloc.new_object<task_type>(std::forward<F>(f), group_context, alloc);

        std::atomic<listed_task*>& micro_list = micro_lists[push_index];
        push_index = (push_index + 1) % num_micro_lists;

        listed_task* current_head_task = micro_list.load(std::memory_order_relaxed);
        t->next() = current_head_task;
#if COUNT_NUMS
        t->num_elements_before() = 1 + current_head_task->num_elements_before();
#endif

        // Only the owning thread and the grab task access micro_list
        // grab task only doing exchange(nullptr)
        while (!micro_list.compare_exchange_strong(current_head_task, t)) {
            // grab task took the list for processing
            // current_head_task is updated by CAS
            __TBB_ASSERT(current_head_task == nullptr, nullptr);
            t->next() = current_head_task;
#if COUNT_NUMS
            t->num_elements_before() = 0;
#endif
        }

        if (current_head_task == nullptr) {
            // The first item in the list, spawn the grab task
            grab_task* grab_t = alloc.new_object<grab_task>(micro_list, r1::get_thread_reference_vertex(&group_wait_context_vertex), group_context, alloc);
            r1::spawn(*grab_t, group_context);
        }
    }

private:
    std::atomic<listed_task*> micro_lists[num_micro_lists];
    std::size_t               push_index;
};

class aggregating_task_group {
private:
    d1::wait_context_vertex m_wait_vertex;
    d1::task_group_context  m_context;
    static thread_local task_list m_task_list;

    d1::task_group_context& context() noexcept {
        return m_context.actual_context();
    }

public:
    aggregating_task_group()
        : m_wait_vertex(0)
        , m_context(d1::task_group_context::bound, d1::task_group_context::default_traits | d1::task_group_context::concurrent_wait)
    {}

    aggregating_task_group(d1::task_group_context& ctx)
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
            // in case of missing wait (for the sake of better testability & debuggability)
            if (!context().is_group_execution_cancelled())
                cancel();
            d1::wait(m_wait_vertex.get_context(), context());
            if (!stack_unwinding_in_progress)
                throw_exception(exception_id::missing_wait);
        }
    }

    void cancel() {
        context().cancel_group_execution();
    }

    task_group_status wait() {
        bool cancellation_status = false;
        try_call([&] {
            d1::wait(m_wait_vertex.get_context(), context());
        }).on_completion([&] {
            cancellation_status = context().is_group_execution_cancelled();
        });
        return cancellation_status ? canceled : complete;
    }

    template <typename F>
    void run(F&& f) {
        m_task_list.push(std::forward<F>(f), m_wait_vertex, context());
    }

    template <typename F>
    task_group_status run_and_wait(const F& f);
}; // class aggregating_task_group

thread_local task_list aggregating_task_group::m_task_list;

} // namespace d2
} // namespace detail

inline namespace v1 {
using detail::d2::aggregating_task_group;
} // inline namespace v1

} // namespace tbb


#endif // __TBB_aggregating_task_group_H
