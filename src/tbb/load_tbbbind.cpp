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

//! \file load_tbbbind.cpp
//! \brief Implementation of TBBbind library loading

#if __TBB_WEAK_SYMBOLS_PRESENT
#pragma weak __TBB_internal_initialize_system_topology
#pragma weak __TBB_internal_destroy_system_topology
#pragma weak __TBB_internal_allocate_binding_handler
#pragma weak __TBB_internal_deallocate_binding_handler
#pragma weak __TBB_internal_apply_affinity
#pragma weak __TBB_internal_restore_affinity
#pragma weak __TBB_internal_get_default_concurrency
#pragma weak __TBB_internal_set_tbbbind_assertion_handler

extern "C" {
void __TBB_internal_initialize_system_topology(
    size_t groups_num,
    int& numa_nodes_count, int*& numa_indexes_list,
    int& core_types_count, int*& core_types_indexes_list
);
void __TBB_internal_destroy_system_topology( );

//TODO: consider renaming to `create_binding_handler` and `destroy_binding_handler`
binding_handler* __TBB_internal_allocate_binding_handler( int slot_num, int numa_id, int core_type_id, int max_threads_per_core );
void __TBB_internal_deallocate_binding_handler( binding_handler* handler_ptr );

void __TBB_internal_apply_affinity( binding_handler* handler_ptr, int slot_num );
void __TBB_internal_restore_affinity( binding_handler* handler_ptr, int slot_num );

int __TBB_internal_get_default_concurrency( int numa_id, int core_type_id, int max_threads_per_core );

void __TBB_internal_set_tbbbind_assertion_handler( assertion_handler_type handler );
}
#endif /* __TBB_WEAK_SYMBOLS_PRESENT */

// Stubs that will be used if TBBbind library is unavailable.
static void dummy_destroy_system_topology ( ) { }
static binding_handler* dummy_allocate_binding_handler ( int, int, int, int ) { return nullptr; }
static void dummy_deallocate_binding_handler ( binding_handler* ) { }
static void dummy_apply_affinity ( binding_handler*, int ) { }
static void dummy_restore_affinity ( binding_handler*, int ) { }
static int dummy_get_default_concurrency( int, int, int ) { return governor::default_num_threads(); }
static void dummy_set_assertion_handler( assertion_handler_type ) { }

// Handlers for communication with TBBbind
static void (*initialize_system_topology_ptr)(
    size_t groups_num,
    int& numa_nodes_count, int*& numa_indexes_list,
    int& core_types_count, int*& core_types_indexes_list
) = nullptr;
static void (*destroy_system_topology_ptr)( ) = dummy_destroy_system_topology;

static binding_handler* (*allocate_binding_handler_ptr)( int slot_num, int numa_id, int core_type_id, int max_threads_per_core )
    = dummy_allocate_binding_handler;
static void (*deallocate_binding_handler_ptr)( binding_handler* handler_ptr )
    = dummy_deallocate_binding_handler;
static void (*apply_affinity_ptr)( binding_handler* handler_ptr, int slot_num )
    = dummy_apply_affinity;
static void (*restore_affinity_ptr)( binding_handler* handler_ptr, int slot_num )
    = dummy_restore_affinity;
int (*get_default_concurrency_ptr)( int numa_id, int core_type_id, int max_threads_per_core )
    = dummy_get_default_concurrency;
void (*set_assertion_handler_ptr)( assertion_handler_type handler )
    = dummy_set_assertion_handler;

#if _WIN32 || _WIN64 || __unix__ || __APPLE__

// Table describing how to link the handlers.
static const dynamic_link_descriptor TbbBindLinkTable[] = {
    DLD(__TBB_internal_initialize_system_topology, initialize_system_topology_ptr),
    DLD(__TBB_internal_destroy_system_topology, destroy_system_topology_ptr),
#if __TBB_CPUBIND_PRESENT
    DLD(__TBB_internal_allocate_binding_handler, allocate_binding_handler_ptr),
    DLD(__TBB_internal_deallocate_binding_handler, deallocate_binding_handler_ptr),
    DLD(__TBB_internal_apply_affinity, apply_affinity_ptr),
    DLD(__TBB_internal_restore_affinity, restore_affinity_ptr),
#endif
    DLD(__TBB_internal_get_default_concurrency, get_default_concurrency_ptr)
};

static const unsigned LinkTableSize = sizeof(TbbBindLinkTable) / sizeof(dynamic_link_descriptor);

#if TBB_USE_DEBUG
#define DEBUG_SUFFIX "_debug"
#else
#define DEBUG_SUFFIX
#endif /* TBB_USE_DEBUG */

#if _WIN32 || _WIN64
#define LIBRARY_EXTENSION ".dll"
#define LIBRARY_PREFIX
#elif __APPLE__
#define LIBRARY_EXTENSION __TBB_STRING(.3.dylib)
#define LIBRARY_PREFIX "lib"
#elif __unix__
#define LIBRARY_EXTENSION __TBB_STRING(.so.3)
#define LIBRARY_PREFIX "lib"
#endif /* __unix__ */

#define TBBBIND_NAME            LIBRARY_PREFIX "tbbbind"            DEBUG_SUFFIX LIBRARY_EXTENSION
#define TBBBIND_2_0_NAME        LIBRARY_PREFIX "tbbbind_2_0"        DEBUG_SUFFIX LIBRARY_EXTENSION
#define TBBBIND_2_5_NAME        LIBRARY_PREFIX "tbbbind_2_5"        DEBUG_SUFFIX LIBRARY_EXTENSION
#endif /* _WIN32 || _WIN64 || __unix__ */

// Representation of system hardware topology information on the TBB side.
// System topology may be initialized by third-party component (e.g. hwloc)
// or just filled in with default stubs.
namespace system_topology {
namespace {
const char* load_tbbbind_shared_object() {
#if _WIN32 || _WIN64 || __unix__ || __APPLE__
#if _WIN32 && !_WIN64
    // For 32-bit Windows applications, process affinity masks can only support up to 32 logical CPUs.
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    if (si.dwNumberOfProcessors > 32) return nullptr;
#endif /* _WIN32 && !_WIN64 */
    for (const auto& tbbbind_version : {TBBBIND_2_5_NAME, TBBBIND_2_0_NAME, TBBBIND_NAME}) {
        if (dynamic_link(tbbbind_version, TbbBindLinkTable, LinkTableSize, nullptr, DYNAMIC_LINK_LOCAL_BINDING)) {
            return tbbbind_version;
        }
    }
#endif /* _WIN32 || _WIN64 || __unix__ || __APPLE__ */
    return nullptr;
}

}
} // namespace system_topology

