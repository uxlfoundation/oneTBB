/*
    Copyright (c) 2025 Intel Corporation
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

//! \file test_tbbbind.cpp
//! \brief Test for TBBbind library, covers [configuration.debug_features]

#define TEST_CUSTOM_ASSERTION_HANDLER_ENABLED 1
#if _WIN32 || _WIN64
#define _CRT_SECURE_NO_WARNINGS
#endif

#if defined(_MSC_VER) && !defined(__INTEL_COMPILER)
#pragma warning(disable: 4324) // warning C4324: structure was padded due to alignment specifier
#pragma warning(disable: 4702) // warning C4702: unreachable code, the code became unreadchable after stubs adding
#pragma warning(disable: 4722) // warning C4722: destructor never returns, potential memory leak
#endif

#include "oneapi/tbb/global_control.h"
#include "common/test.h"
#include "common/utils_assert.h"

// no need to do it in the test
#define __TBB_SKIP_DEPENDENCY_SIGNATURE_VERIFICATION 1

// to get dynamic_link()
#include "oneapi/tbb/version.h"
#define SYMBOL_TO_FIND_LIBRARY TBB_runtime_version
#include "../../src/tbb/dynamic_link.cpp"

// have to define, as my_thread_leave in unconditionally used in outermost_worker_waiter
#define __TBB_PREVIEW_PARALLEL_PHASE 1

// we need only system_topology::load_tbbbind_shared_object() functionality,
// so add stubs for the rest of governor.cpp
#if __clang__
#pragma GCC diagnostic ignored "-Wunused-private-field"
#endif
#include "../../src/tbb/governor.cpp"

#if __TBB_DYNAMIC_LOAD_ENABLED

namespace tbb {
namespace detail {
namespace r1 {

basic_tls<thread_data*> governor::theTLS;
rml::tbb_factory governor::theRMLServerFactory;
bool governor::UsePrivateRML;
bool governor::is_rethrow_broken;
cpu_features_type governor::cpu_features;
::std::atomic<bool> __TBB_InitOnce::InitializationDone{};
#if !__TBB_USE_FUTEX
std::mutex concurrent_monitor_mutex::my_init_mutex;
#endif

void global_control_acquire() { abort(); }
void handle_perror( int , const char* ) { abort(); }
void detect_cpu_features(cpu_features_type&) { abort(); }
bool gcc_rethrow_exception_broken() { abort(); }
#if __TBB_USE_OS_AFFINITY_SYSCALL
void destroy_process_mask() { abort(); }
#endif
void runtime_warning( const char* , ... ) { abort(); }
void clear_address_waiter_table() { abort(); }
void global_control_release() { abort(); }
void DoOneTimeInitialization() { abort(); }
bool thread_dispatcher::must_join_workers() const { abort(); }
int AvailableHwConcurrency() { abort(); }
#if TBB_USE_ASSERT
bool is_present(d1::global_control&) { abort(); }
#endif // TBB_USE_ASSERT
bool remove_and_check_if_empty(d1::global_control&) { abort(); }
void PrintExtraVersionInfo( const char*, const char*, ... ) { abort(); }

threading_control* threading_control::register_public_reference() { abort(); }
std::size_t threading_control::worker_stack_size() { abort(); }
void threading_control::register_thread(thread_data&) { abort(); }
void threading_control::unregister_thread(thread_data&) { abort(); }
bool threading_control::unregister_public_reference(bool) { abort(); }
bool threading_control::unregister_lifetime_control(bool) { abort(); }
bool threading_control::is_present() { abort(); }

arena& arena::create(threading_control*, unsigned, unsigned, unsigned,
                    d1::constraints
#if __TBB_PREVIEW_PARALLEL_PHASE
                         , tbb::task_arena::leave_policy
#endif
    ) {
    abort();
}
void arena::on_thread_leaving(unsigned) { abort(); }
thread_control_monitor& arena::get_waiting_threads_monitor() {
    abort();
}

void observer_list::do_notify_exit_observers(observer_proxy*, bool) { abort(); }

#if __TBB_RESUMABLE_TASKS
bool task_dispatcher::resume(task_dispatcher&) { abort(); }
d1::suspend_point task_dispatcher::get_suspend_point() { abort(); }
#endif

void small_object_pool_impl::destroy() { abort(); }

#if _WIN32 || _WIN64
int NumberOfProcessorGroups() { abort(); }
#endif

namespace rml {
tbb_server* make_private_server( tbb_client&  ) { abort(); }
::rml::factory::status_type tbb_factory::open() { abort(); }
void tbb_factory::close() { abort(); }
::rml::factory::status_type tbb_factory::make_server(tbb::detail::r1::rml::tbb_server*&,
                                        tbb::detail::r1::rml::tbb_client&) {
    abort();
}
}

}}}

#endif // __TBB_DYNAMIC_LOAD_ENABLED

// Testing can't be done without TBBbind.
#if __TBB_SELF_CONTAINED_TBBBIND || __TBB_HWLOC_VALID_ENVIRONMENT
static bool isTbbBindAvailable() { return true; }
#else
static bool isTbbBindAvailable() { return false; }
#endif

// All assertions in TBBbind are available only in TBB_USE_ASSERT mode.
#if TBB_USE_ASSERT
static bool canTestAsserts() { return true; }
#else
static bool canTestAsserts() { return false; }
#endif

// this code hangs in  -DBUILD_SHARED_LIBS=OFF case (#1832)
#if __TBB_DYNAMIC_LOAD_ENABLED
// Must initialize system_topology and so register assertion handler in TBBbind.
// The test harness expects TBBBIND status always to be reported as part of TBB_VERSION
// output, so initialize system_topology even if TBBbind is not available.
struct Init {
    Init() {
        auto constraints = tbb::task_arena::constraints{}
            .set_max_threads_per_core(1);
        tbb::task_arena arena( constraints );
        arena.initialize();
    }
} init;
#endif

// The test relies on an assumption that system_topology::load_tbbbind_shared_object() find
// same instance of TBBbind as TBB uses internally.
//! Testing that assertions called inside TBBbind are handled correctly
//! \brief \ref interface \ref requirement
TEST_CASE("Using custom assertion handler inside TBBbind"
          * doctest::skip(!isTbbBindAvailable() || !canTestAsserts())) {
    // fills pointers to TBBbind entry points
    const char *tbbbind_path = tbb::detail::r1::system_topology::load_tbbbind_shared_object();
    REQUIRE_MESSAGE(tbbbind_path != nullptr, "Failed to load TBBbind");
    REQUIRE_MESSAGE(tbb::detail::r1::deallocate_binding_handler_ptr != nullptr,
                    "Failed to fill deallocate_binding_handler_ptr");

    // we are expecting that deallocate_binding_handler_ptr points to TBBbind entry point
    TEST_CUSTOM_ASSERTION_HANDLER(tbb::detail::r1::deallocate_binding_handler_ptr(nullptr),
                                  "Trying to deallocate nullptr pointer.");
}
