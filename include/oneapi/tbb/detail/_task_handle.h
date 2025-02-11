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
#include <forward_list>
#include "../mutex.h"

namespace tbb {
namespace detail {

namespace d1 { class task_group_context; class wait_context; struct execution_data; }
namespace d2 {

class task_handle;
class task_handle_task;

class task_state_handle {
public:
    task_state_handle(d1::small_object_allocator& alloc)
        : m_is_finished(false), m_num_references(0), m_allocator(alloc) {}

    void reserve() {
        ++m_num_references;
    }

    void release() {
        if (--m_num_references == 0) {
            m_allocator.delete_object(this);
        }
    }

    // TODO: fences?
    void mark_completed() {
        m_is_finished.store(true, std::memory_order_relaxed);
    }

    bool is_completed() const {
        return m_is_finished.load(std::memory_order_relaxed);
    }
private:
    std::atomic<bool> m_is_finished;
    std::atomic<std::size_t> m_num_references;
    d1::small_object_allocator m_allocator;
};

class successor_vertex : public d1::reference_vertex {
public:
    successor_vertex(task_handle_task* task, d1::small_object_allocator& alloc)
        // TODO: add comment why 1 is necessary
        : d1::reference_vertex(nullptr, 1), m_task(task), m_allocator(alloc) {}

    task_handle_task* release_bypass(std::uint32_t delta = 1);
private:
    task_handle_task* m_task;
    d1::small_object_allocator m_allocator;
};

class task_successors_list {
public:
    task_successors_list() = default;

    void add_successor(successor_vertex* successor) {
        __TBB_ASSERT(successor, nullptr);
        successor->reserve();

        d1::mutex::scoped_lock lock(m_mutex);
        m_successor_vertexes.push_front(successor);
    }

    task_handle_task* release_successors();
private:
    std::forward_list<successor_vertex*> m_successor_vertexes;
    d1::mutex m_mutex;
    d1::small_object_allocator m_allocator;
};

class task_handle_task : public d1::task {
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
        , m_state_handle(nullptr)
        , m_ctx(ctx)
        , m_allocator(alloc)
    {
        suppress_unused_warning(m_version_and_traits);
        m_successors_list = m_allocator.new_object<task_successors_list>(); // TODO: deallocate
        m_wait_tree_vertex->reserve();
    }

    task_handle_task* complete_task_bypass() {
        task_handle_task* next_task = nullptr;

        if (m_state_handle) {
            m_state_handle->mark_completed();
            m_state_handle->release();

            if (m_successors_list) {
                next_task = m_successors_list->release_successors();
            }
        }
        return next_task;
    }

    ~task_handle_task() override {
        m_wait_tree_vertex->release();
    }

    task_state_handle* create_state_handle() {
        // m_allocator can be used since the same thread allocates task and task_handler
        m_state_handle = m_allocator.new_object<task_state_handle>(m_allocator);
        m_state_handle->reserve();
        return m_state_handle;
    }

    successor_vertex* get_continuation_vertex() {
        if (m_continuation_vertex == nullptr) {
            d1::small_object_allocator alloc;
            m_continuation_vertex = alloc.new_object<successor_vertex>(this, alloc);
        }
        return m_continuation_vertex;
    }

    void add_successor(successor_vertex* successor) {
        __TBB_ASSERT(m_successors_list, nullptr);
        m_successors_list->add_successor(successor);
    } 

    bool has_dependency() const { return m_continuation_vertex != nullptr; }

    void release_continuation() {
        __TBB_ASSERT(m_continuation_vertex, nullptr);
        m_continuation_vertex->release();
    }

    void unset_continuation() { m_continuation_vertex = nullptr; }

    d1::task_group_context& ctx() const { return m_ctx; }
private:
    std::uint64_t m_version_and_traits{};
    d1::wait_tree_vertex_interface* m_wait_tree_vertex;
    task_state_handle* m_state_handle;
    task_successors_list* m_successors_list;
    successor_vertex* m_continuation_vertex;
    d1::task_group_context& m_ctx;
    d1::small_object_allocator m_allocator;
};

inline task_handle_task* successor_vertex::release_bypass(std::uint32_t delta) {
    task_handle_task* next_task = nullptr;

    std::uint32_t ref = m_ref_count.fetch_sub(static_cast<std::uint64_t>(delta)) - static_cast<std::uint64_t>(delta);
    if (ref == 0) {
        m_task->unset_continuation();
        next_task = m_task;
        m_allocator.delete_object(this);
    }
    return next_task;
}
inline task_handle_task* task_successors_list::release_successors() {
    d1::mutex::scoped_lock lock(m_mutex);
    task_handle_task* next_task = nullptr;

    for (auto& vertex : m_successor_vertexes) {
        task_handle_task* task = vertex->release_bypass();
        
        if (next_task == nullptr) {
            next_task = task;
        } else {
            d1::spawn(*task, task->ctx());
        }
    }
    return next_task;
}

class task_handle {
    struct task_handle_task_finalizer_t {
        void operator()(task_handle_task* p){ p->finalize(); }
    };
    
    struct task_state_handle_finalizer_t {
        void operator()(task_state_handle* handle) { handle->release(); }
    };
    using handle_impl_t = std::unique_ptr<task_handle_task, task_handle_task_finalizer_t>;
    using task_state_handle_impl_t = std::unique_ptr<task_state_handle, task_state_handle_finalizer_t>;

    handle_impl_t m_handle = {nullptr};
    task_state_handle_impl_t m_state_handle = {nullptr};
public:
    task_handle() = default;
    task_handle(task_handle&&) = default;
    task_handle& operator=(task_handle&&) = default;

    explicit operator bool() const noexcept { return static_cast<bool>(m_handle); }

    friend bool operator==(task_handle const& th, std::nullptr_t) noexcept;
    friend bool operator==(std::nullptr_t, task_handle const& th) noexcept;

    friend bool operator!=(task_handle const& th, std::nullptr_t) noexcept;
    friend bool operator!=(std::nullptr_t, task_handle const& th) noexcept;

    bool is_completed() const {
        return m_state_handle ? m_state_handle->is_completed() : false;
    }

    void add_successor(task_handle& successor) {
        // TODO: clear behavior in case of empty this and successor
        if (m_handle && successor.m_handle) {
            __TBB_ASSERT(m_state_handle, nullptr);
            if (!m_state_handle->is_completed()) {
                m_handle->add_successor(successor.m_handle->get_continuation_vertex());
            }
        }
    }

private:
    friend struct task_handle_accessor;

    task_handle(task_handle_task* t)
        : m_handle{t}, m_state_handle(t->create_state_handle())
    {
        m_state_handle->reserve();
    }

    d1::task* release() {
       return m_handle.release();
    }
};

struct task_handle_accessor {
    static task_handle construct(task_handle_task* t)  { return {t}; }
    static d1::task* release(task_handle& th) { return th.release(); }

    static d1::task_group_context& ctx_of(task_handle& th) {
        __TBB_ASSERT(th.m_handle, "ctx_of does not expect empty task_handle.");
        return th.m_handle->ctx();
    }

    static bool has_dependency(task_handle& th) {
        __TBB_ASSERT(th.m_handle, "has_dependency does not expect empty task_handle.");
        return th.m_handle->has_dependency();
    }

    static void release_continuation(task_handle& th) {
        __TBB_ASSERT(th.m_handle, "release_continuation does not expect empty task_handle.");
        th.m_handle->release_continuation();
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

} // namespace d2
} // namespace detail
} // namespace tbb

#endif /* __TBB_task_handle_H */
