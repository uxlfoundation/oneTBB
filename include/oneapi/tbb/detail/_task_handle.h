/*
    Copyright (c) 2020-2025 Intel Corporation
    Copyright (c) 2025 UXL Foundation Contributors

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

class task_handle_task;
class continuation_vertex;

class successor_list_node {
public:
    successor_list_node(continuation_vertex* successor, d1::small_object_allocator& alloc)
        : m_next_successor(nullptr)
        , m_continuation_vertex(successor)
        , m_allocator(alloc)
    {}

    continuation_vertex* get_continuation_vertex() const {
        return m_continuation_vertex;
    }

    successor_list_node* get_next_node() const {
        return m_next_successor;
    }

    void set_next_node(successor_list_node* next_node) {
        m_next_successor = next_node;
    }

    void finalize() {
        m_allocator.delete_object(this);
    }
private:
    successor_list_node* m_next_successor;
    continuation_vertex* m_continuation_vertex;
    d1::small_object_allocator m_allocator;
};

inline task_handle_task* release_successor_list(successor_list_node* list);

class task_dynamic_state {
public:
    task_dynamic_state(task_handle_task* task, d1::small_object_allocator& alloc)
        : m_task(task)
        , m_successor_list_head(nullptr)
        , m_continuation_vertex(nullptr)
        , m_num_references(1) // reserves a task co-ownership for dynamic state
        , m_allocator(alloc)
    {}

    void reserve() { ++m_num_references; }

    void release() {
        if (--m_num_references == 0) {
            m_allocator.delete_object(this);
        }
    }

    task_handle_task* complete_task() {
        successor_list_node* list = fetch_successor_list();
        return release_successor_list(list);
    }

    bool has_dependencies() const {
        return m_continuation_vertex.load(std::memory_order_acquire) != nullptr;
    }
    
    void unset_dependency() {
        m_continuation_vertex.store(nullptr, std::memory_order_release);
    }

    void add_successor(continuation_vertex* successor);
    void release_continuation();
    
    continuation_vertex* get_continuation_vertex() {
        continuation_vertex* current_continuation_vertex = m_continuation_vertex.load(std::memory_order_acquire);

        if (current_continuation_vertex == nullptr) {
            d1::small_object_allocator alloc;
            continuation_vertex* new_continuation_vertex = alloc.new_object<continuation_vertex>(m_task, alloc);

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

    static bool is_alive(successor_list_node* node) {
        return node != reinterpret_cast<successor_list_node*>(~std::uintptr_t(0));
    }

    successor_list_node* fetch_successor_list() {
        return m_successor_list_head.exchange(reinterpret_cast<successor_list_node*>(~std::uintptr_t(0)));
    }

private:
    task_handle_task* m_task;
    std::atomic<successor_list_node*> m_successor_list_head;
    std::atomic<continuation_vertex*> m_continuation_vertex;
    std::atomic<std::size_t> m_num_references;
    d1::small_object_allocator m_allocator;
};

inline void internal_make_edge(task_dynamic_state* pred, task_dynamic_state* succ) {
    __TBB_ASSERT(pred != nullptr && succ != nullptr , nullptr);
    pred->add_successor(succ->get_continuation_vertex());
}
#endif // __TBB_PREVIEW_TASK_GROUP_EXTENSIONS

class task_handle_task : public d1::task {
    std::uint64_t m_version_and_traits{};
    d1::wait_tree_vertex_interface* m_wait_tree_vertex;
    d1::task_group_context& m_ctx;
    d1::small_object_allocator m_allocator;
#if __TBB_PREVIEW_TASK_GROUP_EXTENSIONS
    std::atomic<task_dynamic_state*> m_dynamic_state;
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
        : m_wait_tree_vertex(vertex)
        , m_ctx(ctx)
        , m_allocator(alloc)
#if __TBB_PREVIEW_TASK_GROUP_EXTENSIONS
        , m_dynamic_state(nullptr)
#endif
    {
        suppress_unused_warning(m_version_and_traits);
        m_wait_tree_vertex->reserve();
    }

    ~task_handle_task() override {
        m_wait_tree_vertex->release();
#if __TBB_PREVIEW_TASK_GROUP_EXTENSIONS
        task_dynamic_state* current_state = m_dynamic_state.load(std::memory_order_relaxed);
        if (current_state != nullptr) {
            current_state->release();
        }
#endif
    }

    d1::task_group_context& ctx() const { return m_ctx; }

#if __TBB_PREVIEW_TASK_GROUP_EXTENSIONS
    // Initializes the dynamic state if:
    // * the task_tracker object was created
    // * first dependency was added to a task_handle
    // * Successors were transferred to the current task
    task_dynamic_state* get_dynamic_state() {
        task_dynamic_state* current_state = m_dynamic_state.load(std::memory_order_acquire);

        if (current_state == nullptr) {
            d1::small_object_allocator alloc;

            task_dynamic_state* new_state = alloc.new_object<task_dynamic_state>(this, alloc);

            if (m_dynamic_state.compare_exchange_strong(current_state, new_state)) {
                current_state = new_state;
            } else {
                // Other thread created the dynamic state
                alloc.delete_object(new_state);
            }
        }

        __TBB_ASSERT(current_state != nullptr, "Failed to create dynamic state");
        return current_state;
    }

    task_handle_task* complete_task() {
        task_handle_task* next_task = nullptr;

        task_dynamic_state* current_state = m_dynamic_state.load(std::memory_order_relaxed);
        if (current_state != nullptr) {
            next_task = current_state->complete_task();
        }
        return next_task;
    }

    void release_continuation() {
        task_dynamic_state* current_state = m_dynamic_state.load(std::memory_order_relaxed);
        if (current_state != nullptr && current_state->has_dependencies()) {
            current_state->release_continuation();
        }
    }

    bool has_dependencies() const {
        task_dynamic_state* current_state = m_dynamic_state.load(std::memory_order_relaxed);
        return current_state ? current_state->has_dependencies() : false;
    }
#endif
};

#if __TBB_PREVIEW_TASK_GROUP_EXTENSIONS
class continuation_vertex : public d1::reference_vertex {
public:
    continuation_vertex(task_handle_task* task, d1::small_object_allocator& alloc)
        // Reserving 1 for task_handle that owns the task for which the predecessors are added
        // reference counter would be released when this task_handle would be submitted for execution
        : d1::reference_vertex(nullptr, 1)
        , m_task(task)
        , m_allocator(alloc)
    {}

    task_handle_task* release_bypass(std::uint32_t delta = 1) {
        task_handle_task* next_task = nullptr;

        std::uint64_t ref = m_ref_count.fetch_sub(static_cast<std::uint64_t>(delta)) - static_cast<uint64_t>(delta);

        if (ref == 0) {
            // TODO: can we skip this step since the task would be spawned anyway ?
            m_task->get_dynamic_state()->unset_dependency();
            next_task = m_task;
            m_allocator.delete_object(this);
        }
        return next_task;
    }
private:
    task_handle_task* m_task;
    d1::small_object_allocator m_allocator;
}; // class continuation_vertex
#endif

class task_handle {
    struct task_handle_task_finalizer_t{
        void operator()(task_handle_task* p){ p->finalize(); }
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

    task_handle(task_handle_task* t) : m_handle {t}{}

    d1::task* release() {
       return m_handle.release();
    }
};

struct task_handle_accessor {
    static task_handle construct(task_handle_task* t) { return {t}; }

    static d1::task* release(task_handle& th) {
#if __TBB_PREVIEW_TASK_GROUP_EXTENSIONS
        th.m_handle->release_continuation();
#endif
        return th.release();
}

    static d1::task_group_context& ctx_of(task_handle& th) {
        __TBB_ASSERT(th.m_handle, "ctx_of does not expect empty task_handle.");
        return th.m_handle->ctx();
    }

#if __TBB_PREVIEW_TASK_GROUP_EXTENSIONS
    static task_dynamic_state* get_task_dynamic_state(task_handle& th) {
        return th.m_handle->get_dynamic_state();
    }

    static bool has_dependencies(task_handle& th) {
        __TBB_ASSERT(th.m_handle, "has_dependency does not expect empty task_handle");
        return th.m_handle->has_dependencies();
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
inline task_handle_task* release_successor_list(successor_list_node* node) {
    task_handle_task* next_task = nullptr;

    while (node != nullptr) {
        successor_list_node* next_node = node->get_next_node();
        task_handle_task* successor_task = node->get_continuation_vertex()->release_bypass();
        node->finalize();
        node = next_node;

        if (successor_task) {
            if (next_task == nullptr) {
                next_task = successor_task;
            } else {
                d1::spawn(*successor_task, successor_task->ctx());
            }
        }
    }
    return next_task;
}

inline void task_dynamic_state::add_successor(continuation_vertex* successor) {
    __TBB_ASSERT(successor != nullptr, nullptr);
    successor_list_node* current_successor_list_head = m_successor_list_head.load(std::memory_order_acquire);

    if (is_alive(current_successor_list_head)) {
        successor->reserve();

        d1::small_object_allocator alloc;
        successor_list_node* new_successor_node = alloc.new_object<successor_list_node>(successor, alloc);
        new_successor_node->set_next_node(current_successor_list_head);

        while (!m_successor_list_head.compare_exchange_strong(current_successor_list_head, new_successor_node)) {
            // Other thread updated the head of the list
            if (!is_alive(current_successor_list_head)) {
                // Current task has completed while we tried to insert the successor to the list
                new_successor_node->finalize();
                successor->release();
                break;
            }
            new_successor_node->set_next_node(current_successor_list_head);
        }
    }
}

inline void task_dynamic_state::release_continuation() {
    continuation_vertex* current_vertex = m_continuation_vertex.load(std::memory_order_acquire);
    __TBB_ASSERT(current_vertex != nullptr, "release_continuation requested for task without dependencies");
    task_handle_task* task = current_vertex->release_bypass();
    
    // Dependent tasks have completed before the task_handle holding the continuation was submitted for execution
    // task_handle was the last owner of the taskand it should be spawned
    if (task != nullptr) {
        d1::spawn(*task, task->ctx());
    }
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

    task_tracker(const task_handle& th)
        : m_task_state(th ? th.m_handle->get_dynamic_state() : nullptr)
    {
        // Register new co-owner of the dynamic state
        if (m_task_state) m_task_state->reserve();
    }

    ~task_tracker() {
        if (m_task_state) m_task_state->release();
    }

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

    task_tracker& operator=(const task_handle& th) {
        // Release co-ownership on the previously tracked dynamic state
        if (m_task_state) m_task_state->release();

        if (th) {
            m_task_state = th.m_handle->get_dynamic_state();

            // Reserve co-ownership on the new dynamic state
            __TBB_ASSERT(m_task_state != nullptr, "No state in the non-empty task_handle");
            m_task_state->reserve();
        } else {
            m_task_state = nullptr;
        }
        return *this;
    }

    explicit operator bool() const noexcept { return m_task_state != nullptr; }
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
