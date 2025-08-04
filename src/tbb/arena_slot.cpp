/*
    Copyright (c) 2005-2025 Intel Corporation
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

#include "arena_slot.h"
#include "arena.h"
#include "thread_data.h"

namespace tbb {
namespace detail {
namespace r1 {

//------------------------------------------------------------------------
// Arena Slot
//------------------------------------------------------------------------

d1::task* arena_slot::get_task(execution_data_ext& ed, isolation_type isolation) {
    bool all_tasks_checked = false;
    bool tasks_skipped = false;

    // A helper function to check the task isolation constraint. If not met, sets flags that indicate
    // skipped tasks. Returns the pointer to the task, or nullptr if the task cannot be executed.
    auto check_task_isolation = [&](d1::task* task_candidate) -> d1::task* {
        __TBB_ASSERT(task_candidate, nullptr);
        __TBB_ASSERT(!is_poisoned( task_candidate ), "A poisoned task cannot be processed");
        if (isolation == no_isolation || isolation == task_accessor::isolation(*task_candidate)) {
            return task_candidate;
        }
        // The task must be skipped due to isolation mismatch
        tasks_skipped = true;
        return nullptr;
    };

    __TBB_ASSERT(is_task_pool_published(), nullptr);
    accessed_by_owner.store(true, std::memory_order_relaxed);
    // The current task position in the task pool.
    std::size_t T0 = tail.load(std::memory_order_relaxed);
    // The bounds of available tasks in the task pool. H0 is only used when the head bound is reached.
    std::size_t H0 = (std::size_t)-1, T = T0;
    d1::task* result = nullptr;
    do {
        // The full fence is required to sync the store of `tail` with the load of `head` (write-read barrier)
        T = --tail;
        // The acquire load of head is required to guarantee consistency of our task pool
        // when a thief rolls back the head.
        if ( (std::intptr_t)( head.load(std::memory_order_acquire) ) > (std::intptr_t)T ) {
            acquire_task_pool();
            H0 = head.load(std::memory_order_relaxed);
            if ( (std::intptr_t)H0 > (std::intptr_t)T ) {
                // The thief has not backed off - nothing to grab.
                __TBB_ASSERT( H0 == head.load(std::memory_order_relaxed)
                    && T == tail.load(std::memory_order_relaxed)
                    && H0 == T + 1, "victim/thief arbitration algorithm failure" );
                (tasks_skipped) ? release_task_pool() : reset_task_pool_and_leave();
                all_tasks_checked = true;
                break /*do-while*/;
            } else if ( H0 == T ) {
                // There is only one task in the task pool. If it can be taken, we want to reset the pool
                if ( task_pool_ptr[T] ) {
                    result = check_task_isolation( task_pool_ptr[T] ); // may update tasks_skipped
                }
                (tasks_skipped) ? release_task_pool() : reset_task_pool_and_leave();
                all_tasks_checked = true;
            } else {
                // Release task pool if there are still some tasks.
                // After the release, the tail will be less than T, thus a thief
                // will not attempt to get a task at the position T.
                release_task_pool();
            }
        }
        // Get a task from the pool at the position T.
        __TBB_ASSERT(tail.load(std::memory_order_relaxed) <= T || is_local_task_pool_quiescent(),
                "Is it safe to get a task at position T?");
        __TBB_ASSERT( !all_tasks_checked || H0 == T, nullptr );
        if ( task_pool_ptr[T] ) {
            if (!all_tasks_checked) {
                result = check_task_isolation( task_pool_ptr[T] ); // may update tasks_skipped
            }
            if ( result ) {
                // Isolation matches; check if there is a real task
                result = task_proxy::try_extract_task_from( result, ed );
                // If some tasks were skipped, mark the position as a hole, otherwise poison it.
                if ( tasks_skipped ) {
                    task_pool_ptr[T] = nullptr;
                } else {
                    poison_pointer( task_pool_ptr[T] );
                }

                if ( result ) break /*do-while*/;
            }
        }
        __TBB_ASSERT( !result, nullptr );
        if ( !tasks_skipped ) {
            poison_pointer( task_pool_ptr[T] );
            __TBB_ASSERT( T0 == T+1, nullptr );
            T0 = T;
        }
    } while ( /*!result &&*/ !all_tasks_checked );

    if ( tasks_skipped ) {
        __TBB_ASSERT( is_task_pool_published(), nullptr ); // the pool was not reset
        tail.store(T0, std::memory_order_release);
    }
    // At this point, skipped tasks - if any - are back in the pool bounds
    accessed_by_owner.store(false, std::memory_order_release);

    __TBB_ASSERT( (std::intptr_t)tail.load(std::memory_order_relaxed) >= 0, nullptr );
    __TBB_ASSERT( result || tasks_skipped || is_quiescent_local_task_pool_reset(), nullptr );
    return result;
}

d1::task* arena_slot::steal_task(arena& a, isolation_type isolation, std::size_t slot_index) {
    d1::task** victim_pool = lock_task_pool();
    if (!victim_pool) {
        return nullptr;
    }
    d1::task* result = nullptr;
    std::size_t H = head.load(std::memory_order_relaxed); // mirror
    std::size_t H0 = H;
    bool tasks_skipped = false;
    do {
        // The full fence is required to sync the store of `head` with the load of `tail` (write-read barrier)
        H = ++head;
        // The acquire load of tail is required to guarantee consistency of victim_pool
        // because the owner synchronizes task spawning via tail.
        if ((std::intptr_t)H > (std::intptr_t)(tail.load(std::memory_order_acquire))) {
            // Stealing attempt failed, deque contents has not been changed by us
            head.store( /*dead: H = */ H0, std::memory_order_relaxed );
            __TBB_ASSERT( !result, nullptr );
            goto unlock;
        }
        result = victim_pool[H-1];
        __TBB_ASSERT( !is_poisoned( result ), nullptr );

        if (result) {
            if (isolation == no_isolation || isolation == task_accessor::isolation(*result)) {
                if (!task_accessor::is_proxy_task(*result)) {
                    break;
                }
                task_proxy& tp = *static_cast<task_proxy*>(result);
                // If mailed task is likely to be grabbed by its destination thread, skip it.
                if (!task_proxy::is_shared(tp.task_and_tag) || !tp.outbox->recipient_is_idle() || a.mailbox(slot_index).recipient_is_idle()) {
                    break;
                }
            }
            // The task cannot be executed either due to isolation or proxy constraints.
            result = nullptr;
            tasks_skipped = true;
        } else if (!tasks_skipped) {
            // Cleanup the task pool from holes until a task is skipped.
            __TBB_ASSERT( H0 == H-1, nullptr );
            poison_pointer( victim_pool[H0] );
            H0 = H;
        }
    } while (!result);
    __TBB_ASSERT( result, nullptr );

    // emit "task was consumed" signal
    poison_pointer( victim_pool[H-1] );
    if (tasks_skipped) {
        // Some proxies in the task pool have been skipped. Set the stolen task to nullptr.
        victim_pool[H-1] = nullptr;
        // The release store synchronizes the victim_pool update(the store of nullptr).
        head.store( /*dead: H = */ H0, std::memory_order_release );
    }
unlock:
    unlock_task_pool(victim_pool);

#if __TBB_PREFETCHING
    __TBB_cl_evict(&victim_slot.head);
    __TBB_cl_evict(&victim_slot.tail);
#endif
    return result;
}

} // namespace r1
} // namespace detail
} // namespace tbb
