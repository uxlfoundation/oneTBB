/*
   Copyright (c) 2025 Intel Corporation

   SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

#ifndef __TCM_TEST_THREAD_POOL_HEADER
#define __TCM_TEST_THREAD_POOL_HEADER

#include "common_tests.h"

#include "tcm/detail/_tcm_assert.h"
#include "tcm.h"

#include <atomic>
#include <mutex>
#include <deque>
#include <future>
#include <thread>
#include <memory>
#include <functional>

// TODO: Add support for CPU constraints
class client_thread_pool {
public:
    template <typename Func>
    void parallel_for(int start, int end, const Func & f, const tcm_permit_t &expected_permit) {
        uint32_t concurrency;
        tcm_permit_t permit = make_void_permit(&concurrency);
        request_permit(permit, expected_permit);
        thread_pool_cv.notify_all();

        // parallel_for preparation
        int granted_concurrency = get_permit_concurrency(permit);
        int work_size = end - start;
        int base_task_size = std::max(1, work_size / granted_concurrency);
        int task_size_remainder = std::max(0, work_size - granted_concurrency * base_task_size);
        int s = start + base_task_size;

        // Submit work to workers
        std::vector<std::future<void>> task_futures;
        task_futures.reserve(granted_concurrency);
        for (int id = 1; id < granted_concurrency && s != end; ++id) {
            int task_size = task_size_remainder-- > 0 ? base_task_size + 1 : base_task_size;
            int e = s + task_size;
            task_futures.emplace_back(enqueue(f, s, e));
            s = e;
        }
        // External thread joins
        register_thread();
        f(start, start + base_task_size);
        wait(task_futures);
        unregister_thread();

        deactivate_permit();
    }

    template<typename F, typename... Args>
    std::future<void> enqueue(const F& func, Args&&... args) {
        task_t task{std::bind(func, std::forward<Args>(args)...)};
        auto future = task.get_future();
        {
            std::lock_guard<std::mutex> lock(task_deque_mutex);
            tasks.push_back(std::move(task));
        }
        task_deque_cv.notify_all();
        return future;
    }

    client_thread_pool(std::string rname, uint32_t min_threads, uint32_t max_threads)
        : runtime_name(rname), min_concurrency(min_threads), max_concurrency(max_threads) 
    {
        client = connect_new_client(client_renegotiate, "", "tcmConnect " + runtime_name);
        initialize_thread_pool();
    }

    ~client_thread_pool() {
        {
            std::lock_guard<std::mutex> task_lock{task_deque_mutex};
            is_execution_canceled = true;
        }
        {
            std::lock_guard<std::mutex> join_lock(thread_pool_mutex);
            max_joinable = -1;
        }
        thread_pool_cv.notify_all();
        task_deque_cv.notify_all();
        release_permit(ph, "", "tcmReleasePermit " + runtime_name);
        for (auto &worker : workers) {
            worker.join();
            g_num_created_threads -= 1;
        }
        disconnect_client(client, "", "tcmDisconnect " + runtime_name);
    }

private:
    using task_t = std::packaged_task<void()>;
    void wait(std::vector<std::future<void>>& futures) {
        for (auto&& future : futures) {
            future.get();
        }
        std::lock_guard<std::mutex> lock(exception_mutex);
        if (pool_exception) {
            std::rethrow_exception(pool_exception);
        }
    }

    void request_permit(tcm_permit_t &permit, const tcm_permit_t &expected_permit) {
        tcm_permit_request_t request = TCM_PERMIT_REQUEST_INITIALIZER;
        request.min_sw_threads = min_concurrency;
        request.max_sw_threads = max_concurrency;
        auto r = tcmRequestPermit(client, request, &ph, &ph, &permit);
        if (!(check_success(r, "tcmRequestPermit " + runtime_name) && check_permit(expected_permit, permit))) {
            throw tcm_request_permit_error{};
        }
        if (permit.state == TCM_PERMIT_STATE_PENDING) {
            while (permit.state == TCM_PERMIT_STATE_PENDING) {
                std::this_thread::yield();
                get_permit_data(ph, permit, "", "tcmGetPermitData for ph=" + to_string(ph) + " by "
                                                + runtime_name);
            }
        }
        std::lock_guard<std::mutex> join_lock(thread_pool_mutex);
        max_joinable = get_permit_concurrency(permit)-1;
    }

    void deactivate_permit() {
        ::deactivate_permit(ph, "" ,"tcmDeactivatePermit " + runtime_name);
        std::lock_guard<std::mutex> join_lock(thread_pool_mutex);
        max_joinable = 0;
    }

    void register_thread() {
        ::register_thread(ph, "", "tcmRegisterThread " + runtime_name);
    }

    void unregister_thread() {
        ::unregister_thread("", "tcmUnregisterThread " + runtime_name);
    }

    enum pool_state {thread_exit, thread_continue, thread_join};
    pool_state try_join_thread_pool() {
        std::unique_lock<std::mutex> join_lock(thread_pool_mutex);
        thread_pool_cv.wait(join_lock, [this]
                            { return max_joinable == -1 || joined_threads < max_joinable; });
        if (max_joinable == -1) {
            return pool_state::thread_exit;
        }
        if (joined_threads >= max_joinable) {
            return pool_state::thread_continue;
        }
        joined_threads += 1;
        return thread_join;
    }

    void exit_thread_pool() {
        std::lock_guard<std::mutex> join_lock(thread_pool_mutex);
        joined_threads -= 1;
    }

    bool receive_task(task_t& task) {
        std::unique_lock<std::mutex> lock{task_deque_mutex};
        task_deque_cv.wait_for(lock, std::chrono::milliseconds{200} , [this]
                    { return !tasks.empty() || is_execution_canceled; });
        if (is_execution_canceled || tasks.empty()) {
            return false;
        }
        task = std::move(tasks.back());
        tasks.pop_back();
        return true;
    }

    bool need_to_leave() {
        std::lock_guard<std::mutex> join_lock(thread_pool_mutex);
        return max_joinable == -1;
    }

    void initialize_thread_pool() {
        std::call_once(thread_pool_initilized, [this] {
            auto thread_routine = [this] {
                while (true) {
                    try {
                        pool_state state = try_join_thread_pool();
                        if (state == pool_state::thread_exit) {
                            return;
                        } else if (state == pool_state::thread_continue) {
                            continue;
                        }
                        register_thread();
                        // Task execution loop
                        while (true) {
                            task_t task;
                            if (receive_task(task)) {
                                task();
                            } else {
                                break;
                            }
                        }
                        unregister_thread();
                        exit_thread_pool();
                        if (need_to_leave()) {
                            return;
                        }
                    }
                    catch (...) {
                        {
                            std::lock_guard<std::mutex> exception_lock(exception_mutex);
                            if (!pool_exception) {
                                pool_exception = std::current_exception();
                            }
                        }
                        {
                            std::lock_guard<std::mutex> join_lock(thread_pool_mutex);
                            max_joinable = -1;
                        }
                        {
                            std::lock_guard<std::mutex> task_lock{task_deque_mutex};
                            is_execution_canceled = true;
                        }
                        thread_pool_cv.notify_all();
                        task_deque_cv.notify_all();
                        return;
                    }
                }
            };

            for (uint32_t i = 0;
                i < max_concurrency - 1 && g_num_created_threads < g_max_threads;
                ++i)
            {
                if (g_num_created_threads.fetch_add(1) >= g_max_threads) {
                    g_num_created_threads -= 1;
                }
                workers.emplace_back(thread_routine);
            }
        });
    }
    // Auxiliary
    std::string runtime_name;
    // Details for permit request
    uint32_t min_concurrency;
    uint32_t max_concurrency;
    // Thread pool's internals
    std::once_flag thread_pool_initilized;
    std::vector<std::thread> workers;
    std::mutex thread_pool_mutex;
    std::condition_variable thread_pool_cv;
    int max_joinable{};
    int joined_threads{};
    bool is_execution_canceled{false};
    // Tasking internals
    std::deque<task_t> tasks;
    std::condition_variable task_deque_cv;
    std::mutex task_deque_mutex;
    std::mutex exception_mutex;
    std::exception_ptr pool_exception;
    // TCM related internals
    tcm_client_id_t client{};
    tcm_permit_handle_t ph{nullptr};
    static std::atomic_int g_num_created_threads;
    static constexpr int g_max_threads = 256;
};

std::atomic_int client_thread_pool::g_num_created_threads{0};

#endif // __TCM_TEST_THREAD_POOL_HEADER
