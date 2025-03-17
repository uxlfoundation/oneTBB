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
#include <iostream>

namespace tbb {
namespace detail {

namespace d1 { class task_group_context; class wait_context; struct execution_data; }
namespace d2 {

class task_handle;
class task_handle_task;
class successor_vertex;

// TODO: better name
class task_internals {
    enum class task_state {
        created = 0,
        submitted = 1,
        finished = 2
    };
public:
    task_internals(task_handle_task* task, d1::small_object_allocator& alloc)
        : m_task(task)
        , m_state(task_state::created)
        , m_successors_list_head(nullptr)
        , m_successors_list_tail(nullptr)
        , m_successor_vertex(nullptr)
        , m_num_references(0)
        , m_allocator(alloc)
    {}

    task_handle_task* get_task() const {
        return m_task;
    }

    void reserve() { ++m_num_references; }

    void release() {
        if (--m_num_references == 0) {
            m_allocator.delete_object(this);
        }
    }

    void mark_submitted() {
        m_state.store(task_state::submitted, std::memory_order_release);
    }

    bool was_submitted() {
        return m_state.load(std::memory_order_acquire) >= task_state::submitted;
    }

    bool is_completed() {
        return m_state.load(std::memory_order_acquire) == task_state::finished;
    }

    void add_successor(successor_vertex* successor);
    successor_vertex* get_successor_vertex();
    task_handle_task* complete_bypass();
    void release_successor_vertex();
    void transfer_successors_to(task_internals* target_internals);

    bool has_dependency() const { return m_successor_vertex.load(std::memory_order_acquire) != nullptr; }
    void unset_dependency() { m_successor_vertex.store(nullptr, std::memory_order_release); }

private:
    task_handle_task* m_task;
    std::atomic<task_state> m_state; // TODO: should better state tracker be used
    std::atomic<successor_vertex*> m_successors_list_head;
    successor_vertex* m_successors_list_tail;
    std::atomic<successor_vertex*> m_successor_vertex;
    std::atomic<std::size_t> m_num_references;
    d1::small_object_allocator m_allocator;
}; // class task_internals

class successor_vertex : public d1::reference_vertex {
public:
    successor_vertex(task_handle_task* task, d1::small_object_allocator& alloc)
        // TODO: comment why 1
        : d1::reference_vertex(nullptr, 1)
        , m_task(task)
        , m_next_successor(nullptr)
        , m_allocator(alloc)
    {}
    
    task_handle_task* release_bypass(std::uint32_t delta = 1);

    void set_next_successor(successor_vertex* next) {
        m_next_successor = next;
    }

    successor_vertex* get_next_successor() const {
        return m_next_successor;
    }
private:
    task_handle_task* m_task;
    successor_vertex* m_next_successor;
    d1::small_object_allocator m_allocator;
}; // class successor_vertex

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
        , m_ctx(ctx)
        , m_allocator(alloc) {
        suppress_unused_warning(m_version_and_traits);
        m_wait_tree_vertex->reserve();
    }

    ~task_handle_task() override {
        m_wait_tree_vertex->release();
    }

    d1::task_group_context& ctx() const { return m_ctx; }

    task_internals* create_internals() {
        __TBB_ASSERT(m_internals == nullptr, "Task internals already created");
        // m_allocator can be used since the same thread allocates task and internals
        m_internals = m_allocator.new_object<task_internals>(this, m_allocator);
        __TBB_ASSERT(m_internals != nullptr, "Failed to create task internals");
        m_internals->reserve();
        return m_internals;
    }

    task_handle_task* complete_bypass() {
        return m_internals ? m_internals->complete_bypass() : nullptr;
    }

    void unset_dependency() { m_internals->unset_dependency(); }

    void add_successor(task_handle_task* successor) {
        __TBB_ASSERT(m_internals != nullptr, "Task internals are not created");
        __TBB_ASSERT(!successor->m_internals->was_submitted(), "Incorrect state of successor task");
        m_internals->add_successor(successor->m_internals->get_successor_vertex());
    }

    void transfer_successors_to(task_internals* target) {
        __TBB_ASSERT(m_internals != nullptr, "Task internals are not created");
        m_internals->transfer_successors_to(target);
    }
private:
    std::uint64_t m_version_and_traits{};
    d1::wait_tree_vertex_interface* m_wait_tree_vertex;
    task_internals* m_internals;
    d1::task_group_context& m_ctx;
    d1::small_object_allocator m_allocator;
}; // class task_handle_task

inline void task_internals::add_successor(successor_vertex* successor) {
    __TBB_ASSERT(successor, nullptr);

    if (!is_completed()) {
        successor->reserve();

        successor_vertex* current_head = m_successors_list_head.load(std::memory_order_acquire);
        successor->set_next_successor(current_head);

        while (!m_successors_list_head.compare_exchange_strong(current_head, successor)) {
            if (is_completed()) {
                successor->release();
                break;
            }
            successor->set_next_successor(current_head);
        }
    }
}

inline successor_vertex* task_internals::get_successor_vertex() {
    successor_vertex* current_successor_vertex = m_successor_vertex.load(std::memory_order_acquire);

    if (current_successor_vertex == nullptr) {
        d1::small_object_allocator alloc;
        successor_vertex* new_vertex = alloc.new_object<successor_vertex>(m_task, alloc);

        if (!m_successor_vertex.compare_exchange_strong(current_successor_vertex, new_vertex)) {
            // Other thread already created a vertex
            alloc.delete_object(new_vertex);
        } else {
            // This thread has created a vertex
            current_successor_vertex = new_vertex;
        }
    }
    __TBB_ASSERT(current_successor_vertex != nullptr, nullptr);
    return current_successor_vertex;
}

inline task_handle_task* release_successors_list(successor_vertex* head) {
    task_handle_task* next_task = nullptr;

    while (head) {
        successor_vertex* next_successor = head->get_next_successor();
        task_handle_task* successor_task = head->release_bypass();

        if (successor_task) {
            if (next_task == nullptr) {
                next_task = successor_task;
            } else {
                d1::spawn(*successor_task, successor_task->ctx());
            }
        }
        head = next_successor;
    }
    return next_task;
}

inline task_handle_task* task_internals::complete_bypass() {
    m_state.store(task_state::finished, std::memory_order_release);
    task_handle_task* next_task = release_successors_list(m_successors_list_head.exchange(nullptr));

    // Releasing the task ownership
    release();
    return next_task;
}

inline void task_internals::release_successor_vertex() {
    successor_vertex* current_vertex = m_successor_vertex.load(std::memory_order_acquire);
    __TBB_ASSERT(current_vertex, nullptr);
    // Can regular release be used
    current_vertex->release_bypass();
}

inline void task_internals::transfer_successors_to(task_internals* target_internals) {
    successor_vertex* current_successors_list_head = m_successors_list_head.exchange(nullptr);
    
    if (current_successors_list_head != nullptr) {
        if (target_internals->is_completed()) {
            // Target task completed, no need to do the transfer
            release_successors_list(current_successors_list_head);
        } else {
            // Try doing the transfer

            // TODO: should tail of the list be stored?
            successor_vertex* current_successors_list_tail = current_successors_list_head;

            while (current_successors_list_tail->get_next_successor() != nullptr) {
                current_successors_list_tail = current_successors_list_tail->get_next_successor();
            }

            // successor_vertex* current_successors_list_tail = std::exchange(m_successors_list_tail, nullptr);
            successor_vertex* target_successors_list_head = target_internals->m_successors_list_head.load(std::memory_order_acquire);

            current_successors_list_tail->set_next_successor(target_successors_list_head);

            while (!target_internals->m_successors_list_head.compare_exchange_strong(target_successors_list_head, current_successors_list_head)) {
                if (target_internals->is_completed()) {
                    release_successors_list(current_successors_list_head);
                    break;
                }
                current_successors_list_tail->set_next_successor(target_successors_list_head);
            }
        }
    }
}

inline task_handle_task* successor_vertex::release_bypass(std::uint32_t delta) {
    task_handle_task* next_task = nullptr;

    std::uint32_t ref = m_ref_count.fetch_sub(static_cast<std::uint64_t>(delta)) - static_cast<std::uint64_t>(delta);
    if (ref == 0) {
        m_task->unset_dependency();
        next_task = m_task;
        m_allocator.delete_object(this);
    }
    return next_task;
}

class task_handle {
    struct task_handle_task_finalizer_t {
        void operator()(task_handle_task* p) { p->finalize(); }
    };

    struct task_internals_finalizer_t {
        void operator()(task_internals* p) { p->release(); }
    };   

    using handle_impl_t = std::unique_ptr<task_handle_task, task_handle_task_finalizer_t>;
    using task_internals_impl_t = std::unique_ptr<task_internals, task_internals_finalizer_t>;

    handle_impl_t m_handle = {nullptr};
    task_internals_impl_t m_task_internals = {nullptr};
public:
    task_handle() = default;
    task_handle(task_handle&&) = default;
    task_handle& operator=(task_handle&&) = default;

    explicit operator bool() const noexcept { return static_cast<bool>(m_handle); }

    bool was_submitted() const {
        __TBB_ASSERT(m_task_internals != nullptr, "Attempt to check state of empty task handle");
        return m_task_internals->was_submitted();
    }

    bool is_completed() const {
        __TBB_ASSERT(m_task_internals != nullptr, "Attempt to check state of empty task handle");
        return m_task_internals->is_completed();
    }

    friend bool operator==(task_handle const& th, std::nullptr_t) noexcept;
    friend bool operator==(std::nullptr_t, task_handle const& th) noexcept;

    friend bool operator!=(task_handle const& th, std::nullptr_t) noexcept;
    friend bool operator!=(std::nullptr_t, task_handle const& th) noexcept;

private:
    friend struct task_handle_accessor;

    task_handle(task_handle_task* t)
        : m_handle(t)
        , m_task_internals(t->create_internals())
    {
        // Reserve the counter for the task body
        m_task_internals->reserve();
    }

    d1::task* release() {
        m_task_internals.reset();
        return m_handle.release();
    }

    d1::task* release_task_body() {
        return m_handle.release();
    }

    void add_successor(task_handle& successor) {
        __TBB_ASSERT(m_task_internals != nullptr && successor.m_task_internals != nullptr,
                     "Attempt to set dependency for empty task handle");
        __TBB_ASSERT(!successor.m_task_internals->was_submitted(), "Attempt to set dependency for already submitted task");
        m_task_internals->add_successor(successor.m_task_internals->get_successor_vertex());
    }
};

struct task_handle_accessor {
    static task_handle construct(task_handle_task* t)  { return {t}; }
    static d1::task*   release(task_handle& th)        { return th.release(); }

    static d1::task_group_context& ctx_of(task_handle& th) {
        __TBB_ASSERT(th.m_handle, "ctx_of does not expect empty task_handle.");
        return th.m_handle->ctx();
    }

    static bool has_dependency(task_handle& th) {
        __TBB_ASSERT(th.m_handle != nullptr && th.m_task_internals, "Attempt to get dependency empty task_handle as submitted");
        return th.m_task_internals->has_dependency();
    }

    static d1::task* release_task_body(task_handle& th) {
        __TBB_ASSERT(th.m_handle, "release_task_body does not expect empty task_handle.");
        return th.release_task_body();
    }

    static void make_edge(task_handle& pred, task_handle& succ) {
        pred.add_successor(succ);
    }

    static void make_edge(task_handle_task* pred_task, task_handle& succ) {
        pred_task->add_successor(succ.m_handle.get());
    }

    static void mark_task_submitted(task_handle& th) {
        __TBB_ASSERT(th.m_handle != nullptr && th.m_task_internals, "Attempt to mark empty task_handle as submitted");
        th.m_task_internals->mark_submitted();
    }

    static void release_successor_vertex(task_handle& th) {
        __TBB_ASSERT(th.m_handle != nullptr && th.m_task_internals, "Attempt to mark empty task_handle as submitted");
        th.m_task_internals->release_successor_vertex();
    }

    static void transfer_successors(task_handle_task* task, task_handle& succ) {
        task->transfer_successors_to(succ.m_task_internals.get());
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
