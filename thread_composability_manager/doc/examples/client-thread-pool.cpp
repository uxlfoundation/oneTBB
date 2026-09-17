/*
   Copyright (c) 2026 UXL Foundation Contributors

   SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

#include "tcm.h"

#include "utils.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

tcm_result_t negotiation_callback(tcm_permit_handle_t permit_handle, void* arg,
                                  tcm_callback_flags_t invocation_reason);

/* begin pool interface */
class client_thread_pool {
public:
    // Applies func to the [start, end) range, splitting it between as many
    // threads as TCM grants. May be called from several threads concurrently.
    template <typename F> void parallel_for(int start, int end, const F& func) {
        const int work_size = end - start;
        const int grant = static_cast<int>(request_permit());

        // One chunk per granted thread, the remainder spread over the first ones
        const int chunks = std::min(std::max(grant, 1), work_size);
        std::vector<std::future<void>> futures;
        futures.reserve(chunks);
        for (int i = 0, from = start; i < chunks; ++i) {
            const int size = work_size / chunks + (i < work_size % chunks);
            futures.push_back(enqueue(func, from, from + size));
            from += size;
        }
        // Wait for the work completion
        for (auto& future : futures)
            future.get();

        deactivate_permit();
    }

    client_thread_pool() {
        if (tcmConnect(negotiation_callback, &client_id) != TCM_RESULT_SUCCESS) {
            std::cerr << "tcmConnect error. Check TCM_ENABLE is set to 1\n";
            std::abort();
        }
        for (unsigned i = 0; i < std::thread::hardware_concurrency(); ++i)
            workers.emplace_back(&client_thread_pool::worker_routine, this);
    }

    ~client_thread_pool() {
        cancel_tasks();
        shut_down_pool();
        for (auto& worker : workers)
            worker.join();

        if (tcm_permit_handle_t handle = permit_handle.load())
            tcmReleasePermit(handle);
        tcmDisconnect(client_id);
    }
/* end pool interface */
/* begin permit management */
private:
    // ------------------------------- Permit -------------------------------

    // Makes sure the pool holds a permit and returns the concurrency it grants.
    // Concurrent parallel_for calls share a single permit: the first of them
    // requests it, the others only wait for it to become usable.
    uint32_t request_permit() {
        uint32_t grant = 0;
        tcm_permit_t permit = make_permit(grant);

        if (1 == ++concurrent_invocations)
            send_permit_request(permit);

        wait_until_permit_is_usable(permit);
        return grant;
    }

    void send_permit_request(tcm_permit_t& permit) {
        {
            // The pool reuses one permit, so a new request must not overlap
            // with the deactivation of the previous one
            std::unique_lock<std::mutex> permit_lock(permit_mutex);
            permit_cv.wait(permit_lock, [this]{ return !is_deactivating; });
        }

        tcm_permit_request_t request = TCM_PERMIT_REQUEST_INITIALIZER;
        request.min_sw_threads = 1;
        // TCM creates a permit for a null handle and reuses the permit the
        // handle refers to otherwise, so the pool keeps one permit for its life
        tcm_permit_handle_t handle = permit_handle.load();
        if (tcmRequestPermit(client_id, request, /*callback_arg*/this,
                             &handle, &permit) != TCM_RESULT_SUCCESS) {
            std::cerr << "tcmRequestPermit returned error\n";
            std::abort();
        }
        permit_handle.store(handle);
        notify_permit_update();
    }

    // Waits for TCM to make the permit usable and applies its grant to the pool
    void wait_until_permit_is_usable(tcm_permit_t& permit) {
        std::unique_lock<std::mutex> permit_lock(permit_mutex);
        // Count the updates seen before the permit data is read, so that an
        // update published while it is being read is not missed
        unsigned observed_updates = permit_updates;
        while (true) {
            tcm_permit_handle_t handle = permit_handle.load();
            permit_lock.unlock();

            // The permit may already be usable, for example when another thread
            // has requested it
            if (handle && read_permit_data(handle, permit))
                return;

            permit_lock.lock();
            permit_cv.wait(permit_lock, [this, observed_updates] {
                return permit_updates != observed_updates; });
            observed_updates = permit_updates;
        }
    }

    // Reads the current permit data. Returns true if the data is usable, in
    // which case the granted concurrency is applied to the pool.
    bool read_permit_data(tcm_permit_handle_t handle, tcm_permit_t& permit) {
        // Only the newest read publishes what it has read
        const unsigned epoch = ++permit_epoch;
        tcmGetPermitData(handle, &permit);
        if (permit.state == TCM_PERMIT_STATE_PENDING ||
            permit.state == TCM_PERMIT_STATE_VOID || permit.flags.stale)
            // The permit cannot be used yet: either TCM has not satisfied the
            // request, or the data was outdated by a concurrent renegotiation
            // and has to be read again.
            return false;

        // Workers register with the handle, so it must be visible to them
        // before the grant that lets them run.
        permit_handle.store(handle);
        publish_grant(*permit.concurrencies, epoch);
        return true;
    }

    void deactivate_permit() {
        {
            std::lock_guard<std::mutex> permit_lock(permit_mutex);
            is_deactivating = true;
        }
        // The last of the concurrent parallel_for calls deactivates the permit
        if (--concurrent_invocations == 0) {
            tcmDeactivatePermit(permit_handle.load());
            revoke_grant();
        }
        {
            std::lock_guard<std::mutex> permit_lock(permit_mutex);
            is_deactivating = false;
        }
        permit_cv.notify_all();
    }

    void notify_permit_update() {
        {
            std::lock_guard<std::mutex> permit_lock(permit_mutex);
            ++permit_updates;
        }
        permit_cv.notify_all();
    }
/* end permit management */

/* begin worker pool */
    // ----------------------------- Worker pool ----------------------------
    using task_t = std::packaged_task<void()>;

    // Lets 'grant' workers run tasks. The epoch check drops a grant that a
    // newer read of the permit data has already superseded.
    void publish_grant(uint32_t grant, unsigned epoch) {
        std::lock_guard<std::mutex> pool_lock(pool_mutex);
        if (allowed_threads != shutting_down && epoch == permit_epoch) {
            allowed_threads = static_cast<int>(grant);
            pool_cv.notify_all();
        }
    }

    // Without a permit the pool is not allowed to run any worker
    void revoke_grant() {
        std::lock_guard<std::mutex> pool_lock(pool_mutex);
        if (allowed_threads != shutting_down)
            allowed_threads = 0;
    }

    void shut_down_pool() {
        {
            std::lock_guard<std::mutex> pool_lock(pool_mutex);
            allowed_threads = shutting_down;
        }
        pool_cv.notify_all();
    }

    // Blocks until the permit allows one more worker to run tasks.
    // Returns false if the pool is shutting down and the worker must finish.
    bool wait_to_join_pool() {
        std::unique_lock<std::mutex> pool_lock(pool_mutex);
        pool_cv.wait(pool_lock, [this] {
            return allowed_threads == shutting_down
                || running_threads < allowed_threads; });

        if (allowed_threads == shutting_down)
            return false;

        ++running_threads;
        return true;
    }

    void leave_pool() {
        std::lock_guard<std::mutex> pool_lock(pool_mutex);
        --running_threads;
    }

    void worker_routine() {
        while (wait_to_join_pool()) {
            // A worker is known to TCM only while it runs tasks
            tcmRegisterThread(permit_handle.load());
            task_t task;
            while (get_task(task))
                task();
            tcmUnregisterThread();

            leave_pool();
        }
    }
/* end worker pool */
/* begin tasking */
    // ------------------------------- Tasking ------------------------------

    template <typename F>
    std::future<void> enqueue(const F& func, int from, int to) {
        task_t task{[func, from, to]{ func(from, to); }};
        std::future<void> future = task.get_future();
        {
            std::lock_guard<std::mutex> task_lock(task_deque_mutex);
            tasks.push_back(std::move(task));
        }
        task_deque_cv.notify_one();
        return future;
    }

    // Takes the next task. Returns false if the pool is being destroyed, or if
    // no work has appeared for a while: an idle worker leaves the pool so that
    // TCM stops counting it as running.
    bool get_task(task_t& task) {
        std::unique_lock<std::mutex> task_lock(task_deque_mutex);
        task_deque_cv.wait_for(task_lock, std::chrono::milliseconds(100),
            [this]{ return !tasks.empty() || is_canceled; });

        if (is_canceled || tasks.empty())
            return false;

        task = std::move(tasks.back());
        tasks.pop_back();
        return true;
    }

    void cancel_tasks() {
        {
            std::lock_guard<std::mutex> task_lock(task_deque_mutex);
            is_canceled = true;
        }
        task_deque_cv.notify_all();
    }
/* end tasking */

/* begin pool state */
    // Worker pool internals
    std::vector<std::thread> workers;
    std::mutex pool_mutex;
    std::condition_variable pool_cv;
    static constexpr int shutting_down = -1;
    int allowed_threads{0}; // How many workers the permit allows to run tasks
    int running_threads{0}; // How many workers do run tasks

    // Tasking internals, guarded by task_deque_mutex
    std::deque<task_t> tasks;
    std::mutex task_deque_mutex;
    std::condition_variable task_deque_cv;
    bool is_canceled{false};

    // TCM related internals
    tcm_client_id_t client_id{};
    std::atomic<tcm_permit_handle_t> permit_handle{nullptr};
    std::mutex permit_mutex;
    std::condition_variable permit_cv;
    unsigned permit_updates{0};  // Number of permit updates published so far
    bool is_deactivating{false};
    std::atomic<unsigned> concurrent_invocations{0};
    std::atomic<unsigned> permit_epoch{0};
/* end pool state */

    friend tcm_result_t negotiation_callback(
        tcm_permit_handle_t permit_handle, void* arg,
        tcm_callback_flags_t invocation_reason);
};

// TCM calls this back to tell the pool that its permit has changed
/* begin negotiation callback */
tcm_result_t negotiation_callback(tcm_permit_handle_t permit_handle, void* arg,
                                  tcm_callback_flags_t invocation_reason)
{
    client_thread_pool& pool = *static_cast<client_thread_pool*>(arg);

    if (invocation_reason.new_concurrency) {
        uint32_t grant = 0;
        tcm_permit_t permit = make_permit(grant);
        pool.read_permit_data(permit_handle, permit);
    }
    // Wake up the threads that wait for the new permit data
    pool.notify_permit_update();
    return TCM_RESULT_SUCCESS;
}
/* end negotiation callback */

std::atomic<int> threads_to_start{10};

// Simulates an application thread that runs parallel work either in the shared
// pool or in a pool of its own. All such threads start at the same time, so TCM
// has to distribute the machine between them.
void application_thread() {
    static auto empty_work = [](int /*begin*/, int /*end*/) {};

    // A pool shared by several application threads
    static client_thread_pool shared_pool;
    std::unique_ptr<client_thread_pool> private_pool{nullptr};

    const int id = --threads_to_start;

    if (id % 2 == 0)
        private_pool =
            std::unique_ptr<client_thread_pool>(new client_thread_pool);

    while (threads_to_start > 0)
        std::this_thread::yield();
    if (id % 2)
        shared_pool.parallel_for(0, 1000, empty_work);
    else
        private_pool->parallel_for(0, 1000, empty_work);
}

int main() {
    const int num_threads = threads_to_start;
    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i)
        threads.emplace_back(application_thread);

    // Nested parallelism: every chunk of the outer range is processed by a pool
    // of its own, so TCM has to compose the outer and the inner pools
    const int data_size = 10 * std::thread::hardware_concurrency();
    std::vector<int> data(data_size, 0);

    client_thread_pool outer_pool;
    outer_pool.parallel_for(0, data_size, [&data](int begin, int end) {
        client_thread_pool inner_pool;
        inner_pool.parallel_for(begin, end, [&data](int from, int to) {
            for (int i = from; i < to; ++i)
                data[i] += 1;
        });
    });

    for (auto& thread : threads)
        thread.join();

    // Every element must have been processed exactly once
    bool is_valid = std::all_of(data.begin(), data.end(),
                               [](int x){ return x == 1; });
    return is_valid ? /*success*/ 0 : /*failure*/-1;
}
