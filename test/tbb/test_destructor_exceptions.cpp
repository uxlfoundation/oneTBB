/*
    Copyright (c) 2026 UXL Foundation Contributors

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

#define TBB_PREVIEW_ISOLATED_TASK_GROUP 1
#include "common/test.h"

#include "oneapi/tbb/flow_graph.h"
#include "oneapi/tbb/global_control.h"
#include "oneapi/tbb/task_arena.h"
#include "oneapi/tbb/task_group.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <thread>
#include <type_traits>

//! \file test_destructor_exceptions.cpp
//! \brief Test for [flow_graph.graph scheduler.task_group] specification

namespace {

#if TBB_USE_EXCEPTIONS

// which context to test
enum class context_kind { owned, external };
// test for exception from task or assertion failure triggering
enum class exception_kind { user, assertion };
enum class exit_kind { propagation, termination };

struct user_exception {};
struct assertion_exception {};
struct outer_exception {};

struct scenario_state {
    exception_kind source;
    exit_kind exit;
    std::thread::id destroying_thread{std::this_thread::get_id()};
    std::atomic<bool> task_started{false};
    std::atomic<bool> destruction_started{false};
    std::atomic<bool> task_throwing{false};
    std::atomic<unsigned> assertion_calls{0};
    bool outer_throwing{false};

    scenario_state(exception_kind source_, exit_kind exit_) : source(source_), exit(exit_) {}
};

std::atomic<scenario_state*> current_scenario{nullptr};
const char assertion_message[] = "Throw from destructor cleanup";

// A terminate handler exits without returning to doctest, so checks must fail the process directly.
void require_scenario(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "Destructor exception test failed: %s\n", message);
        std::fflush(stderr);
        std::_Exit(EXIT_FAILURE);
    }
}

bool expected_exception(std::exception_ptr exception) {
    if (!exception)
        return false;
    try {
        std::rethrow_exception(exception);
    } catch (const user_exception&) {
        return current_scenario.load()->source == exception_kind::user;
    } catch (const assertion_exception&) {
        return current_scenario.load()->source == exception_kind::assertion;
    } catch (...) {
        return false;
    }
}

void check_exception_source() {
    require_scenario(current_scenario.load()->destruction_started, "destruction did not start");
    require_scenario(current_scenario.load()->task_started, "task did not start");
    require_scenario(current_scenario.load()->task_throwing, "task did not throw during cleanup");
    unsigned expected_calls = current_scenario.load()->source == exception_kind::assertion ? 1 : 0;
    require_scenario(current_scenario.load()->assertion_calls == expected_calls,
                     "unexpected assertion handler call count");
    require_scenario(std::this_thread::get_id() == current_scenario.load()->destroying_thread,
                     "exception did not reach the destroying thread");
    require_scenario(expected_exception(std::current_exception()), "wrong exception reached destruction");
}

void terminate_handler() noexcept {
    require_scenario(current_scenario.load()->exit == exit_kind::termination,
                     "termination instead of exception propagation");
    require_scenario(current_scenario.load()->outer_throwing, "outer exception was not thrown");
    check_exception_source();
    std::_Exit(EXIT_SUCCESS);
}

void assertion_handler(const char*, int, const char*, const char* comment) {
    require_scenario(current_scenario.load()->source == exception_kind::assertion, "unexpected assertion");
    require_scenario(current_scenario.load()->task_throwing, "assertion outside the throwing task");
    require_scenario(comment && std::strcmp(comment, assertion_message) == 0,
                     "unexpected assertion message");
    require_scenario(current_scenario.load()->assertion_calls.fetch_add(1) == 0,
                     "assertion handler called more than once");
    throw assertion_exception{};
}

[[noreturn]] void throw_from_task() {
    require_scenario(current_scenario.load()->destruction_started, "task threw before destructor cleanup");
    current_scenario.load()->task_throwing = true;
    if (current_scenario.load()->source == exception_kind::assertion) {
        __TBB_ASSERT_RELEASE(false, assertion_message);
        require_scenario(false, "assertion handler returned");
    }
    throw user_exception{};
}

struct destruction_marker {
    ~destruction_marker() {
#if __TBB_CPP17_UNCAUGHT_EXCEPTIONS_PRESENT
        bool unwinding = std::uncaught_exceptions() > 0;
#else
        bool unwinding = std::uncaught_exception();
#endif
        require_scenario(unwinding == (current_scenario.load()->exit == exit_kind::termination),
                         "incorrect unwinding state before destruction");
        current_scenario.load()->destruction_started = true;
    }
};

class throwing_graph_task : public tbb::detail::d2::graph_task {
    tbb::detail::d1::task* execute(tbb::detail::d1::execution_data&) override {
        current_scenario.load()->task_started = true;
        throw_from_task();
    }

    tbb::detail::d1::task* cancel(tbb::detail::d1::execution_data& ed) override {
        // The dispatcher cancels the throwing task; only then may its wait reference be released.
        finalize<throwing_graph_task>(ed);
        return nullptr;
    }

public:
    throwing_graph_task(tbb::flow::graph& graph, tbb::detail::d1::small_object_allocator& allocator)
        : graph_task(graph, allocator) {}
};

void prepare_task(tbb::flow::graph& graph) {
    tbb::detail::d1::small_object_allocator allocator;
    auto* task = allocator.new_object<throwing_graph_task>(graph, allocator);
    tbb::detail::d2::spawn_in_graph_arena(graph, *task);
}

template <typename Group>
void prepare_task(Group& group) {
    group.run([] {
        current_scenario.load()->task_started = true;
        // A queued task would be skipped by destructor cancellation. Keep this task running instead.
        while (!tbb::is_current_task_group_canceling())
            std::this_thread::yield();
        throw_from_task();
    });
    // Wait until the task starts so cancellation cannot skip its body.
    while (!current_scenario.load()->task_started)
        std::this_thread::yield();
}

template <typename Group>
void prepare_destruction(Group& group) {
    prepare_task(group);
    destruction_marker marker;
    if (current_scenario.load()->exit == exit_kind::termination) {
        current_scenario.load()->outer_throwing = true;
        throw outer_exception{};
    }
}

template <typename Group>
void destroy_group(context_kind context) {
    if (context == context_kind::external) {
        tbb::task_group_context external_context(tbb::task_group_context::isolated);
        Group group(external_context);
        prepare_destruction(group);
    } else {
        Group group;
        prepare_destruction(group);
    }
}

template <typename Group>
void run_scenario(context_kind context, exception_kind source, exit_kind exit) {
    scenario_state state(source, exit);
    current_scenario = &state;
    auto previous_terminate = std::set_terminate(terminate_handler);
    auto previous_assertion = tbb::set_assertion_handler(assertion_handler);
    auto restore_handlers = tbb::detail::make_raii_guard([=] {
        tbb::set_assertion_handler(previous_assertion);
        std::set_terminate(previous_terminate);
        current_scenario = nullptr;
    });

    // A graph task must run in its destructor's wait; task groups need one already-running worker.
    tbb::task_arena arena(std::is_same<Group, tbb::flow::graph>::value ? 1 : 2);
    bool caught = false;
    try {
        arena.execute([&] { destroy_group<Group>(context); });
        require_scenario(false, "destruction returned without an exception");
    } catch (...) {
        require_scenario(exit == exit_kind::propagation, "exception escaped instead of termination");
        check_exception_source();
        caught = true;
    }
    REQUIRE_MESSAGE(caught, "The destructor must propagate the task exception");

    arena.execute([] {
        bool task_completed = false;
        tbb::task_group group;
        group.run([&] { task_completed = true; });
        group.wait();
        CHECK_MESSAGE(task_completed, "Task-group work must succeed after destructor exception");

        bool graph_completed = false;
        tbb::flow::graph graph;
        tbb::flow::function_node<int> node(graph, tbb::flow::serial, [&](int) {
            graph_completed = true;
        });
        REQUIRE(node.try_put(1));
        graph.wait_for_all();
        CHECK_MESSAGE(graph_completed, "Graph work must succeed after destructor exception");
    });
}

using graph = tbb::flow::graph;
using task_group = tbb::task_group;
using isolated_task_group = tbb::isolated_task_group;

#define DESTRUCTOR_EXCEPTION_CASE(Group, Context, Source, Exit)                       \
    TEST_CASE(#Group "_" #Context "_" #Source "_" #Exit) {                            \
        run_scenario<Group>(context_kind::Context, exception_kind::Source, exit_kind::Exit); \
    }

#else

#define DESTRUCTOR_EXCEPTION_CASE(Group, Context, Source, Exit)                       \
    TEST_CASE(#Group "_" #Context "_" #Source "_" #Exit) {                            \
        std::puts("SKIP: destructor exception tests require exception support");       \
    }

#endif // TBB_USE_EXCEPTIONS

#define DESTRUCTOR_EXCEPTION_CASES(Group, Context)                      \
    DESTRUCTOR_EXCEPTION_CASE(Group, Context, user, propagation)        \
    DESTRUCTOR_EXCEPTION_CASE(Group, Context, assertion, propagation)   \
    DESTRUCTOR_EXCEPTION_CASE(Group, Context, user, termination)          \
    DESTRUCTOR_EXCEPTION_CASE(Group, Context, assertion, termination)

// Note that the names of the test cases are used in CMake to run them individually
//! \brief \ref error_guessing
DESTRUCTOR_EXCEPTION_CASES(graph, owned)
DESTRUCTOR_EXCEPTION_CASES(graph, external)
DESTRUCTOR_EXCEPTION_CASES(task_group, owned)
DESTRUCTOR_EXCEPTION_CASES(task_group, external)
DESTRUCTOR_EXCEPTION_CASES(isolated_task_group, owned)
DESTRUCTOR_EXCEPTION_CASES(isolated_task_group, external)

#undef DESTRUCTOR_EXCEPTION_CASES
#undef DESTRUCTOR_EXCEPTION_CASE

} // namespace
