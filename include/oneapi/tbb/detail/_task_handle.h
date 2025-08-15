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
class task_dynamic_state;

class successor_list_node {
public:
    successor_list_node(task_dynamic_state* successor_state, d1::small_object_allocator& alloc)
        : m_next_node(nullptr)
        , m_successor_state(successor_state)
        , m_allocator(alloc)
    {}

    task_dynamic_state* get_successor_state() const {
        return m_successor_state;
    }

    successor_list_node* get_next_node() const {
        return m_next_node;
    }

    void set_next_node(successor_list_node* next_node) {
        m_next_node = next_node;
    }

    void finalize() {
        m_allocator.delete_object(this);
    }
private:
    successor_list_node* m_next_node;
    task_dynamic_state* m_successor_state;
    d1::small_object_allocator m_allocator;
};

class task_dynamic_state {
public:
    task_dynamic_state(task_handle_task* task, d1::small_object_allocator& alloc)
        : m_task(task)
        , m_successor_list_head(nullptr)
        , m_num_dependencies(0)
        , m_num_references(1) // reserves a task co-ownership for dynamic state
        , m_allocator(alloc)
    {}

    void reserve() { ++m_num_references; }

    void release() {
        if (--m_num_references == 0) {
            m_allocator.delete_object(this);
        }
    }

    void register_dependency() {
        if (m_num_dependencies++ == 0) {
            // Register an additional dependency for a task_handle owning the current task
            ++m_num_dependencies;
        }
    }

    bool release_dependency() {
        auto updated_dependency_counter = --m_num_dependencies;
        return updated_dependency_counter == 0;
    }

    task_handle_task* complete_and_try_bypass_successor();

    bool has_dependencies() const {
        return m_num_dependencies.load(std::memory_order_acquire) != 0;
    }

    void add_successor(task_dynamic_state* successor);
    void add_successor_node(successor_list_node* new_successor_node, successor_list_node* current_successor_list_head);

    using successor_list_state_flag = std::uintptr_t;
    static constexpr successor_list_state_flag COMPLETED_FLAG = ~std::uintptr_t(0);

    static bool represents_completed_task(successor_list_node* list_head) {
        return list_head == reinterpret_cast<successor_list_node*>(COMPLETED_FLAG);
    }

    successor_list_node* fetch_successor_list(successor_list_state_flag new_list_state_flag) {
        return m_successor_list_head.exchange(reinterpret_cast<successor_list_node*>(new_list_state_flag));
    }

    task_handle_task* get_task() { return m_task; }

private:
    task_handle_task* m_task;
    std::atomic<successor_list_node*> m_successor_list_head;
    std::atomic<std::size_t> m_num_dependencies;
    std::atomic<std::size_t> m_num_references;
    d1::small_object_allocator m_allocator;
};
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
    // Returns the dynamic state associated with the task. If the state has not been initialized, initializes it.
    task_dynamic_state* get_dynamic_state() {
        task_dynamic_state* current_state = m_dynamic_state.load(std::memory_order_acquire);

        if (current_state == nullptr) {
            d1::small_object_allocator alloc;

            task_dynamic_state* new_state = alloc.new_object<task_dynamic_state>(this, alloc);

            if (m_dynamic_state.compare_exchange_strong(current_state, new_state)) {
                current_state = new_state;
            } else {
                // CAS failed, current_state points to the dynamic state created by another thread
                alloc.delete_object(new_state);
            }
        }

        __TBB_ASSERT(current_state != nullptr, "Failed to create dynamic state");
        return current_state;
    }

    task_handle_task* complete_and_try_bypass_successor() {
        task_handle_task* next_task = nullptr;

        task_dynamic_state* current_state = m_dynamic_state.load(std::memory_order_relaxed);
        if (current_state != nullptr) {
            next_task = current_state->complete_and_try_bypass_successor();
        }
        return next_task;
    }

    bool release_dependency() {
        task_dynamic_state* current_state = m_dynamic_state.load(std::memory_order_relaxed);
        __TBB_ASSERT(current_state != nullptr && current_state->has_dependencies(),
                     "release_dependency was called for task without dependencies");
        return current_state->release_dependency();
    }

    bool has_dependencies() const {
        task_dynamic_state* current_state = m_dynamic_state.load(std::memory_order_relaxed);
        return current_state ? current_state->has_dependencies() : false;
    }
#endif
};

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
    friend class task_completion_handle;
#endif

    task_handle(task_handle_task* t) : m_handle {t}{}

    d1::task* release() {
       return m_handle.release();
    }
};

struct task_handle_accessor {
    static task_handle construct(task_handle_task* t) { return {t}; }

    static task_handle_task* release(task_handle& th) {
        return th.m_handle.release();
    }

    static d1::task_group_context& ctx_of(task_handle& th) {
        __TBB_ASSERT(th.m_handle, "ctx_of does not expect empty task_handle.");
        return th.m_handle->ctx();
    }

#if __TBB_PREVIEW_TASK_GROUP_EXTENSIONS
    static task_dynamic_state* get_task_dynamic_state(task_handle& th) {
        return th.m_handle->get_dynamic_state();
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
inline void task_dynamic_state::add_successor_node(successor_list_node* new_successor_node,
                                                   successor_list_node* current_successor_list_head)
{
    __TBB_ASSERT(new_successor_node != nullptr, nullptr);

    new_successor_node->set_next_node(current_successor_list_head);

    while (!m_successor_list_head.compare_exchange_strong(current_successor_list_head, new_successor_node)) {
        // Other thread updated the head of the list

        if (represents_completed_task(current_successor_list_head)) {
            // Current task has completed while we tried to insert the successor to the list
            new_successor_node->get_successor_state()->release_dependency();
            new_successor_node->finalize();
            break;
        }

        new_successor_node->set_next_node(current_successor_list_head);
    }
}

inline void task_dynamic_state::add_successor(task_dynamic_state* successor) {
    __TBB_ASSERT(successor != nullptr, nullptr);
    successor_list_node* current_successor_list_head = m_successor_list_head.load(std::memory_order_acquire);

    if (!represents_completed_task(current_successor_list_head)) {
        successor->register_dependency();

        d1::small_object_allocator alloc;
        successor_list_node* new_successor_node = alloc.new_object<successor_list_node>(successor, alloc);
        add_successor_node(new_successor_node, current_successor_list_head);
    }
}

inline task_handle_task* task_dynamic_state::complete_and_try_bypass_successor() {
    successor_list_node* node = fetch_successor_list(COMPLETED_FLAG);

    task_handle_task* next_task = nullptr;

    while (node != nullptr) {
        task_dynamic_state* successor_state = node->get_successor_state();

        if (successor_state->release_dependency()) {
            task_handle_task* successor_task = successor_state->get_task();
            if (next_task == nullptr) {
                next_task = successor_task;
            } else {
                d1::spawn(*successor_task, successor_task->ctx());
            }
        }

        successor_list_node* next_node = node->get_next_node();
        node->finalize();
        node = next_node;
    }

    return next_task;
}

class task_completion_handle {
public:
    task_completion_handle() : m_task_state(nullptr) {}

    task_completion_handle(const task_completion_handle& other) 
        : m_task_state(other.m_task_state)
    {
        // Register one more co-owner of the dynamic state
        if (m_task_state) m_task_state->reserve();
    }
    task_completion_handle(task_completion_handle&& other)
        : m_task_state(other.m_task_state)
    {
        other.m_task_state = nullptr;
    }

    task_completion_handle(const task_handle& th)
        : m_task_state(nullptr)
    {
        __TBB_ASSERT(th, "Construction of task_completion_handle from an empty task_handle");
        m_task_state = th.m_handle->get_dynamic_state();
        // Register one more co-owner of the dynamic state
        m_task_state->reserve();
    }

    ~task_completion_handle() {
        if (m_task_state) m_task_state->release();
    }

    task_completion_handle& operator=(const task_completion_handle& other) {
        if (m_task_state != other.m_task_state) {
            // Release co-ownership on the previously tracked dynamic state
            if (m_task_state) m_task_state->release();

            m_task_state = other.m_task_state;

            // Register new co-owner of the new dynamic state
            if (m_task_state) m_task_state->reserve();
        }
        return *this;
    }

    task_completion_handle& operator=(task_completion_handle&& other) {
        if (this != &other) {
            // Release co-ownership on the previously tracked dynamic state
            if (m_task_state) m_task_state->release();
    
            m_task_state = other.m_task_state;
            other.m_task_state = nullptr;
        }
        return *this;
    }

    task_completion_handle& operator=(const task_handle& th) {
        __TBB_ASSERT(th, "Assignment of task_completion_state from an empty task_handle");
        task_dynamic_state* th_state = th.m_handle->get_dynamic_state();
        __TBB_ASSERT(th_state != nullptr, "No state in the non-empty task_handle");
        if (m_task_state != th_state) {
            // Release co-ownership on the previously tracked dynamic state
            if (m_task_state) m_task_state->release();

            m_task_state = th_state;

            // Reserve co-ownership on the new dynamic state
            m_task_state->reserve();
        }
        return *this;
    }

    explicit operator bool() const noexcept { return m_task_state != nullptr; }
private:
    friend bool operator==(const task_completion_handle& t, std::nullptr_t) noexcept {
        return t.m_task_state == nullptr;
    }

    friend bool operator==(const task_completion_handle& lhs, const task_completion_handle& rhs) noexcept {
        return lhs.m_task_state == rhs.m_task_state;
    }

#if !__TBB_CPP20_COMPARISONS_PRESENT
    friend bool operator==(std::nullptr_t, const task_completion_handle& t) noexcept {
        return t == nullptr;
    }

    friend bool operator!=(const task_completion_handle& t, std::nullptr_t) noexcept {
        return !(t == nullptr);
    }

    friend bool operator!=(std::nullptr_t, const task_completion_handle& t) noexcept {
        return !(t == nullptr);
    }

    friend bool operator!=(const task_completion_handle& lhs, const task_completion_handle& rhs) noexcept {
        return !(lhs == rhs);
    }
#endif // !__TBB_CPP20_COMPARISONS_PRESENT

    friend struct task_completion_handle_accessor;

    task_dynamic_state* m_task_state;
};

struct task_completion_handle_accessor {
    static task_dynamic_state* get_task_dynamic_state(task_completion_handle& tracker) {
        return tracker.m_task_state;
    }
};
#endif

} // namespace d2
} // namespace detail
} // namespace tbb

#endif /* __TBB_task_handle_H */
