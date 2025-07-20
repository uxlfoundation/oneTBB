/*
    Copyright (c) 2021-2024 Intel Corporation

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

#include "oneapi/tbb/task_group.h"
#include "oneapi/tbb/task_arena.h"
#include "common/test.h"
#include "common/utils.h"

#include "common/spin_barrier.h"

#include <type_traits>
#include "oneapi/tbb/global_control.h"


//! \file conformance_task_group.cpp
//! \brief Test for [scheduler.task_group] specification

//! Test checks that for lost task handle
//! \brief \ref requirement
TEST_CASE("Task handle created but not run"){
    {
        oneapi::tbb::task_group tg;

        //This flag is intentionally made non-atomic for Thread Sanitizer
        //to raise a flag if implementation of task_group is incorrect
        bool run {false};

        auto h = tg.defer([&]{
            run = true;
        });
        CHECK_MESSAGE(run == false, "delayed task should not be run until run(task_handle) is called");
    }
}

//! Basic test for task handle
//! \brief \ref interface \ref requirement
TEST_CASE("Task handle run"){
    oneapi::tbb::task_handle h;

    oneapi::tbb::task_group tg;
    //This flag is intentionally made non-atomic for Thread Sanitizer
    //to raise a flag if implementation of task_group is incorrect
    bool run {false};

    h = tg.defer([&]{
        run = true;
    });
    CHECK_MESSAGE(run == false, "delayed task should not be run until run(task_handle) is called");
    tg.run(std::move(h));
    tg.wait();
    CHECK_MESSAGE(run == true, "Delayed task should be completed when task_group::wait exits");

    CHECK_MESSAGE(h == nullptr, "Delayed task can be executed only once");
}

//! Basic test for task handle
//! \brief \ref interface \ref requirement
TEST_CASE("Task handle run_and_wait"){
    oneapi::tbb::task_handle h;

    oneapi::tbb::task_group tg;
    //This flag is intentionally made non-atomic for Thread Sanitizer
    //to raise a flag if implementation of task_group is incorrect
    bool run {false};

    h = tg.defer([&]{
        run = true;
    });
    CHECK_MESSAGE(run == false, "delayed task should not be run until run(task_handle) is called");
    tg.run_and_wait(std::move(h));
    CHECK_MESSAGE(run == true, "Delayed task should be completed when task_group::wait exits");

    CHECK_MESSAGE(h == nullptr, "Delayed task can be executed only once");
}

//! Test for empty check
//! \brief \ref interface
TEST_CASE("Task handle empty check"){
    oneapi::tbb::task_group tg;

    oneapi::tbb::task_handle h;

    bool empty = (h == nullptr);
    CHECK_MESSAGE(empty, "default constructed task_handle should be empty");

    h = tg.defer([]{});

    CHECK_MESSAGE(h != nullptr, "delayed task returned by task_group::delayed should not be empty");
}

//! Test for comparison operations
//! \brief \ref interface
TEST_CASE("Task handle comparison/empty checks"){
    oneapi::tbb::task_group tg;

    oneapi::tbb::task_handle h;

    bool empty =  ! static_cast<bool>(h);
    CHECK_MESSAGE(empty, "default constructed task_handle should be empty");
    CHECK_MESSAGE(h == nullptr, "default constructed task_handle should be empty");
    CHECK_MESSAGE(nullptr == h, "default constructed task_handle should be empty");

    h = tg.defer([]{});

    CHECK_MESSAGE(h != nullptr, "deferred task returned by task_group::defer() should not be empty");
    CHECK_MESSAGE(nullptr != h, "deferred task returned by task_group::defer() should not be empty");

}

//! Test for task_handle being non copyable
//! \brief \ref interface
TEST_CASE("Task handle being non copyable"){
    static_assert(
              (!std::is_copy_constructible<oneapi::tbb::task_handle>::value)
            &&(!std::is_copy_assignable<oneapi::tbb::task_handle>::value),
            "oneapi::tbb::task_handle should be non copyable");
}
//! Test that task_handle prolongs task_group::wait
//! \brief \ref requirement
TEST_CASE("Task handle blocks wait"){
    //Forbid creation of worker threads to ensure that task described by the task_handle is not run until wait is called
    oneapi::tbb::global_control s(oneapi::tbb::global_control::max_allowed_parallelism, 1);
    oneapi::tbb::task_group tg;
    //explicit task_arena is needed to prevent a deadlock,
    //as both task_group::run() and task_group::wait() should be called in the same arena
    //to guarantee execution of the task spawned by run().
    oneapi::tbb::task_arena arena;

    //This flag is intentionally made non-atomic for Thread Sanitizer
    //to raise a flag if implementation of task_group is incorrect
    bool completed  {false};
    std::atomic<bool> start_wait {false};
    std::atomic<bool> thread_started{false};

    oneapi::tbb::task_handle h = tg.defer([&]{
        completed = true;
    });

    std::thread wait_thread {[&]{
        CHECK_MESSAGE(completed == false, "Deferred task should not be run until run(task_handle) is called");

        thread_started = true;
        utils::SpinWaitUntilEq(start_wait, true);
        arena.execute([&]{
            tg.wait();
            CHECK_MESSAGE(completed == true, "Deferred task should be completed when task_group::wait exits");
        });
    }};

    utils::SpinWaitUntilEq(thread_started, true);
    CHECK_MESSAGE(completed == false, "Deferred task should not be run until run(task_handle) is called");

    arena.execute([&]{
        tg.run(std::move(h));
    });
    CHECK_MESSAGE(completed == false, "Deferred task should not be run until run(task_handle) and wait is called");
    start_wait = true;
    wait_thread.join();
}

#if TBB_USE_EXCEPTIONS
//! The test for exception handling in task_handle
//! \brief \ref requirement
TEST_CASE("Task handle exception propagation"){
    oneapi::tbb::task_group tg;

    oneapi::tbb::task_handle h = tg.defer([&]{
        volatile bool suppress_unreachable_code_warning = true;
        if (suppress_unreachable_code_warning) {
            throw std::runtime_error{ "" };
        }
    });

    tg.run(std::move(h));

    CHECK_THROWS_AS(tg.wait(), std::runtime_error);
}
#endif // TBB_USE_EXCEPTIONS

//TODO: move to some common place to share with unit tests
namespace accept_task_group_context {

struct SelfRunner {
    tbb::task_group& m_tg;
    std::atomic<unsigned>& count;
    void operator()() const {
        unsigned previous_count = count.fetch_sub(1);
        if (previous_count > 1)
            m_tg.run( *this );
    }
};

template <typename TaskGroup, typename CancelF, typename WaitF>
void run_cancellation_use_case(CancelF&& cancel, WaitF&& wait) {
    std::atomic<bool> outer_cancelled{false};
    std::atomic<unsigned> count{13};

    tbb::task_group_context inner_ctx(tbb::task_group_context::isolated);
    TaskGroup inner_tg(inner_ctx);

    tbb::task_group outer_tg;
    auto outer_tg_task = [&] {
        inner_tg.run([&] {
            utils::SpinWaitUntilEq(outer_cancelled, true);
            inner_tg.run( SelfRunner{inner_tg, count} );
        });

        utils::try_call([&] {
            std::forward<CancelF>(cancel)(outer_tg);
        }).on_completion([&] {
            outer_cancelled = true;
        });
    };

    auto check = [&] {
        tbb::task_group_status outer_status = tbb::task_group_status::not_complete;
        outer_status = std::forward<WaitF>(wait)(outer_tg);
        CHECK_MESSAGE(
            outer_status == tbb::task_group_status::canceled,
            "Outer task group should have been cancelled."
        );

        tbb::task_group_status inner_status = inner_tg.wait();
        CHECK_MESSAGE(
            inner_status == tbb::task_group_status::complete,
            "Inner task group should have completed despite the cancellation of the outer one."
        );

        CHECK_MESSAGE(0 == count, "Some of the inner group tasks were not executed.");
    };

    outer_tg.run(outer_tg_task);
    check();
}

template <typename TaskGroup>
void test() {
    run_cancellation_use_case<TaskGroup>(
        [](tbb::task_group& outer) { outer.cancel(); },
        [](tbb::task_group& outer) { return outer.wait(); }
    );

#if TBB_USE_EXCEPTIONS
    run_cancellation_use_case<TaskGroup>(
        [](tbb::task_group& /*outer*/) {
            volatile bool suppress_unreachable_code_warning = true;
            if (suppress_unreachable_code_warning) {
                throw int();
            }
        },
        [](tbb::task_group& outer) {
            try {
                outer.wait();
                return tbb::task_group_status::complete;
            } catch(const int&) {
                return tbb::task_group_status::canceled;
            }
        }
    );
#endif
}

} // namespace accept_task_group_context

//! Respect task_group_context passed from outside
//! \brief \ref interface \ref requirement
TEST_CASE("Respect task_group_context passed from outside") {
    accept_task_group_context::test<tbb::task_group>();
}

//! \brief \ref interface \ref requirement
TEST_CASE("Test continuation") {
    tbb::task_group tg;

    int x{}, y{};
    int sum{};
    auto sum_task = tg.defer([&] {
        sum = x + y;
    });

    auto y_task = tg.defer([&] {
        y = 2;
    });

    auto x_task = tg.defer([&] {
        x = 40;
    });

    sum_task.add_predecessor(x_task);
    sum_task.add_predecessor(y_task);

    tg.run(std::move(sum_task));
    tg.run(std::move(x_task));
    tg.run(std::move(y_task));

    tg.wait();

    REQUIRE(sum == 42);
}

//! \brief \ref interface \ref requirement
TEST_CASE("Test continuation - small tree") {
    tbb::task_group tg;

    int x{}, y{};
    int sum{};
    auto sum_task = tg.defer([&] {
        sum = x + y;
    });

    auto y_task = tg.defer([&] {
        y = 2;
    });

    auto x_task = tg.defer([&] {
        x = 40;
    });

    sum_task.add_predecessor(x_task);
    sum_task.add_predecessor(y_task);

    int multiplier{};
    int product{};
    auto mult_task = tg.defer([&] {
        multiplier = 42;
    });

    auto product_task = tg.defer([&] {
        product = sum * multiplier;
    });

    product_task.add_predecessor(sum_task);
    product_task.add_predecessor(mult_task);

    tg.run(std::move(sum_task));
    tg.run(std::move(x_task));
    tg.run(std::move(y_task));
    tg.run(std::move(mult_task));
    tg.run(std::move(product_task));

    tg.wait();

    REQUIRE(product == 42 * 42);
}

long serial_fib(int n) {
    return n < 2 ? n : serial_fib(n - 1) + serial_fib(n - 2);
}

struct fib_continuation {
    void operator()() const {
        m_data->m_sum = m_data->m_x + m_data->m_y;
        delete m_data;
    }

    struct data {
        data(int& sum) : m_sum(sum) {}
        int m_x{ 0 }, m_y{ 0 };
        int& m_sum;
    }* m_data;

    fib_continuation(fib_continuation::data* d) : m_data(d) {}
};

struct fib_computation {
    fib_computation(int n, int* x, tbb::task_group& tg) : m_n(n), m_x(x), m_tg(tg) {}

    void operator()() const {
        if (m_n < 16) {
            *m_x = serial_fib(m_n);
        } else {
            // Continuation passing
            fib_continuation::data* data = new fib_continuation::data(*m_x);
            auto continuation = m_tg.defer(fib_continuation{data});
            tbb::this_task_group::current_task::transfer_successors_to(continuation);
            // auto& c = *this->allocate_continuation<fib_continuation>(/* children_counter = */ 2, *x);
            auto right = m_tg.defer(fib_computation(m_n - 1, &data->m_x, m_tg));
            auto left = m_tg.defer(fib_computation(m_n - 2, &data->m_y, m_tg));

            continuation.add_predecessor(left);
            continuation.add_predecessor(right);
            m_tg.run(std::move(continuation));
            m_tg.run(std::move(left));
            m_tg.run(std::move(right));
        }
    }

    int m_n;
    int* m_x;
    tbb::task_group& m_tg;
};

//! \brief \ref interface \ref requirement
TEST_CASE("Test continuation - fibonacci") {
    tbb::task_group tg;
    int N = 0;
    tg.run(fib_computation(30, &N, tg));
    tg.wait();
    REQUIRE_MESSAGE(N == 832040, "Fibonacci(30) should be 832040");
}

//! \brief \ref interface \ref requirement
TEST_CASE("Test continuation - multiple successors") {
    tbb::task_group tg;

    int x{}, y{};
    int sum{};
    auto sum_task = tg.defer([&] {
        sum = x + y;
    });

    auto y_task = tg.defer([&] {
        y = 2;
    });

    auto x_task = tg.defer([&] {
        x = 40;
    });

    sum_task.add_predecessor(x_task);
    sum_task.add_predecessor(y_task);

    int multiplier{};
    int product{};
    auto mult_task = tg.defer([&] {
        multiplier = 42;
    });

    auto product_task = tg.defer([&] {
        product = sum * multiplier;
    });

    product_task.add_predecessor(sum_task);
    product_task.add_predecessor(mult_task);

    int product_plus_sum{};
    auto total_results = tg.defer([&] {
        product_plus_sum = product + sum;
    });

    product_task.add_successor(total_results);
    sum_task.add_successor(total_results);
    // total_results.add_predecessor(product_task);
    // total_results.add_predecessor(sum_task);

    tg.run(std::move(total_results));
    tg.run(std::move(sum_task));
    tg.run(std::move(x_task));
    tg.run(std::move(y_task));
    tg.run(std::move(mult_task));
    tg.run(std::move(product_task));

    tg.wait();

    REQUIRE(product_plus_sum == 42 * 42 + 42);
}

//! \brief \ref interface \ref requirement
TEST_CASE("Test add_predecessor after task submission") {
    tbb::task_group tg;

    int x{}, y{};
    int sum{};
    auto sum_task = tg.defer([&] {
        sum = x + y;
    });

    std::atomic<bool> is_ready{false};
    auto y_task = tg.defer([&] {
        y = 2;
        while (!is_ready) ;
    });

    auto x_task = tg.defer([&] {
        x = 40;
        while (!is_ready) ;
    });

    tg.run(std::move(x_task));
    tg.run(std::move(y_task));

    sum_task.add_predecessor(x_task);
    sum_task.add_predecessor(y_task);

    is_ready = true;
    tg.run(std::move(sum_task));

    tg.wait();

    REQUIRE(sum == 42);
}

//! \brief \ref interface \ref requirement
TEST_CASE("Test add_predecessor after task finish") {
    tbb::task_group tg;

    int x{}, y{};
    int sum{};
    auto sum_task = tg.defer([&] {
        sum = x + y;
    });

    std::atomic<bool> is_ready{};
    auto y_task = tg.defer([&] {
        y = 2;
    });

    auto x_task = tg.defer([&] {
        x = 40;
        is_ready = true;
    });

    tg.run(std::move(x_task));

    while (!is_ready) ;
    sum_task.add_predecessor(x_task);
    sum_task.add_predecessor(y_task);

    tg.run(std::move(y_task));
    tg.run(std::move(sum_task));

    tg.wait();

    REQUIRE(sum == 42);
}

namespace users {
    template<typename T>
    bool range_is_too_small(T b, T e) {
        return std::distance(b, e) <= 4;
    }

    template<typename T>
    void do_serial_sort(T b, T e) {
        std::sort(b, e);
    }

    template<typename T>
    void create_left_range(T& lb, T& le, T b, T e) {
        lb = b;
        le = b + std::distance(b, e) / 2;
    }

    template<typename T>
    void create_right_range(T& rb, T& re, T b, T e) {
        rb = b + std::distance(b, e) / 2;
        re = e;
    }

    template<typename T>
    void do_merge(T lb, T le, T rb, T re) {
        std::vector<typename T::value_type> merged(std::distance(lb, le) + std::distance(rb, re));
        std::merge(lb, le, rb, re, merged.begin());
        std::copy(merged.begin(), merged.end(), lb);
    }
}

template<typename T>
void merge_sort(tbb::task_group& tg, T b, T e) {
    if (users::range_is_too_small(b, e)) {
        // base-case when range is small
        users::do_serial_sort(b, e);
    } else {
        // calculate left and right ranges
        T lb, le, rb, re;
        users::create_left_range(lb, le, b, e);
        users::create_right_range(rb, re, b, e);

        // create the three tasks
        tbb::task_handle sortleft = tg.defer([lb, le, &tg] { merge_sort(tg, lb, le); });

        tbb::task_handle sortright = tg.defer([rb, re, &tg] { merge_sort(tg, rb, re); });
        tbb::task_handle merge = tg.defer([rb, lb, le, re] { users::do_merge(lb, le, rb, re); });

        // add predecessors for new merge task
        merge.add_predecessor(sortleft);
        merge.add_predecessor(sortright);

        // insert new subgraph between currently executing
        // task and its successors
        tbb::this_task_group::current_task::transfer_successors_to(merge);

        tg.run(std::move(sortleft));
        tg.run(std::move(sortright));
        tg.run(std::move(merge));
    }
}

//! \brief \ref interface \ref requirement
TEST_CASE("Test continuation - recursive decomposition") {
    tbb::task_group tg;
    int size = 10000;
    std::vector<int> v(size);
    for (int i = 0; i < size; ++i) {
        v[i] = size - i;
    }

    tg.run_and_wait(tg.defer([&] { merge_sort(tg, v.begin(), v.end()); }));

    for (int i = 0; i < size - 1; ++i) {
        REQUIRE(v[i] <= v[i + 1]);
    }
}

//! \brief \ref interface \ref requirement
TEST_CASE("Test continuation - add dependency on pre-submitted task tree") {
    tbb::task_group tg;
    int sum{};
    int x{}, y{};

    auto init_sum_task = tg.defer([&] {

        auto final_sum = tg.defer([&] {
            sum = x + y;
        });

        tbb::this_task_group::current_task::transfer_successors_to(final_sum);
        auto x_task = tg.defer([&] {
            x = 40;
        });

        auto y_task = tg.defer([&] {
            y = 2;
        });

        final_sum.add_predecessor(x_task);
        final_sum.add_predecessor(y_task);

        tg.run(std::move(final_sum));
        tg.run(std::move(x_task));
        tg.run(std::move(y_task));
    });

    int product{};
    auto product_task = tg.defer([&] {
        product = sum * sum;
    });

    product_task.add_predecessor(init_sum_task);

    tg.run(std::move(init_sum_task));
    tg.run(std::move(product_task));

    tg.wait();

    REQUIRE(product == 42 * 42);
}

//! \brief \ref interface \ref requirement
TEST_CASE("Test continuation - add dependency after transfer_successors_to") {
    tbb::task_group tg;
    int sum{};
    int x{}, y{};

    std::atomic<bool> is_ready{false};
    auto init_sum_task = tg.defer([&] {

        auto final_sum = tg.defer([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            sum = x + y;
        });

        tbb::this_task_group::current_task::transfer_successors_to(final_sum);
        is_ready = true;

        auto x_task = tg.defer([&] {
            x = 40;
        });

        auto y_task = tg.defer([&] {
            y = 2;
        });

        final_sum.add_predecessor(x_task);
        final_sum.add_predecessor(y_task);

        tg.run(std::move(final_sum));
        tg.run(std::move(x_task));
        tg.run(std::move(y_task));
    });

    int product{};
    auto product_task = tg.defer([&] {
        product = sum * sum;
    });
    tg.run(std::move(init_sum_task));

    while (!is_ready) ;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    product_task.add_predecessor(init_sum_task);

    tg.run(std::move(product_task));

    tg.wait();

    REQUIRE(product == 42 * 42);
}

