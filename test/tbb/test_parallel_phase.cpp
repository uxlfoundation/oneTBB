/*
    Copyright (c) 2025 Intel Corporation
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

//! \file test_parallel_phase.cpp
//! \brief Test for [scheduler.task_arena scheduler.parallel_phase] functionality
//!
#if defined(_MSC_VER) && !defined(__INTEL_COMPILER)
#pragma warning(push)
#pragma warning(disable: 4324) // warning C4324: structure was padded due to alignment specifier
#endif

#include <utility>

#include "common/test.h"
#include "common/utils.h"
#include "common/utils_concurrency_limit.h"

#include "oneapi/tbb/detail/_parallel_phase.h"
#include "tbb/global_control.h"
#include "tbb/task_arena.h"

// For thread_leave_manager
#include "../src/tbb/misc.cpp"
#include "../src/tbb/arena.h"

#if defined(_MSC_VER) && !defined(__INTEL_COMPILER)
#pragma warning(pop)
#endif

using end_flag_fast_leave = tbb::task_arena::parallel_phase::end_flag_fast_leave;

using tbb::detail::r1::thread_leave_manager;

//! \brief \ref error_guessing
TEST_CASE("Test thread_leave_manager state machine") {
    thread_leave_manager tlm;
    tlm.set_initial_state(tbb::task_arena::leave_policy::fast);
    REQUIRE(!tlm.is_retention_allowed());

    {
        tbb::global_control gc(tbb::global_control::leave_policy, tbb::task_arena::leave_policy::fast);
        tlm.set_initial_state(tbb::task_arena::leave_policy::automatic);
        REQUIRE(!tlm.is_retention_allowed());
    }

    REQUIRE(!tlm.is_retention_allowed());
    tlm.register_parallel_phase();
    REQUIRE(tlm.is_retention_allowed());
    tlm.unregister_parallel_phase(0);

    tlm.set_initial_state(tbb::task_arena::leave_policy::automatic);
    if (tlm.is_retention_allowed()) {
        tlm.register_parallel_phase();
        REQUIRE(tlm.is_retention_allowed());
        tlm.unregister_parallel_phase(tbb::detail::d1::phase::end_fast_leave);
        REQUIRE(!tlm.is_retention_allowed());
        tlm.reset_if_needed();
        REQUIRE(tlm.is_retention_allowed());
    }
}

//! \brief \ref stress \ref error_guessing
TEST_CASE("Test thread_leave_manager under contention") {
    thread_leave_manager tlm;
    tlm.set_initial_state(tbb::task_arena::leave_policy::fast);

    const utils::thread_num_type num_threads = utils::get_platform_max_threads();
    constexpr int iters = 1000;

    utils::NativeParallelFor(num_threads, [&](utils::thread_num_type idx) {
        for (int i = 0; i < iters; ++i) {
            tlm.register_parallel_phase();
            REQUIRE(tlm.is_retention_allowed());
            // Exercise both fast-leave and non-fast-leave unregister paths.
            std::uintptr_t flags = ((i + idx) % 2) ? std::uintptr_t(tbb::detail::d1::phase::end_fast_leave)
                                                    : std::uintptr_t(0);
            tlm.unregister_parallel_phase(flags);
            // Reset should never disturb the state
            tlm.reset_if_needed();
        }
    });

    REQUIRE(!tlm.is_retention_allowed());
}

struct arena_with_leave_manager : public tbb::task_arena {
    using tbb::task_arena::task_arena;
    using tbb::task_arena::get_leave_policy;

    thread_leave_manager& get_thread_leave_manager() {
        initialize();
        auto* a = my_arena.load(std::memory_order_relaxed);
        REQUIRE_MESSAGE(a, "arena must be initialized to inspect its thread_leave_manager");
        return a->my_thread_leave;
    }
};

//! \brief \ref interface \ref requirement
TEST_CASE("Test task_arena leavy policy settings") {
    using leave_policy = tbb::task_arena::leave_policy;

    {
        // The default leave policy is automatic
        arena_with_leave_manager ta{};
        REQUIRE(ta.get_leave_policy() == leave_policy::automatic);
    }
    {
        // The leave policy set by the constructor is reflected by the arena
        arena_with_leave_manager ta{tbb::task_arena::automatic, 1, tbb::task_arena::priority::normal,
                                    leave_policy::fast};
        REQUIRE(ta.get_leave_policy() == leave_policy::fast);
        REQUIRE(!ta.get_thread_leave_manager().is_retention_allowed());
    }
    {
        arena_with_leave_manager ta{tbb::task_arena::automatic, 1, tbb::task_arena::priority::normal,
                                    leave_policy::automatic};
        REQUIRE(ta.get_leave_policy() == leave_policy::automatic);
    }
    {
        // The same holds for the constructor taking constraints
        arena_with_leave_manager ta{tbb::task_arena::constraints{}, 1, tbb::task_arena::priority::normal,
                                    leave_policy::fast};
        REQUIRE(ta.get_leave_policy() == leave_policy::fast);
        REQUIRE(!ta.get_thread_leave_manager().is_retention_allowed());
    }
    {
        // The leave policy of a arena is set by the initialize method
        arena_with_leave_manager ta{};
        REQUIRE(!ta.is_active());

        ta.initialize(tbb::task_arena::automatic, 1, tbb::task_arena::priority::normal, leave_policy::fast);
        REQUIRE(ta.is_active());
        REQUIRE(ta.get_leave_policy() == leave_policy::fast);
        REQUIRE(!ta.get_thread_leave_manager().is_retention_allowed());
    }
    {
        // The same holds for the initialize method taking constraints
        arena_with_leave_manager ta{};
        ta.initialize(tbb::task_arena::constraints{}, 1, tbb::task_arena::priority::normal,
                      leave_policy::fast);
        REQUIRE(ta.is_active());
        REQUIRE(ta.get_leave_policy() == leave_policy::fast);
        REQUIRE(!ta.get_thread_leave_manager().is_retention_allowed());
    }
    {
        // The leave policy set by the constructor survives the initialization and termination
        arena_with_leave_manager ta{tbb::task_arena::automatic, 1, tbb::task_arena::priority::normal,
                                    leave_policy::fast};
        ta.initialize();
        REQUIRE(ta.get_leave_policy() == leave_policy::fast);
        REQUIRE(!ta.get_thread_leave_manager().is_retention_allowed());

        ta.terminate();
        REQUIRE(!ta.is_active());
        REQUIRE(ta.get_leave_policy() == leave_policy::fast);

        ta.initialize();
        REQUIRE(ta.get_leave_policy() == leave_policy::fast);
        REQUIRE(!ta.get_thread_leave_manager().is_retention_allowed());

        // The settings that are not passed to the repeated initialization are reset
        ta.terminate();
        ta.initialize(tbb::task_arena::automatic, 1, tbb::task_arena::priority::normal);
        REQUIRE(ta.get_leave_policy() == leave_policy::automatic);
    }
    {
        // The same holds for the initialize method taking constraints
        arena_with_leave_manager ta{tbb::task_arena::constraints{}, 1, tbb::task_arena::priority::normal,
                                    leave_policy::fast};
        REQUIRE(!ta.get_thread_leave_manager().is_retention_allowed());

        ta.terminate();
        ta.initialize(tbb::task_arena::constraints{}, 1, tbb::task_arena::priority::normal);
        REQUIRE(ta.get_leave_policy() == leave_policy::automatic);
    }
}

//! \brief \ref interface \ref requirement
TEST_CASE("Test thread_leave_manager state machine via explicit parallel_phase API") {
    arena_with_leave_manager ta_fast{tbb::task_arena::automatic, 1, tbb::task_arena::priority::normal,
                             tbb::task_arena::leave_policy::fast};
    auto& tlm_fast = ta_fast.get_thread_leave_manager();
    REQUIRE(!tlm_fast.is_retention_allowed());

    // During active parallel_phase, thread retention should be allowed
    ta_fast.start_parallel_phase();
    REQUIRE(tlm_fast.is_retention_allowed());

    // Return to leave policy set by the arena constructor, i.e. fast
    ta_fast.end_parallel_phase();
    REQUIRE(!tlm_fast.is_retention_allowed());

    arena_with_leave_manager ta_auto{tbb::task_arena::automatic, 1, tbb::task_arena::priority::normal,
                             tbb::task_arena::leave_policy::automatic};
    auto& tlm_auto = ta_auto.get_thread_leave_manager();
    // Automatic leave policy might not allow retention by default (configuration specific)
    bool default_state = tlm_auto.is_retention_allowed();

    ta_auto.start_parallel_phase();
    REQUIRE(tlm_auto.is_retention_allowed());

    ta_auto.end_parallel_phase(end_flag_fast_leave{});
    REQUIRE(!tlm_auto.is_retention_allowed());

    ta_auto.start_parallel_phase();
    REQUIRE(tlm_auto.is_retention_allowed());

    // After end_parallel_phase without fast_leave, retention state should return to the default state
    ta_auto.end_parallel_phase();
    REQUIRE(tlm_auto.is_retention_allowed() == default_state);
}

//! \brief \ref interface \ref requirement
TEST_CASE("Test thread_leave_manager state machine via RAII parallel_phase API") {
    arena_with_leave_manager ta{tbb::task_arena::automatic, 1, tbb::task_arena::priority::normal,
                        tbb::task_arena::leave_policy::automatic};
    auto& tlm = ta.get_thread_leave_manager();
    bool default_state = tlm.is_retention_allowed();

    {
        tbb::task_arena::parallel_phase phase{ta};
        REQUIRE(tlm.is_retention_allowed());
    }
    REQUIRE(tlm.is_retention_allowed() == default_state);

    {
        tbb::task_arena::parallel_phase phase{ta, end_flag_fast_leave{}};
        REQUIRE(tlm.is_retention_allowed());
    }
    REQUIRE(!tlm.is_retention_allowed());

    {
        tbb::task_arena::parallel_phase phase{ta};
        REQUIRE(tlm.is_retention_allowed());
    }
    REQUIRE(tlm.is_retention_allowed() == default_state);

    // Explicit end, the destructor at scope exit must be a no-op.
    {
        tbb::task_arena::parallel_phase phase{ta};
        REQUIRE(tlm.is_retention_allowed());
        phase.end();
        REQUIRE(tlm.is_retention_allowed() == default_state);
    }
    REQUIRE(tlm.is_retention_allowed() == default_state);

    {
        tbb::task_arena::parallel_phase phase{ta, end_flag_fast_leave{}};
        REQUIRE(tlm.is_retention_allowed());
        phase.end();
        REQUIRE(!tlm.is_retention_allowed());
    }
    REQUIRE(!tlm.is_retention_allowed());
}

//! \brief \ref interface \ref requirement
TEST_CASE("Test thread_leave_manager state machine via explicit parallel_phase API for this_task_arena") {
    // Start phase to initialize the implicit arena for the calling thread.
    tbb::this_task_arena::start_parallel_phase();

    // Attach a helper arena object to the implicit arena so its
    // thread_leave_manager state can be inspected directly.
    arena_with_leave_manager ta_attach{tbb::attach{}};
    auto& tlm = ta_attach.get_thread_leave_manager();
    REQUIRE(tlm.is_retention_allowed());

    tbb::this_task_arena::end_parallel_phase();
    bool default_state = tlm.is_retention_allowed();

    tbb::this_task_arena::start_parallel_phase();
    REQUIRE(tlm.is_retention_allowed());
    tbb::this_task_arena::end_parallel_phase(end_flag_fast_leave{});
    REQUIRE(!tlm.is_retention_allowed());

    tbb::this_task_arena::start_parallel_phase();
    REQUIRE(tlm.is_retention_allowed());
    tbb::this_task_arena::end_parallel_phase();
    REQUIRE(tlm.is_retention_allowed() == default_state);
}

//! \brief \ref interface \ref requirement
TEST_CASE("Test thread_leave_manager state machine via RAII parallel_phase API for this_task_arena") {
    tbb::this_task_arena::start_parallel_phase();
    arena_with_leave_manager ta_attach{tbb::attach{}};
    auto& tlm = ta_attach.get_thread_leave_manager();
    tbb::this_task_arena::end_parallel_phase();
    bool default_state = tlm.is_retention_allowed();

    {
        tbb::task_arena::parallel_phase phase{tbb::attach{}};
        REQUIRE(tlm.is_retention_allowed());
    }
    REQUIRE(tlm.is_retention_allowed() == default_state);

    {
        tbb::task_arena::parallel_phase phase{tbb::attach{}, end_flag_fast_leave{}};
        REQUIRE(tlm.is_retention_allowed());
    }
    REQUIRE(!tlm.is_retention_allowed());

    // Explicit end, the destructor at scope exit must be a no-op.
    {
        tbb::task_arena::parallel_phase phase{tbb::attach{}};
        REQUIRE(tlm.is_retention_allowed());
        phase.end();
        REQUIRE(tlm.is_retention_allowed() == default_state);
    }
    REQUIRE(tlm.is_retention_allowed() == default_state);
    {
        tbb::task_arena::parallel_phase phase{tbb::attach{}, end_flag_fast_leave{}};
        REQUIRE(tlm.is_retention_allowed());
        phase.end();
        REQUIRE(!tlm.is_retention_allowed());
    }
    REQUIRE(!tlm.is_retention_allowed());
}

//! \brief \ref interface \ref requirement
TEST_CASE("RAII parallel_phase move construction transfers ownership") {
    arena_with_leave_manager ta{tbb::task_arena::automatic, 1, tbb::task_arena::priority::normal,
                        tbb::task_arena::leave_policy::fast};
    auto& tlm = ta.get_thread_leave_manager();
    bool default_state = tlm.is_retention_allowed();

    {
        tbb::task_arena::parallel_phase phase1{ta, end_flag_fast_leave{}};
        REQUIRE(tlm.is_retention_allowed());

        tbb::task_arena::parallel_phase phase2{std::move(phase1)};
        // Ownership moved to phase2; the phase is still active (no unregister happened).
        REQUIRE(tlm.is_retention_allowed());

        // phase1 no longer owns the registration; its destruction must be a no-op,
        // leaving phase2 as the sole owner responsible for unregistering.
    }
    // phase2 has ended the phase with end_with_fast_leave, so retention is no longer allowed
    REQUIRE(!tlm.is_retention_allowed());

    // Consumed by the next phase, restoring default behavior.
    {
        tbb::task_arena::parallel_phase phase{ta};
        REQUIRE(tlm.is_retention_allowed());
    }
    REQUIRE(tlm.is_retention_allowed() == default_state);
}

//! \brief \ref interface \ref requirement
TEST_CASE("RAII parallel_phase move assignment transfers ownership across arenas") {
    arena_with_leave_manager ta1{tbb::task_arena::automatic, 1, tbb::task_arena::priority::normal,
                        tbb::task_arena::leave_policy::fast};
    arena_with_leave_manager ta2{tbb::task_arena::automatic, 1, tbb::task_arena::priority::normal,
                        tbb::task_arena::leave_policy::automatic};
    auto& tlm1 = ta1.get_thread_leave_manager();
    auto& tlm2 = ta2.get_thread_leave_manager();

    {
        tbb::task_arena::parallel_phase phase1{ta1};
        tbb::task_arena::parallel_phase phase2{ta2, end_flag_fast_leave{}};
        REQUIRE(tlm1.is_retention_allowed());
        REQUIRE(tlm2.is_retention_allowed());

        // Move assignment ends phase1 first, then transfers ownership of phase2's
        phase1 = std::move(phase2);
        // Phase for ta1 is no longer active
        REQUIRE(!tlm1.is_retention_allowed());
        REQUIRE(tlm2.is_retention_allowed());
    }
    // phase1 (now owning ta2's registration with end_with_fast_leave) unregisters it here.
    REQUIRE(!tlm2.is_retention_allowed());
    REQUIRE(!tlm1.is_retention_allowed());
}

//! \brief \ref error_guessing
TEST_CASE("RAII parallel_phase moved from already ended phase") {
    arena_with_leave_manager ta{tbb::task_arena::automatic, 1, tbb::task_arena::priority::normal,
                                tbb::task_arena::leave_policy::fast};
    auto& tlm = ta.get_thread_leave_manager();
    REQUIRE(!tlm.is_retention_allowed());

    {
        // Move constructor
        tbb::task_arena::parallel_phase phase{ta};
        REQUIRE(tlm.is_retention_allowed());
        phase.end();
        REQUIRE(!tlm.is_retention_allowed());

        // The source object owns nothing, so the moved-to object must not own anything
        tbb::task_arena::parallel_phase moved{std::move(phase)};
        REQUIRE(!tlm.is_retention_allowed());
    }
    {
        // Move assignment
        tbb::task_arena::parallel_phase src{ta};
        src.end();

        tbb::task_arena::parallel_phase dst{ta};
        REQUIRE(tlm.is_retention_allowed());

        // Move assignment ends the phase owned by dst and takes over the ownership from src
        dst = std::move(src);
        REQUIRE(!tlm.is_retention_allowed());
        // Deliberately ending the phase must be a no-op
        dst.end();
    }
}

//! \brief \ref interface \ref requirement
TEST_CASE("Test thread_leave_manager state machine with global_control leave_policy") {
    arena_with_leave_manager ta{tbb::task_arena::automatic, 1, tbb::task_arena::priority::normal,
                                tbb::task_arena::leave_policy::automatic};
    {
        tbb::global_control gc(tbb::global_control::leave_policy, tbb::task_arena::leave_policy::fast);

        // ta is initialized only at this point, so the leave policy is set to fast
        auto& tlm = ta.get_thread_leave_manager();
        REQUIRE(!tlm.is_retention_allowed());

        ta.start_parallel_phase();
        REQUIRE(tlm.is_retention_allowed());

        ta.end_parallel_phase();
        REQUIRE(!tlm.is_retention_allowed());

        ta.start_parallel_phase();
        REQUIRE(tlm.is_retention_allowed());
        ta.end_parallel_phase(end_flag_fast_leave{});
        REQUIRE(!tlm.is_retention_allowed());
    }
    // Leave policy remains fast even after global_control is destroyed
    REQUIRE(!ta.get_thread_leave_manager().is_retention_allowed());
}

//! \brief \ref interface \ref requirement
TEST_CASE("Test global_control leave_policy active_value") {
    using tbb::global_control;
    constexpr auto leave_policy = global_control::leave_policy;
    constexpr auto automatic    = size_t(tbb::task_arena::leave_policy::automatic);
    constexpr auto fast         = size_t(tbb::task_arena::leave_policy::fast);

    // Default active_value should be automatic
    REQUIRE(global_control::active_value(leave_policy) == automatic);

    {
        // Single global_control with fast leave
        global_control gc1(leave_policy, fast);
        REQUIRE(global_control::active_value(leave_policy) == fast);

        {
            // Disjunction: any fast => fast
            global_control gc2(leave_policy, automatic);
            REQUIRE(global_control::active_value(leave_policy) == fast);
        }
        // gc2 destroyed, gc1 still active
        REQUIRE(global_control::active_value(leave_policy) == fast);
    }
    // All global_control objects destroyed, should return to default
    REQUIRE(global_control::active_value(leave_policy) == automatic);

    {
        // Only automatic: no effect
        global_control gc1(leave_policy, automatic);
        REQUIRE(global_control::active_value(leave_policy) == automatic);
    }

    {
        // Multiple fast objects
        global_control gc1(leave_policy, fast);
        global_control gc2(leave_policy, fast);
        REQUIRE(global_control::active_value(leave_policy) == fast);
    }
    REQUIRE(global_control::active_value(leave_policy) == automatic);
}

//! \brief \ref interface
TEST_CASE("Feature test macro") {
    CHECK_MESSAGE(TBB_HAS_PARALLEL_PHASE == 202608, "Incorrect feature test macro");
}
