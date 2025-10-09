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
#include <atomic>

#include <iostream>

namespace tbb {
namespace detail {
namespace d2 {

struct task_list_node {
    task_list_node(task_handle_task* t, d1::small_object_allocator& a)
        : task(t)
        , next(nullptr)
#if TRY_DIVIDE_AND_CONQUER
        , num_elements_before(0)
#endif
        , alloc(a)
    {
        __TBB_ASSERT(t != nullptr, "Task should be assigned to task_list_node");
    } 

    static void destroy(task_list_node* node) {
        __TBB_ASSERT(node != nullptr, nullptr);
        d1::small_object_allocator alloc = node->alloc;
        alloc.delete_object(node);
    }

    task_handle_task* task;
    task_list_node* next;
#if TRY_DIVIDE_AND_CONQUER
    std::size_t num_elements_before;
#endif
    d1::small_object_allocator alloc;
};

template <typename T>
void destroy_task(T* task, const d1::execution_data* ed, d1::small_object_allocator alloc) {
    if (ed) {
        alloc.delete_object(task, *ed);
    } else {
        alloc.delete_object(task);
    }
}

#if TRY_DIVIDE_AND_CONQUER
inline constexpr std::size_t divide_and_conquer_grainsize = 16;

class divide_and_conquer_task : public d1::task {
    task_list_node*            m_list;
    d1::task_group_context&    m_ctx;
    d1::small_object_allocator m_alloc;
public:
    divide_and_conquer_task(task_list_node* list, d1::task_group_context& ctx, d1::small_object_allocator& alloc)
        : m_list(list), m_ctx(ctx), m_alloc(alloc)
    {}

    d1::task* execute(d1::execution_data& ed) override {
        __TBB_ASSERT(m_list != nullptr, "List should not be empty");
        std::size_t num_items = m_list->num_elements_before + 1;
        d1::task* next_task = nullptr;

        if (num_items < divide_and_conquer_grainsize) {
            next_task = m_list->task;
            __TBB_ASSERT(next_task != nullptr, nullptr);

            task_list_node* next_node = m_list->next;
            task_list_node::destroy(m_list);
            m_list = next_node;

            while (m_list != nullptr) {
                __TBB_ASSERT(m_list->task != nullptr, nullptr);
                d1::spawn(*m_list->task, m_list->task->ctx());

                next_node = m_list->next;
                task_list_node::destroy(m_list);
                m_list = next_node;
            }
        } else {
            // Continue divide and conquer
            // Finding a middle of the list
            std::size_t middle = num_items / 2;

            task_list_node* left_leaf_last_node = m_list;

            while (left_leaf_last_node->num_elements_before != middle) {
                left_leaf_last_node->num_elements_before -= middle;
                __TBB_ASSERT(left_leaf_last_node != nullptr, nullptr);
                left_leaf_last_node = left_leaf_last_node->next;
            }

            task_list_node* right_leaf_first_node = left_leaf_last_node->next;
            
            // Terminate the left leaf
            left_leaf_last_node->next = nullptr;
            
            d1::small_object_allocator alloc;
            divide_and_conquer_task* left_leaf_processing = alloc.new_object<divide_and_conquer_task>(m_list, m_ctx, alloc);
            divide_and_conquer_task* right_leaf_processing = alloc.new_object<divide_and_conquer_task>(right_leaf_first_node, m_ctx, alloc);

            d1::spawn(*right_leaf_processing, m_ctx);
            next_task = left_leaf_processing;
        }

        destroy_task(this, &ed, m_alloc);
        return next_task;
    }

    d1::task* cancel(d1::execution_data&) override {
        __TBB_ASSERT(false, nullptr);
        return nullptr;
    } 
};

#else

class chunk_processing_task : public d1::task {
    task_list_node*            m_chunk_begin;
    d1::small_object_allocator m_alloc;
public:
    chunk_processing_task(task_list_node* chunk_begin, d1::small_object_allocator& alloc)
        : m_chunk_begin(chunk_begin)
        , m_alloc(alloc)
    {}

    d1::task* execute(d1::execution_data& ed) override {
        __TBB_ASSERT(m_chunk_begin != nullptr, "Chunk should not be empty");
        task_list_node* task_node = m_chunk_begin;

        // Prepare task for bypassing
        task_list_node* next_node = task_node->next;
        d1::task* next_task = task_node->task;
        task_list_node::destroy(task_node);
        task_node = next_node;

        while (task_node != nullptr) {
            next_node = task_node->next;

            __TBB_ASSERT(task_node->task != nullptr, nullptr);
            d1::spawn(*task_node->task, task_node->task->ctx());

            task_list_node::destroy(task_node);
            task_node = next_node;
        }
        destroy_task(this, &ed, m_alloc);
        return next_task;
    }

    d1::task* cancel(d1::execution_data& ed) override {
        __TBB_ASSERT(false, "Should not be executed yet");
        return nullptr;
    }
}; // class chunk_processing_task
#endif

class gathering_task : public d1::task {
    std::atomic<task_list_node*>& m_list_to_gather;
    d1::task_group_context& m_ctx;
    d1::small_object_allocator m_alloc;

    // TODO: which value should it have
    // 3 is from parallel_invoke implementation
    static constexpr std::size_t chunk_size = 3;
public:
    gathering_task(std::atomic<task_list_node*>& list_to_gather, d1::task_group_context& ctx,
                   d1::small_object_allocator& alloc)
        : m_list_to_gather(list_to_gather)
        , m_ctx(ctx)
        , m_alloc(alloc)
    {}

    d1::task* execute(d1::execution_data& ed) override {
        __TBB_ASSERT(ed.context == &m_ctx, "The task group context should be used for all tasks");
        task_list_node* task_list = m_list_to_gather.exchange(nullptr);
        __TBB_ASSERT(task_list != nullptr, "Gathering task should not grab an empty list");
        d1::task* next_task = nullptr;
        // d1::task* next_task = task_list->task;

        // task_list = task_list->next;

        // std::size_t num = 0;
        // while (task_list != nullptr) {
        //     // d1::spawn(*task_list->task, task_list->task->ctx());
        //     __TBB_ASSERT(task_list->task != nullptr, nullptr);
        //     __TBB_ASSERT(&m_ctx == &task_list->task->ctx(), nullptr);
        //     d1::spawn(*task_list->task, m_ctx);
        //     task_list = task_list->next;
        //     ++num;
        // }

        // std::cout << "Gathering task processed " << num << " tasks" << std::endl;
        // return next_task;

#if TRY_DIVIDE_AND_CONQUER
        std::size_t num_items = task_list->num_elements_before + 1;

        if (num_items < divide_and_conquer_grainsize) {
            next_task = task_list->task;
            __TBB_ASSERT(next_task != nullptr, nullptr);

            task_list_node* next_node = task_list->next;
            task_list_node::destroy(task_list);
            task_list = next_node;

            while (task_list != nullptr) {
                __TBB_ASSERT(task_list->task != nullptr, nullptr);
                d1::spawn(*task_list->task, task_list->task->ctx());

                next_node = task_list->next;
                task_list_node::destroy(task_list);
                task_list = next_node;
            }
        } else {
            // Do divide and conquer
            // Finding a middle of the list
            std::size_t middle = num_items / 2;

            task_list_node* left_leaf_last_node = task_list;

            while (left_leaf_last_node->num_elements_before != middle) {
                left_leaf_last_node->num_elements_before -= middle;
                __TBB_ASSERT(left_leaf_last_node != nullptr, nullptr);
                left_leaf_last_node = left_leaf_last_node->next;
            }

            task_list_node* right_leaf_first_node = left_leaf_last_node->next;
            
            // Terminate the left leaf
            left_leaf_last_node->next = nullptr;
            
            d1::small_object_allocator alloc;
            divide_and_conquer_task* left_leaf_processing = alloc.new_object<divide_and_conquer_task>(task_list, m_ctx, alloc);
            divide_and_conquer_task* right_leaf_processing = alloc.new_object<divide_and_conquer_task>(right_leaf_first_node, m_ctx, alloc);

            d1::spawn(*right_leaf_processing, m_ctx);
            next_task = left_leaf_processing;
        }
#else
        while (task_list != nullptr) {
            task_list_node* chunk_begin = task_list;
            task_list_node* chunk_last = nullptr;

            std::size_t count = 0;
            while (task_list != nullptr && count < chunk_size) {
                chunk_last = task_list;
                task_list = task_list->next;
                ++count;
            }
            
            if (count == chunk_size) {
                // Form a chunk
                chunk_last->next = nullptr; // Terminate the chunk
                d1::small_object_allocator alloc;
                chunk_processing_task* process_chunk = alloc.new_object<chunk_processing_task>(chunk_begin, alloc);
                if (next_task == nullptr) {
                    next_task = process_chunk;
                } else {
                    d1::spawn(*process_chunk, m_ctx);
                }
            } else {
                // Not enough tasks to form a chunk
                // Spawn tasks 
                while (chunk_begin != nullptr) {
                    __TBB_ASSERT(chunk_begin->task != nullptr, nullptr);

                    if (next_task == nullptr) {
                        next_task = chunk_begin->task;
                    } else {
                        d1::spawn(*chunk_begin->task, chunk_begin->task->ctx());
                    }

                    task_list_node* next_node = chunk_begin->next;
                    task_list_node::destroy(chunk_begin);
                    chunk_begin = next_node;
                }
            }
        }
#endif
        destroy_task(this, &ed, m_alloc);
        return next_task;
    }

    d1::task* cancel(d1::execution_data& ed) override {
        __TBB_ASSERT(false, "Should not be executed yet");
        return nullptr;
    }
};

class aggregating_task_group {
private:
    d1::wait_context_vertex m_wait_vertex;
    d1::task_group_context  m_context;
    static thread_local std::atomic<task_list_node*> m_task_list;

    d1::task_group_context& context() noexcept {
        return m_context.actual_context();
    }

    template <typename F>
    task_handle_task* prepare_task(F&& f, d1::small_object_allocator& alloc) {
        return alloc.new_object<function_task<typename std::decay<F>::type>>(std::forward<F>(f),
            r1::get_thread_reference_vertex(&m_wait_vertex), context(), alloc);
    }

    gathering_task* prepare_gathering_task(d1::small_object_allocator& alloc) {
        return alloc.new_object<gathering_task>(m_task_list, context(), alloc);
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
        task_handle_task* t = prepare_task(std::forward<F>(f), alloc);
        
        task_list_node* new_list_node = alloc.new_object<task_list_node>(t, alloc);
        task_list_node* current_list_head = m_task_list.load(std::memory_order_relaxed);

        new_list_node->next = current_list_head;
#if TRY_DIVIDE_AND_CONQUER
        new_list_node->num_elements_before = current_list_head ? current_list_head->num_elements_before + 1 : 0;
#endif

        // Only the owning thread and the gathering task access m_task_list
        // gathering task only doing exchange(nullptr)
        while (!m_task_list.compare_exchange_strong(current_list_head, new_list_node)) {
            __TBB_ASSERT(current_list_head == nullptr, nullptr);
            // gathering task took the list for processing
            // current_list_head is updated by the CAS
            new_list_node->next = current_list_head;
#if TRY_DIVIDE_AND_CONQUER
            new_list_node->num_elements_before = 0;
#endif
        }

        if (current_list_head == nullptr) {
            // The first item in the list
            // r1::enqueue(*prepare_gathering_task(alloc), context(), nullptr);
            d1::spawn(*prepare_gathering_task(alloc), context());
        }
    }

    template <typename F>
    task_group_status run_and_wait(const F& f);
}; // class aggregating_task_group

thread_local std::atomic<task_list_node*> aggregating_task_group::m_task_list;

} // namespace d2
} // namespace detail

inline namespace v1 {
using detail::d2::aggregating_task_group;
} // inline namespace v1

} // namespace tbb


#endif // __TBB_aggregating_task_group_H
