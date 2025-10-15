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
    if (ed) {
        alloc.delete_object(task, *ed);
    } else {
        alloc.delete_object(task);
    }
}

class chunk_root_task;

class list_task : public d1::task {
protected:
    list_task*                      m_next_task;
#if COUNT_NUMS
    std::size_t                     m_num_elements_before;
#endif
    d1::wait_tree_vertex_interface* m_wait_tree_vertex;
    d1::task_group_context&         m_ctx;
    d1::small_object_allocator      m_allocator;

public:
    list_task(d1::task_group_context& ctx, d1::small_object_allocator& allocator)
        : m_next_task(nullptr)
#if COUNT_NUMS
        , m_num_elements_before(0)
#endif
        , m_wait_tree_vertex(nullptr)
        , m_ctx(ctx)
        , m_allocator(allocator)
    {}

    ~list_task() override {
        __TBB_ASSERT(m_wait_tree_vertex != nullptr, "m_wait_tree_vertex should be set before the destruction");
        m_wait_tree_vertex->release();
    }

    virtual void destroy(d1::execution_data& ed) = 0;

    d1::task_group_context& ctx() const { return m_ctx; }
    list_task*& next() { return m_next_task; }
    d1::wait_tree_vertex_interface*& wait_tree_vertex() { return m_wait_tree_vertex; }
#if COUNT_NUMS
    std::size_t& num_elements_before() { return m_num_elements_before; }
#endif
};

template <typename F>
class function_list_task : public list_task {
    // TODO: enable EBO for this task before releasing
    const F m_function;

public:
    template <typename FF>
    function_list_task(FF&& function, d1::task_group_context& ctx, d1::small_object_allocator& alloc)
        : list_task(ctx, alloc)
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
        destroy_task(this, &ed, m_allocator);
    }
};

class chunk_root_task : public d1::task, public d1::wait_tree_vertex_interface {
    d1::task_group_context&         m_ctx;
    d1::wait_tree_vertex_interface* m_root_wait_tree_vertex;
    
    list_task*                      m_first_task;
    list_task*                      m_second_task;
    list_task*                      m_third_task;
    
    std::atomic<std::size_t>        m_ref_count;
    d1::small_object_allocator&     m_allocator;
public:
    chunk_root_task(list_task* first_task, list_task* second_task, list_task* third_task,
                    d1::task_group_context& ctx, d1::wait_tree_vertex_interface* root_vertex,
                    d1::small_object_allocator& allocator)
        : m_ctx(ctx)
        , m_root_wait_tree_vertex(root_vertex)
        , m_first_task(first_task), m_second_task(second_task), m_third_task(third_task)
        , m_ref_count(4) // 3 for child tasks, one for self
        , m_allocator(allocator)
    {
        __TBB_ASSERT(first_task != nullptr && second_task != nullptr && third_task != nullptr, nullptr);
        m_root_wait_tree_vertex->reserve();
    }

    void reserve(std::uint32_t = 1) override {
        __TBB_ASSERT(false, "Should not be called");
    }

    void release(std::uint32_t delta = 1) override {
        if (--m_ref_count == 0) {
            d1::wait_tree_vertex_interface* root_wait_vertex = m_root_wait_tree_vertex;
            // TODO: investigate how to propagate the execution data here
            // destroy_task(this, nullptr, m_allocator);
            this->~chunk_root_task();

            root_wait_vertex->release();
        }
    }

    d1::task* execute(d1::execution_data& ed) override {
        __TBB_ASSERT(ed.context == &m_ctx, "The task group context should be used for all tasks");
        d1::task* bypass_task = nullptr;

        m_first_task->wait_tree_vertex() = this;
        if (&m_ctx == &m_first_task->ctx()) {
            bypass_task = m_first_task;
        } else {
            r1::spawn(*m_first_task, m_first_task->ctx());
        }

        m_second_task->wait_tree_vertex() = this;
        if (bypass_task == nullptr && &m_ctx == &m_second_task->ctx()) {
            bypass_task = m_second_task;
        } else {
            r1::spawn(*m_second_task, m_second_task->ctx());
        }

        m_third_task->wait_tree_vertex() = this;
        if (bypass_task == nullptr && &m_ctx == &m_third_task->ctx()) {
            bypass_task = m_third_task;
        } else {
            r1::spawn(*m_third_task, m_third_task->ctx());
        }

        release();
        return bypass_task;
    }

    d1::task* cancel(d1::execution_data& ed) override {
        __TBB_ASSERT(false, "Should not be called");
        return nullptr;
    }
};

#if COUNT_NUMS
static tbb::concurrent_vector<std::size_t> statistics;
#endif

class grab_task : public d1::task {
    std::atomic<list_task*>&        m_list_to_grab;
    d1::wait_tree_vertex_interface* m_wait_tree_vertex;
    d1::task_group_context&         m_ctx;
    d1::small_object_allocator      m_allocator;
public:
    grab_task(std::atomic<list_task*>& list_to_grab, d1::wait_tree_vertex_interface* wait_tree_vertex,
              d1::task_group_context& ctx, d1::small_object_allocator& allocator)
        : m_list_to_grab(list_to_grab)
        , m_wait_tree_vertex(wait_tree_vertex)
        , m_ctx(ctx)
        , m_allocator(allocator)
    {
        __TBB_ASSERT(m_wait_tree_vertex != nullptr, nullptr);
        m_wait_tree_vertex->reserve();
    }

    ~grab_task() {
        m_wait_tree_vertex->release();
    }

    d1::task* execute(d1::execution_data& ed) override {
        __TBB_ASSERT(ed.context == &m_ctx, "The task group context should be used for all tasks");
        list_task* task_ptr = m_list_to_grab.exchange(nullptr);
        __TBB_ASSERT(task_ptr != nullptr, "Grab task should take at least one task");

#if COUNT_NUMS
        statistics.emplace_back(task_ptr->num_elements_before() + 1);
#endif

        d1::task* bypass_task = nullptr;

        d1::small_object_allocator alloc;
        // Chunk loop
        while (task_ptr != nullptr && task_ptr->next() != nullptr && task_ptr->next()->next() != nullptr) {
            list_task* next_task = task_ptr->next()->next()->next();
            chunk_root_task* chunk_task = alloc.new_object<chunk_root_task>(task_ptr, task_ptr->next(), task_ptr->next()->next(),
                                                                            m_ctx, m_wait_tree_vertex, alloc);

            if (bypass_task == nullptr) {
                bypass_task = chunk_task;
            } else {
                r1::spawn(*chunk_task, m_ctx);
            }
            task_ptr = next_task;
        }

        // Not enough tasks to form a chunk
        while (task_ptr != nullptr) {
            list_task* next_task = task_ptr->next();

            m_wait_tree_vertex->reserve();
            task_ptr->wait_tree_vertex() = m_wait_tree_vertex;

            if (bypass_task == nullptr && &m_ctx == &task_ptr->ctx()) {
                bypass_task = task_ptr;
            } else {
                r1::spawn(*task_ptr, task_ptr->ctx());
            }
            task_ptr = next_task;
        }
        destroy_task(this, &ed, m_allocator);
        return bypass_task;
    }

    d1::task* cancel(d1::execution_data& ed) override {
        __TBB_ASSERT(false, "Should not be called");
        return nullptr;
    }
};

class aggregating_task_group {
private:
    d1::wait_context_vertex m_wait_vertex;
    d1::task_group_context  m_context;
    static thread_local std::atomic<list_task*> m_task_list;

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
        d1::small_object_allocator alloc;
        using task_type = function_list_task<typename std::decay<F>::type>;
        list_task* t = alloc.new_object<task_type>(std::forward<F>(f), context(), alloc);

        list_task* current_list_head = m_task_list.load(std::memory_order_relaxed);
        t->next() = current_list_head;
#if COUNT_NUMS
        if (current_list_head) t->num_elements_before() = 1 + current_list_head->num_elements_before();
#endif

        // Only the owning thread and the grab task access m_task_list
        // grab task only doing exchange(nullptr)
        while (!m_task_list.compare_exchange_strong(current_list_head, t)) {
            __TBB_ASSERT(current_list_head == nullptr, nullptr);
            // grab task took the list for processing
            // current_list_head is updated by CAS
            t->next() = current_list_head;
#if COUNT_NUMS
            t->num_elements_before() = 0;
#endif
        }

        if (current_list_head == nullptr) {
            // The first item in the list, spawn the grab task
            grab_task* grab = alloc.new_object<grab_task>(m_task_list, r1::get_thread_reference_vertex(&m_wait_vertex),
                                                          context(), alloc);
            r1::spawn(*grab, context());
        }
    }

    template <typename F>
    task_group_status run_and_wait(const F& f);
}; // class aggregating_task_group

thread_local std::atomic<list_task*> aggregating_task_group::m_task_list;

} // namespace d2
} // namespace detail

inline namespace v1 {
using detail::d2::aggregating_task_group;
} // inline namespace v1

} // namespace tbb


#endif // __TBB_aggregating_task_group_H
