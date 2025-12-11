#ifndef __TBB_mail_task_group_H
#define __TBB_mail_task_group_H

#include "detail/_config.h"
#include "detail/_namespace_injection.h"
#include "detail/_assert.h"
#include "detail/_utils.h"
#include "detail/_task.h"
#include "detail/_small_object_pool.h"
#include "task_group.h"
#include "task_arena.h"
#include <atomic>

namespace tbb {
namespace detail {
namespace d1 {

template <typename Function>
class function_task : public task {
private:
    const Function                  m_function;
    d1::wait_tree_vertex_interface* m_wait_tree_vertex;
    small_object_allocator          m_allocator;

public:
    template <typename FF>
    function_task(FF&& ff, d1::wait_tree_vertex_interface* wait_tree_vertex, small_object_allocator& allocator)
        : m_function(std::forward<FF>(ff))
        , m_wait_tree_vertex(wait_tree_vertex)
        , m_allocator(allocator)
    {
        m_wait_tree_vertex->reserve();
    }

    ~function_task() {
        m_wait_tree_vertex->release();
    }

    task* execute(execution_data& ed) override {
        m_function();
        m_allocator.delete_object(this, ed);
        return nullptr;
    }

    task* cancel(execution_data& ed) override {
        __TBB_ASSERT(false, "Should not be used");
        return nullptr;
    }
};

class mail_task_group {
private:
    wait_context_vertex m_wait_vertex;
    task_group_context  m_context;
    slot_id             m_slot;

    task_group_context& context() noexcept {
        return m_context.actual_context();
    }
public:
    mail_task_group()
        : m_wait_vertex(0)
        , m_context(task_group_context::bound, task_group_context::default_traits | task_group_context::concurrent_wait)
        , m_slot(1)
    {
        __TBB_ASSERT(m_slot != slot_id(tbb::this_task_arena::max_concurrency()), nullptr);
    }

    mail_task_group(task_group_context& ctx)
        : m_wait_vertex(0)
        , m_context(&ctx)
    {}

    ~mail_task_group() noexcept(false) {
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
    void run(F&& ff) {
        using task_type = typename std::decay<F>::type;
        small_object_allocator alloc;

        function_task<task_type>* f = alloc.new_object<function_task<task_type>>(std::forward<F>(ff), r1::get_thread_reference_vertex(&m_wait_vertex), alloc);

        slot_id slot_to_spawn = m_slot;
        m_slot = (m_slot + 1) % tbb::this_task_arena::max_concurrency();

        if (m_slot == 0) ++m_slot;

        r1::spawn(*f, context(), slot_to_spawn);
    }
}; // class exp_task_group

} // namespace d1
} // namespace detail

inline namespace v1 {
using detail::d1::mail_task_group;
} // inline namespace v1

} // namespace tbb

#endif // __TBB_mail_task_group_H
