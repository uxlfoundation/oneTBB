#ifndef __TBB_parallel_while_H
#define __TBB_parallel_while_H

#include "detail/_config.h"
#include "detail/_namespace_injection.h"
#include "detail/_assert.h"
#include "detail/_utils.h"
#include "detail/_task.h"
#include "detail/_small_object_pool.h"
#include "task_group.h"
#include <atomic>

namespace tbb {
namespace detail {
namespace d1 {

template <typename T>
void destroy_task(T* task, execution_data& ed, small_object_allocator& alloc) {
    // TODO: investigate segfault in task deallocation
    (void)task;
    (void)ed;
    (void)alloc;
    // alloc.delete_object(task, ed);
}

class task_with_ref_count : public task {
    std::atomic<std::size_t> m_ref_count;
public:
    task_with_ref_count() : m_ref_count(1) {}

    void reserve() { ++m_ref_count; }
    void release(execution_data& ed) {
        if (--m_ref_count == 0) {
            destroy(ed);
        }
    }

    virtual void destroy(execution_data& ed) = 0;
};

class listed_task : public task {
    listed_task*           m_next_task;
    task_with_ref_count*   m_parent;
    task_group_context&    m_ctx;
    small_object_allocator m_allocator;
public:
    listed_task(task_group_context& ctx, small_object_allocator& allocator)
        : m_next_task(nullptr)
        , m_parent(nullptr)
        , m_ctx(ctx)
        , m_allocator(allocator)
    {}

    listed_task*& next() { return m_next_task; }
    task_with_ref_count*& parent() { return m_parent; }
    task_group_context& ctx() { return m_ctx; }
    small_object_allocator& allocator() { return m_allocator; }
};

class block_handling_task : public task_with_ref_count {
    listed_task*            m_task_list;
    task_with_ref_count&    m_parent;
    task_group_context&     m_ctx;
    small_object_allocator& m_allocator;
public:
    block_handling_task(listed_task* task_list, task_with_ref_count& parent, task_group_context& ctx, small_object_allocator& allocator)
        : m_task_list(task_list)
        , m_parent(parent)
        , m_ctx(ctx)
        , m_allocator(allocator)
    {}

    task* execute(execution_data& ed) override {
        __TBB_ASSERT(ed.context == &m_ctx, "The task group context should be used for all tasks");
        __TBB_ASSERT(m_task_list != nullptr, "Empty handling task should not be created");
        listed_task* bypass_task = nullptr;

        while (m_task_list != nullptr) {
            listed_task* next_task = m_task_list->next();
            m_task_list->parent() = this;

            if (bypass_task == nullptr && &m_ctx == &m_task_list->ctx()) {
                bypass_task = m_task_list;
            } else {
                r1::spawn(*m_task_list, m_task_list->ctx());
            }

            m_task_list = next_task;
        }

        __TBB_ASSERT(&m_ctx == &bypass_task->ctx(), "Cannot bypass task from different context");
        return bypass_task;
    }

    task* cancel(execution_data& ed) override {
        __TBB_ASSERT(false, "Should not be called");
        return nullptr;
    }

    void destroy(execution_data& ed) override {
        task_with_ref_count& parent = m_parent;
        destroy_task(this, ed, m_allocator);
        parent.release(ed);
    }
};

class block_splitting_task : public task_with_ref_count {
    listed_task*            m_task_list;
    task_with_ref_count&    m_parent;
    task_group_context&     m_ctx;
    small_object_allocator& m_allocator;
public:
    static constexpr std::size_t max_block_size = 4;

    block_splitting_task(listed_task* task_list, task_with_ref_count& parent, task_group_context& ctx, small_object_allocator& allocator)
        : m_task_list(task_list)
        , m_parent(parent)
        , m_ctx(ctx)
        , m_allocator(allocator)
    {}

    task* execute(execution_data& ed) override {
        listed_task* block_begin = m_task_list;
        listed_task* last_block_task = nullptr;
        std::size_t block_size = 0;

        while (m_task_list != nullptr && block_size < max_block_size) {
            last_block_task = m_task_list;
            m_task_list = m_task_list->next();
            ++block_size;
        }

        // Terminate the block
        last_block_task->next() = nullptr;

        small_object_allocator alloc;
        
        this->reserve();
        block_handling_task* handle_block_task = alloc.new_object<block_handling_task>(block_begin, *this, m_ctx, alloc);

        if (m_task_list != nullptr) {
            r1::spawn(*this, m_ctx);
        } else {
            release(ed);
        }

        return handle_block_task;
    }

    task* cancel(execution_data& ed) override {
        __TBB_ASSERT(false, "Should not be called");
        return nullptr;
    }

    void destroy(execution_data& ed) override {
        task_with_ref_count& parent = m_parent;
        destroy_task(this, ed, m_allocator);
        parent.release(ed);
    }
};

class grab_task : public task_with_ref_count {
    std::atomic<listed_task*>&  m_grab_list;
    wait_tree_vertex_interface* m_wait_tree_vertex;
    task_group_context&         m_ctx;
    small_object_allocator&     m_allocator;
public:
    grab_task(std::atomic<listed_task*>& grab_list, wait_tree_vertex_interface* wait_tree_vertex,
              task_group_context& ctx, small_object_allocator& allocator)
        : m_grab_list(grab_list)
        , m_wait_tree_vertex(wait_tree_vertex)
        , m_ctx(ctx)
        , m_allocator(allocator)
    {
        __TBB_ASSERT(m_wait_tree_vertex, nullptr);
        m_wait_tree_vertex->reserve();
    }

    task* execute(execution_data& ed) override {
        __TBB_ASSERT(ed.context == &m_ctx, "The task group context should be used for all tasks");
        listed_task* task_ptr = m_grab_list.exchange(nullptr);
        __TBB_ASSERT(task_ptr != nullptr, "Grab task should take at least one task");

        // Do first iteration of block splitting
        listed_task* block_begin = task_ptr;
        listed_task* last_block_task = nullptr;
        std::size_t block_size = 0;
        while (task_ptr != nullptr && block_size < block_splitting_task::max_block_size) {
            last_block_task = task_ptr;
            task_ptr = task_ptr->next();
            ++block_size;
        }

        // Terminate the block
        last_block_task->next() = nullptr;

        small_object_allocator alloc;
        if (task_ptr != nullptr) {
            // Task list still has work to proceed
            this->reserve();
            block_splitting_task* split_task = alloc.new_object<block_splitting_task>(task_ptr, *this, m_ctx, alloc);
            r1::spawn(*split_task, m_ctx);
        }

        // Keeping the reference counter as is - plus one for handle_block_task and minus one for this
        block_handling_task* handle_block_task = alloc.new_object<block_handling_task>(block_begin, *this, m_ctx, alloc);
        return handle_block_task;
    }

    task* cancel(execution_data& ed) override {
        __TBB_ASSERT(false, "Should not be called");
        return nullptr;
    }

    void destroy(execution_data& ed) override {
        wait_tree_vertex_interface* wait_tree_vertex = m_wait_tree_vertex;
        destroy_task(this, ed, m_allocator);
        wait_tree_vertex->release();
    }
};

template <typename Body, typename Input>
class listed_function_task : public listed_task {
    Body  m_body;
    Input m_input;
public:
    listed_function_task(const Body& body, Input&& input, task_group_context& ctx, small_object_allocator& alloc)
        : listed_task(ctx, alloc)
        , m_body(body)
        , m_input(std::move(input))
    {}

    task* execute(execution_data& ed) override {
        __TBB_ASSERT(ed.context == &this->ctx(), "The task group context should be used for all tasks");
        m_body(std::move(m_input));
        this->parent()->release(ed);
        destroy_task(this, ed, this->allocator());
        return nullptr;
    }

    task* cancel(d1::execution_data& ed) override {
        __TBB_ASSERT(false, "Should not be called");
        return nullptr;
    }
};

template <typename Input, typename Body>
void add_to_list(std::atomic<listed_task*>& task_list, const Body& body, Input&& input,
                 wait_context_vertex& wait_vertex, task_group_context& context,
                 small_object_allocator& alloc) {
    using function_task_type = listed_function_task<typename std::decay<Body>::type, Input>;
    function_task_type* t = alloc.new_object<function_task_type>(body, std::move(input), context, alloc);
    listed_task* current_list_head = task_list.load(std::memory_order_relaxed);
    t->next() = current_list_head;

    // Only the owning thread and the grab task access task_list
    // grab task only doing exchange(nullptr)
    while (!task_list.compare_exchange_strong(current_list_head, t)) {
        // grab task took the list for processing
        // current_list_head is updated by CAS
        __TBB_ASSERT(current_list_head == nullptr, nullptr);
        t->next() = current_list_head;
    }

    if (current_list_head == nullptr) {
        // The first item in the list, spawn the grab task
        grab_task* grab = alloc.new_object<grab_task>(task_list, &wait_vertex, context, alloc);
        r1::spawn(*grab, context);
    }
}

template <typename Generator, typename Predicate, typename Body>
void parallel_while(const Generator& generator, const Predicate& pred, const Body& body, task_group_context& context) {
    auto input = generator();

    if (pred(input)) {
        wait_context_vertex wait_vertex;
        d1::small_object_allocator alloc;
        static thread_local std::atomic<listed_task*> task_list(nullptr);

        add_to_list(task_list, body, std::move(input), wait_vertex, context, alloc);

        input = generator();
        while (pred(input)) {
            add_to_list(task_list, body, std::move(input), wait_vertex, context, alloc);
            input = generator();
        }

        r1::wait(wait_vertex.get_context(), context);
    }
}

template <typename Generator, typename Predicate, typename Body>
void parallel_while(const Generator& generator, const Predicate& pred, const Body& body) {
    // task_group_context context(PARALLEL_WHILE);
    task_group_context context;
    parallel_while(generator, pred, body, context);
}

} // namespace d1
} // namespace detail

inline namespace v1 {
using detail::d1::parallel_while;
}

} // namespace tbb

#endif // __TBB_parallel_while_H
