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
class task_dynamic_state {
    enum class task_status {
        no_status = 0,
        submitted = 1,
        completed = 2
    };
public:
    task_dynamic_state(d1::small_object_allocator& alloc)
        : m_task_status(task_status::no_status)
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

    void complete_task() {
        m_task_status.store(task_status::completed, std::memory_order_release);   
    }

    bool was_submitted() const {
        return m_task_status.load(std::memory_order_acquire) >= task_status::submitted;
    }

    bool is_completed() const {
        return m_task_status.load(std::memory_order_acquire) == task_status::completed;
    }
private:
    std::atomic<task_status> m_task_status;
    std::atomic<std::size_t> m_num_references;
    d1::small_object_allocator m_allocator;
};

class task_with_dynamic_state : public d1::task {
public:
    // TODO: should m_state be lazy-initialized while creating task_tracker or adding dependencies?
    // Called while constructing the function_stack_task
    task_with_dynamic_state()
        : m_state(m_allocator.new_object<task_dynamic_state>(m_allocator))
    {
        __TBB_ASSERT(m_state != nullptr, "Failed to create task_dynamic_state");
        m_state->reserve();
    }

    task_with_dynamic_state(d1::small_object_allocator& alloc)
        : m_allocator(alloc)
        , m_state(m_allocator.new_object<task_dynamic_state>(m_allocator))
    {
        __TBB_ASSERT(m_state != nullptr, "Failed to create task_dynamic_state");
        m_state->reserve();
    }

    virtual ~task_with_dynamic_state() {
        __TBB_ASSERT(m_state != nullptr, nullptr);
        m_state->release();
    }

    task_dynamic_state* get_dynamic_state() {
        return m_state;
    }
protected:
    d1::small_object_allocator m_allocator;
private:
    task_dynamic_state* m_state;
};
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
    d1::small_object_allocator m_allocator;
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
        , m_allocator(alloc)
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

    task_handle(task_handle_task* t) : m_handle {t} {}

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
        th.m_handle->get_dynamic_state()->mark_submitted();
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

    task_dynamic_state* m_task_state;
};
#endif

} // namespace d2
} // namespace detail
} // namespace tbb

#endif /* __TBB_task_handle_H */
