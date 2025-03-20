/*
    Copyright (c) 2020-2024 Intel Corporation

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/


#ifndef __TBB_task_handle_H
#define __TBB_task_handle_H

#include "_config.h"
#include "_task.h"
#include "_small_object_pool.h"
#include "_utils.h"
#include <memory>

namespace tbb {
namespace detail {

namespace d1 { class task_group_context; class wait_context; struct execution_data; }
namespace d2 {

class task_handle;

#if __TBB_PREVIEW_TASK_GROUP_EXTENSIONS

class task_with_dynamic_state;
class task_handle_task;
class successor_vertex;

inline task_with_dynamic_state* release_successors_list(successor_vertex* list);
class task_dynamic_state {
    enum class task_status {
        no_status = 0,
        submitted = 1,
        completed = 2
    };
public:
    task_dynamic_state(task_with_dynamic_state* task, d1::small_object_allocator& alloc)
        : m_task(task)
        , m_task_status(task_status::no_status)
        , m_successors_list_head(nullptr)
        , m_continuation_vertex(nullptr)
        , m_num_references(0)
        , m_allocator(alloc)
    {}

    void reserve() { ++m_num_references; }

    void release() {
        if (--m_num_references == 0) {
            m_allocator.delete_object(this);
        }
    }

    void mark_submitted() {
        m_task_status.store(task_status::submitted, std::memory_order_release);
    }

    task_with_dynamic_state* complete_task() {
        m_task_status.store(task_status::completed, std::memory_order_release);   

        task_with_dynamic_state* next_task = release_successors_list(m_successors_list_head.exchange(nullptr));

        // Releasing the task ownership of the dynamic_state
        release();

        return next_task;
    }

    bool was_submitted() const {
        return m_task_status.load(std::memory_order_acquire) >= task_status::submitted;
    }

    bool is_completed() const {
        return m_task_status.load(std::memory_order_acquire) == task_status::completed;
    }

    bool has_dependencies() const {
        return m_continuation_vertex.load(std::memory_order_acquire) != nullptr;
    }
    
    void unset_dependency() {
        m_continuation_vertex.store(nullptr, std::memory_order_release);
    }

    void add_successor(successor_vertex* successor);
    void release_continuation();
    
    successor_vertex* get_continuation_vertex() {
        successor_vertex* current_continuation_vertex = m_continuation_vertex.load(std::memory_order_acquire);

        if (current_continuation_vertex == nullptr) {
            d1::small_object_allocator alloc;
            successor_vertex* new_continuation_vertex = alloc.new_object<successor_vertex>(m_task, alloc);

            if (!m_continuation_vertex.compare_exchange_strong(current_continuation_vertex, new_continuation_vertex)) {
                // Other thread already created a continuation vertex
                alloc.delete_object(new_continuation_vertex);
            } else {
                // This thread has updated the continuation vertex
                current_continuation_vertex = new_continuation_vertex;
            }
        }
        __TBB_ASSERT(current_continuation_vertex != nullptr, "Failed to get continuation vertex");
        return current_continuation_vertex;
    }

private:
    task_with_dynamic_state* m_task;
    std::atomic<task_status> m_task_status;
    std::atomic<successor_vertex*> m_successors_list_head;
    std::atomic<successor_vertex*> m_continuation_vertex;
    std::atomic<std::size_t> m_num_references;
    d1::small_object_allocator m_allocator;
};

inline void internal_make_edge(task_dynamic_state* pred, task_dynamic_state* succ) {
    __TBB_ASSERT(pred != nullptr && succ != nullptr , nullptr);
    pred->add_successor(succ->get_continuation_vertex());
}

class task_with_dynamic_state : public d1::task {
public:
    task_with_dynamic_state()
        : m_state(nullptr)
    {}

    task_with_dynamic_state(d1::small_object_allocator& alloc)
        : m_state(nullptr)
        , m_allocator(alloc)
    {}

    task_dynamic_state* get_dynamic_state() {
        if (m_state == nullptr) {
            m_state = m_allocator.new_object<task_dynamic_state>(this, m_allocator);
        }
        return m_state;
    }

    task_dynamic_state* get_dynamic_state_if_created() {
        return m_state;
    }
private:
    task_dynamic_state* m_state;
protected:
    d1::small_object_allocator m_allocator;
};

inline task_dynamic_state* get_current_task_dynamic_state() {
    d1::task* t = d1::current_task();
    __TBB_ASSERT_RELEASE(t != nullptr, "the function was called outside of task body");
    
    task_with_dynamic_state* t_with_state = dynamic_cast<task_with_dynamic_state*>(t);
    __TBB_ASSERT_RELEASE(t_with_state != nullptr, "the function was called outside of task_group task body");
    
    return t_with_state->get_dynamic_state();
}

class successor_vertex : public d1::reference_vertex {
public:
    successor_vertex(task_with_dynamic_state* task, d1::small_object_allocator& alloc)
        // Reserving 1 for task_handle that owns the task for which the predecessors are added
        // reference counter would be released when this task_handle would be submitted for execution
        : d1::reference_vertex(nullptr, 1)
        , m_task(task)
        , m_next_successor(nullptr)
        , m_allocator(alloc)
    {}

    task_with_dynamic_state* release_bypass(std::uint32_t delta = 1) {
        task_with_dynamic_state* next_task = nullptr;

        std::uint32_t ref = m_ref_count.fetch_sub(static_cast<std::uint64_t>(delta)) - static_cast<uint64_t>(delta);

        if (ref == 0) {
            // TODO: can we skip this step since the task would be spawned anyway
            m_task->get_dynamic_state()->unset_dependency();
            next_task = m_task;
            m_allocator.delete_object(this);
        }
        return next_task;
    }

    void set_next_successor(successor_vertex* next) {
        m_next_successor = next;
    }

    successor_vertex* get_next_successor() const {
        return m_next_successor;
    }
private:
    task_with_dynamic_state* m_task;
    successor_vertex* m_next_successor;
    d1::small_object_allocator m_allocator;
}; // class successor_vertex
#endif // __TBB_PREVIEW_TASK_GROUP_EXTENSIONS

class task_handle_task
#if __TBB_PREVIEW_TASK_GROUP_EXTENSIONS
: public task_with_dynamic_state
#else
: public d1::task
#endif
{
    std::uint64_t m_version_and_traits{};
    d1::wait_tree_vertex_interface* m_wait_tree_vertex;
    d1::task_group_context& m_ctx;
#if !__TBB_PREVIEW_TASK_GROUP_EXTENSIONS
    d1::small_object_allocator m_allocator;
#endif
public:
    void finalize(const d1::execution_data* ed = nullptr) {
        if (ed) {
            m_allocator.delete_object(this, *ed);
        } else {
            m_allocator.delete_object(this);
        }
    }

    task_handle_task(d1::wait_tree_vertex_interface* vertex, d1::task_group_context& ctx, d1::small_object_allocator& alloc)
        : 
#if __TBB_PREVIEW_TASK_GROUP_EXTENSIONS
          task_with_dynamic_state(alloc),
#endif
          m_wait_tree_vertex(vertex)
        , m_ctx(ctx)
#if !__TBB_PREVIEW_TASK_GROUP_EXTENSIONS
        , m_allocator(alloc)
#endif
    {
        suppress_unused_warning(m_version_and_traits);
        m_wait_tree_vertex->reserve();
    }

    ~task_handle_task() override {
        m_wait_tree_vertex->release();
    }

    d1::task_group_context& ctx() const { return m_ctx; }
};

class task_handle {
    struct task_handle_task_finalizer_t{
        void operator()(task_handle_task* p){
#if __TBB_PREVIEW_TASK_GROUP_EXTENSIONS
            p->get_dynamic_state()->release();
#endif
            p->finalize();
        }
    };
    using handle_impl_t = std::unique_ptr<task_handle_task, task_handle_task_finalizer_t>;

    handle_impl_t m_handle = {nullptr};
public:
    task_handle() = default;
    task_handle(task_handle&&) = default;
    task_handle& operator=(task_handle&&) = default;

    explicit operator bool() const noexcept { return static_cast<bool>(m_handle); }

    friend bool operator==(task_handle const& th, std::nullptr_t) noexcept;
    friend bool operator==(std::nullptr_t, task_handle const& th) noexcept;

    friend bool operator!=(task_handle const& th, std::nullptr_t) noexcept;
    friend bool operator!=(std::nullptr_t, task_handle const& th) noexcept;

private:
    friend struct task_handle_accessor;
#if __TBB_PREVIEW_TASK_GROUP_EXTENSIONS
    friend class task_tracker;
#endif

    task_handle(task_handle_task* t) : m_handle {t} {
#if __TBB_PREVIEW_TASK_GROUP_EXTENSIONS
        m_handle->get_dynamic_state()->reserve();
#endif
    }

    d1::task* release() {
       return m_handle.release();
    }
};

struct task_handle_accessor {
    static task_handle construct(task_handle_task* t) { return {t}; }

    static d1::task* release(task_handle& th) { return th.release(); }

    static d1::task_group_context& ctx_of(task_handle& th) {
        __TBB_ASSERT(th.m_handle, "ctx_of does not expect empty task_handle.");
        return th.m_handle->ctx();
    }

#if __TBB_PREVIEW_TASK_GROUP_EXTENSIONS
    static void mark_task_submitted(task_handle& th) {
        __TBB_ASSERT(th.m_handle, "mark_task_submitted does not expect empty task_handle");
        task_dynamic_state* state = get_task_dynamic_state(th);
        if (state != nullptr) state->mark_submitted();
    }

    static task_dynamic_state* get_task_dynamic_state(task_handle& th) {
        return th.m_handle->get_dynamic_state_if_created();
    }

    static void release_continuation(task_handle& th) {
        __TBB_ASSERT(th.m_handle, "release_continuation does not expect empty task_handle");
        task_dynamic_state* state = get_task_dynamic_state(th);
        __TBB_ASSERT(state != nullptr, "dynamic state was not created for task with dependencies");
        state->release_continuation();
    }

    static bool has_dependencies(task_handle& th) {
        __TBB_ASSERT(th.m_handle, "has_dependency does not expect empty task_handle");
        task_dynamic_state* state = get_task_dynamic_state(th);
        return state != nullptr ? state->has_dependencies() : false;
    }
#endif
};

inline bool operator==(task_handle const& th, std::nullptr_t) noexcept {
    return th.m_handle == nullptr;
}
inline bool operator==(std::nullptr_t, task_handle const& th) noexcept {
    return th.m_handle == nullptr;
}

inline bool operator!=(task_handle const& th, std::nullptr_t) noexcept {
    return th.m_handle != nullptr;
}

inline bool operator!=(std::nullptr_t, task_handle const& th) noexcept {
    return th.m_handle != nullptr;
}

#if __TBB_PREVIEW_TASK_GROUP_EXTENSIONS
inline task_with_dynamic_state* release_successors_list(successor_vertex* successor) {
    task_with_dynamic_state* next_task = nullptr;

    while (successor != nullptr) {
        successor_vertex* next_successor = successor->get_next_successor();
        task_with_dynamic_state* successor_task = successor->release_bypass();

        if (successor_task != nullptr) {
            if (next_task == nullptr) {
                next_task = successor_task;
            } else {
                // Successor task is guaranteed to be task_handle_task
                // safe to perform static_cast
                d1::spawn(*successor_task, static_cast<task_handle_task*>(successor_task)->ctx());
            }
        }
        successor = next_successor;
    }
    return next_task;
}

inline void task_dynamic_state::add_successor(successor_vertex* successor) {
    __TBB_ASSERT(successor != nullptr, nullptr);

    if (!is_completed()) {
        successor->reserve();

        successor_vertex* current_successors_list_head = m_successors_list_head.load(std::memory_order_acquire);
        successor->set_next_successor(current_successors_list_head);

        while (!m_successors_list_head.compare_exchange_strong(current_successors_list_head, successor)) {
            // Other thread updated the head of the list
            if (is_completed()) {
                // Current task has completed while we tried to insert the successor to the list
                successor->release();
                break;
            }
            successor->set_next_successor(current_successors_list_head);
        }
    }
    // TODO: test single predecessor in completed state
}

inline void task_dynamic_state::release_continuation() {
    successor_vertex* current_vertex = m_continuation_vertex.load(std::memory_order_acquire);
    __TBB_ASSERT(current_vertex != nullptr, "release_continuation requested for task without dependencies");
    task_with_dynamic_state* task = current_vertex->release_bypass();
    
    // Dependent tasks have completed before the task_handle holding the continuation was submitted for execution
    // task_handle was the last owner of the taskand it should be spawned
    if (task != nullptr) {
        // successor task is guaranteed to be task_handle_task, safe to use static_cast
        d1::spawn(*task, static_cast<task_handle_task*>(task)->ctx());
    }
}

inline d1::task* combine_tasks(d1::task* body_task, task_with_dynamic_state* successor_task) {
    if (body_task == nullptr) return successor_task;
    if (successor_task == nullptr) return body_task;

    // There is a task returned from the body and the successor task - bypassing the body task
    // and spawning the successor one
    // successor task is guaranteed to be task_handle_task, it is safe to use static_cast
    d1::spawn(*successor_task, static_cast<task_handle_task*>(successor_task)->ctx());
    return body_task;
}

class task_tracker {
public:
    task_tracker() : m_task_state(nullptr) {}

    task_tracker(const task_tracker& other) 
        : m_task_state(other.m_task_state)
    {
        // Register one more co-owner of the dynamic state
        if (m_task_state) m_task_state->reserve();
    }
    task_tracker(task_tracker&& other)
        : m_task_state(other.m_task_state)
    {
        other.m_task_state = nullptr;
    }

    // TODO: explicit
    task_tracker(const task_handle& th)
        : m_task_state(th ? th.m_handle->get_dynamic_state() : nullptr)
    {
        // Register new co-owner of the dynamic state
        if (m_task_state) m_task_state->reserve();
    }

    ~task_tracker() = default;

    task_tracker& operator=(const task_tracker& other) {
        if (this != &other) {
            // Release co-ownership on the previously tracked dynamic state
            if (m_task_state) m_task_state->release();

            m_task_state = other.m_task_state;

            // Register new co-owner of the new dynamic state
            if (m_task_state) m_task_state->reserve();
        }
        return *this;
    }

    task_tracker& operator=(task_tracker&& other) {
        if (this != &other) {
            // Release co-ownership on the previously tracked dynamic state
            if (m_task_state) m_task_state->release();

            m_task_state = other.m_task_state;
            other.m_task_state = nullptr;
        }
        return *this;
    }

    // TODO: explicit?
    task_tracker& operator=(const task_handle& th) {
        // Release co-ownership on the previously tracked dynamic state
        if (m_task_state) m_task_state->release();

        if (th) {
            m_task_state = th.m_handle->get_dynamic_state();

            // Reserve co-ownership on the new dynamic state
            __TBB_ASSERT(m_task_state != nullptr, "No state in the non-empty task_handle");
            m_task_state->reserve();
        }
        return *this;
    }

    explicit operator bool() const noexcept { return m_task_state != nullptr; }

    bool was_submitted() const {
        __TBB_ASSERT(m_task_state != nullptr, "Cannot get task status on the empty task_tracker");
        return m_task_state->was_submitted();
    }

    bool is_completed() const {
        __TBB_ASSERT(m_task_state != nullptr, "Cannot get task status on the empty task_tracker");
        return m_task_state->is_completed();
    }
private:
    friend bool operator==(const task_tracker& t, std::nullptr_t) noexcept {
        return t.m_task_state == nullptr;
    }

    friend bool operator==(const task_tracker& lhs, const task_tracker& rhs) noexcept {
        return lhs.m_task_state == rhs.m_task_state;
    }

#if !__TBB_CPP20_COMPARISONS_PRESENT
    friend bool operator==(std::nullptr_t, const task_tracker& t) noexcept {
        return t == nullptr;
    }

    friend bool operator!=(const task_tracker& t, std::nullptr_t) noexcept {
        return !(t == nullptr);
    }

    friend bool operator!=(std::nullptr_t, const task_tracker& t) noexcept {
        return !(t == nullptr);
    }

    friend bool operator!=(const task_tracker& lhs, const task_tracker& rhs) noexcept {
        return !(lhs == rhs);
    }
#endif // !__TBB_CPP20_COMPARISONS_PRESENT

    friend struct task_tracker_accessor;

    task_dynamic_state* m_task_state;
};

struct task_tracker_accessor {
    static task_dynamic_state* get_task_dynamic_state(task_tracker& tracker) {
        return tracker.m_task_state;
    }
};
#endif

} // namespace d2
} // namespace detail
} // namespace tbb

#endif /* __TBB_task_handle_H */
