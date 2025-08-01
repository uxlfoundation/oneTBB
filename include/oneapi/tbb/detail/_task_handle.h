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

class task_dynamic_state {
public:
    task_dynamic_state(d1::small_object_allocator& alloc)
        : m_num_references(1) // reserves a task co-ownership for dynamic state
        , m_allocator(alloc)
    {}

    void reserve() { ++m_num_references; }

    void release() {
        if (--m_num_references == 0) {
            m_allocator.delete_object(this);
        }
    }

    void complete_task() {
    }
private:
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
    // Initializes the dynamic state if:
    // * the task_completion_handle object was created
    // * first dependency was added to a task_handle
    // * Successors were transferred to the current task
    task_dynamic_state* get_dynamic_state() {
        task_dynamic_state* current_state = m_dynamic_state.load(std::memory_order_acquire);

        if (current_state == nullptr) {
            d1::small_object_allocator alloc;

            task_dynamic_state* new_state = alloc.new_object<task_dynamic_state>(alloc);

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

    void complete_task() {
        task_dynamic_state* current_state = m_dynamic_state.load(std::memory_order_relaxed);
        if (current_state != nullptr) {
            current_state->complete_task();
        }
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

    static d1::task* release(task_handle& th) { return th.release(); }

    static d1::task_group_context& ctx_of(task_handle& th) {
        __TBB_ASSERT(th.m_handle, "ctx_of does not expect empty task_handle.");
        return th.m_handle->ctx();
    }
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
        : m_task_state(th.m_handle->get_dynamic_state())
    {
        __TBB_ASSERT(th, "Construction of task_completion_handle from an empty task_handle");
        // Register new co-owner of the dynamic state
        m_task_state->reserve();
    }

    ~task_completion_handle() {
        if (m_task_state) m_task_state->release();
    }

    task_completion_handle& operator=(const task_completion_handle& other) {
        if (this != &other) {
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
        // Release co-ownership on the previously tracked dynamic state
        if (m_task_state) m_task_state->release();

        m_task_state = th.m_handle->get_dynamic_state();

        // Reserve co-ownership on the new dynamic state
        __TBB_ASSERT(m_task_state != nullptr, "No state in the non-empty task_handle");
        m_task_state->reserve();
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

    task_dynamic_state* m_task_state;
};
#endif

} // namespace d2
} // namespace detail
} // namespace tbb

#endif /* __TBB_task_handle_H */
