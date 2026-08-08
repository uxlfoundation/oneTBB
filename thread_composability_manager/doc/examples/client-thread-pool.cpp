/*
   Copyright (c) 2026 UXL Foundation Contributors

   SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

#include "tcm.h"

#include <algorithm>
#include <functional>
#include <future>
#include <iostream>
#include <thread>
#include <vector>
#include <deque>

tcm_result_t renegotiation_callback(tcm_permit_handle_t permit_handle, void* arg,
                                    tcm_callback_flags_t invocation_reason);
class client_thread_pool {
public:
    template <typename Func>
    void parallel_for(int start, int end, const Func & f) {
        const int grant = request_permit();
        thread_pool_cv.notify_all();

        // parallel_for preparation
        const int work_size = end - start;
        const int common_size = std::max(1, work_size / grant);
        int size_remainder = std::max(0, work_size - grant * common_size);
        int s = start + common_size;

        // Submit work to workers
        std::vector<std::future<void>> task_futures;
        task_futures.reserve(grant);
        for (int id = 1; id < grant && s != end; ++id) {
            const int subsize = size_remainder-- > 0 ? common_size + 1 : common_size;
            const int e = s + subsize;
            task_futures.emplace_back(enqueue(f, s, e));
            s = e;
        }

        // External thread joins
        tcmRegisterThread(ph);
        f(start, start + common_size);
        wait(task_futures);
        tcmUnregisterThread();

        deactivate_permit();
    }

    template<typename F, typename... Args>
    std::future<void> enqueue(const F& func, Args&&... args) {
        task_t task{std::bind(func, std::forward<Args>(args)...)};
        std::future<void> future = task.get_future();
        {
            std::lock_guard<std::mutex> lock(task_deque_mutex);
            tasks.push_back(std::move(task));
        }
        task_deque_cv.notify_one();
        return future;
    }

    client_thread_pool() {
        tcm_result_t result = tcmConnect(renegotiation_callback, &client_id);
        if (result != TCM_RESULT_SUCCESS) {
            std::cerr << "tcmConnect was unsuccessful. Check 'TCM_ENABLE' is set to 1\n";
            std::abort();
        }
        initialize_thread_pool();
    }

    ~client_thread_pool() {
        {
            std::lock_guard<std::mutex> task_lock{task_deque_mutex};
            is_execution_canceled = true;
        }
        {
            std::lock_guard<std::mutex> join_lock(thread_pool_mutex);
            max_threads = -1;
        }
        thread_pool_cv.notify_all();
        task_deque_cv.notify_all();

        tcmReleasePermit(ph);

        for (auto &worker : workers)
            worker.join();

        tcmDisconnect(client_id);
    }

private:
    using task_t = std::packaged_task<void()>;
    void wait(std::vector<std::future<void>>& futures) {
        for (auto&& future : futures)
            future.get();
    }

    uint32_t request_permit() {
        tcm_permit_t permit{};
        uint32_t grant = 0;
        permit.concurrencies = &grant;
        permit.size = 1;

        tcm_permit_request_t request = TCM_PERMIT_REQUEST_INITIALIZER;
        request.min_sw_threads = 1; // Minimum of one thread is required to perform work
        permit_changed = false;
        tcm_result_t r = tcmRequestPermit(client_id, request, /*callback_arg*/this, &ph,
                                          &permit);
        if (r != TCM_RESULT_SUCCESS) {
            std::cerr << "tcmRequestPermit returned error status: " << r << std::endl;
            std::abort();
        }

        while (permit.state == TCM_PERMIT_STATE_PENDING || permit.flags.stale) {
            // In case of requested permit oversubscribes or stale data is read, wait for
            // notification from TCM
            std::unique_lock<std::mutex> permit_lock(permit_mutex);
            permit_cv.wait(permit_lock, [this]{ return permit_changed; });

            tcmGetPermitData(ph, &permit);
            permit_changed = false;
        }
        std::lock_guard<std::mutex> join_lock(thread_pool_mutex);
        max_threads = grant - /*num external threads*/1;
        return grant;
    }

    void deactivate_permit() {
        tcmDeactivatePermit(ph);
        std::lock_guard<std::mutex> join_lock(thread_pool_mutex);
        max_threads = 0;
    }

    enum pool_state {thread_exit, thread_continue, thread_join};
    pool_state try_join_thread_pool() {
        std::unique_lock<std::mutex> join_lock(thread_pool_mutex);
        thread_pool_cv.wait(join_lock,
            [this]{ return max_threads == -1 || joined_threads < max_threads; });

        if (max_threads == -1)
            return pool_state::thread_exit;
        else if (joined_threads >= max_threads)
            return pool_state::thread_continue;

        joined_threads += 1;
        return thread_join;
    }

    void exit_thread_pool() {
        std::lock_guard<std::mutex> join_lock(thread_pool_mutex);
        joined_threads -= 1;
    }

    bool receive_task(task_t& task) {
        std::unique_lock<std::mutex> lock{task_deque_mutex};
        task_deque_cv.wait_for(lock, std::chrono::milliseconds{200},
                               [this] { return !tasks.empty() || is_execution_canceled; });
        if (is_execution_canceled || tasks.empty()) {
            return false;
        }
        task = std::move(tasks.back());
        tasks.pop_back();
        return true;
    }

    bool need_to_leave() {
        std::lock_guard<std::mutex> join_lock(thread_pool_mutex);
        return max_threads == -1;
    }

    void initialize_thread_pool() {
        std::call_once(thread_pool_initialized, [this] {
            auto thread_routine = [this] {
                while (true) {
                    pool_state state = try_join_thread_pool();
                    if (state == pool_state::thread_exit)
                        return;
                    else if (state == pool_state::thread_continue)
                        continue;

                    tcmRegisterThread(ph);
                    // Task execution loop
                    while (true) {
                        task_t task;
                        if (receive_task(task))
                            task();
                        else
                            break;
                    }
                    tcmUnregisterThread();

                    exit_thread_pool();
                    if (need_to_leave()) {
                        return;
                    }
                }
            };

            for (unsigned int i = 0; i < std::thread::hardware_concurrency() - 1; ++i)
                workers.emplace_back(thread_routine);
        });
    }

    // Thread pool internals
    std::once_flag thread_pool_initialized;
    std::vector<std::thread> workers;
    std::mutex thread_pool_mutex;
    std::condition_variable thread_pool_cv;
    int max_threads{};
    int joined_threads{};
    bool is_execution_canceled{false};

    // Tasking internals
    std::deque<task_t> tasks;
    std::condition_variable task_deque_cv;
    std::mutex task_deque_mutex;

    // TCM related internals
    tcm_client_id_t client_id{};
    tcm_permit_handle_t ph{nullptr};
    std::condition_variable permit_cv;
    std::mutex permit_mutex;
    bool permit_changed{false};
    friend tcm_result_t renegotiation_callback(tcm_permit_handle_t permit_handle, void* arg,
                                               tcm_callback_flags_t invocation_reason);
};

tcm_result_t renegotiation_callback(tcm_permit_handle_t /*ph*/, void* arg,
                                    tcm_callback_flags_t invocation_reason)
{
    if (invocation_reason.new_state) {
        client_thread_pool& myself = *(client_thread_pool*)arg;
        {
            std::lock_guard<std::mutex> lock(myself.permit_mutex);
            myself.permit_changed = true;
        }
        myself.permit_cv.notify_one();
    }

    return TCM_RESULT_SUCCESS;
}

int main() {
    const int data_size = 10 * std::thread::hardware_concurrency();
    std::vector<int> data(data_size, 0);

    client_thread_pool outer;
    outer.parallel_for(0, data_size, [&data](int begin, int end) {
        client_thread_pool inner{};
        inner.parallel_for(begin, end, [&data] (int s, int e) {
            for (int i = s; i < e; ++ i)
                data[i] += 1;
        });
    });

    bool is_valid = std::all_of(data.begin(), data.end(), [](int x){ return x == 1; });
    return is_valid ? /*success*/ 0 : /*failure*/-1;
}
