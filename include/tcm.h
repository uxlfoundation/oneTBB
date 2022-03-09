/*
 *
 * Copyright (C) 2021-2022 Intel Corporation
 *
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#if WIN32
  #define PERMIT_MANAGER_EXPORT __declspec(dllexport)
#else
  #define PERMIT_MANAGER_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum _ze_result_t {
    ZE_RESULT_SUCCESS = 0x0,
    ZE_RESULT_ERROR_INVALID_ARGUMENT = 0x78000004,
    ZE_RESULT_ERROR_UNKNOWN = 0x7ffffffe
} ze_result_t;

//
// Support for permit states
//

enum zerm_permit_states_t {
  ZERM_PERMIT_STATE_VOID,
  ZERM_PERMIT_STATE_INACTIVE,
  ZERM_PERMIT_STATE_PENDING,
  ZERM_PERMIT_STATE_IDLE,
  ZERM_PERMIT_STATE_ACTIVE
};

typedef uint8_t zerm_permit_state_t;
//
// End support for permit states
//

//
// Support for permit flags
//

typedef struct _zerm_permit_flags_t {
  bool stale : 1;
  bool rigid_concurrency : 1;
  bool exclusive : 1;
  int32_t reserved : 29;
} zerm_permit_flags_t;

typedef struct _zerm_callback_flags_t {
  bool new_concurrency : 1;
  bool new_state : 1;
  int32_t reserved : 30;
} zerm_callback_flags_t;

//
// End support for permit flags
//

//
// Support for cpu masks
//

struct hwloc_bitmap_s;
typedef struct hwloc_bitmap_s* hwloc_bitmap_t;
typedef hwloc_bitmap_t zerm_cpu_mask_t;

//
// End support for cpu masks
//

//
// Support for ids
//

typedef uint64_t zerm_client_id_t;

//
// End support for ids
//

typedef struct _zerm_permit_t {
  uint32_t* concurrencies;
  zerm_cpu_mask_t* cpu_masks;
  uint32_t size;
  zerm_permit_state_t state;
  zerm_permit_flags_t flags;
} zerm_permit_t;

typedef struct zerm_permit_rep_t* zerm_permit_handle_t;

//
// Support for constraints
//

typedef int32_t zerm_numa_node_t;
typedef int32_t zerm_core_type_t;

const int8_t zerm_automatic = -1;
const int8_t zerm_any = -2;

#define ZERM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER {zerm_automatic, zerm_automatic, NULL, \
                                                     zerm_automatic, zerm_automatic, zerm_automatic}

typedef struct _zerm_cpu_constraints_t {
  int32_t min_concurrency;
  int32_t max_concurrency;
  zerm_cpu_mask_t mask;
  zerm_numa_node_t numa_id;
  zerm_core_type_t core_type_id;
  int32_t threads_per_core;
} zerm_cpu_constraints_t;

//
// End support for constraints
//

//
// Support for priorities
//

enum zerm_request_priorities_t {
  ZERM_REQUEST_PRIORITY_LOW    = (INT32_MAX / 4) * 1,
  ZERM_REQUEST_PRIORITY_NORMAL = (INT32_MAX / 4) * 2,
  ZERM_REQUEST_PRIORITY_HIGH   = (INT32_MAX / 4) * 3
};

typedef int32_t zerm_request_priority_t;

//
// End support for priorities
//

//
// Support for requests
//

#define ZERM_PERMIT_REQUEST_INITIALIZER {zerm_automatic, zerm_automatic, \
                                         NULL, 0, ZERM_REQUEST_PRIORITY_NORMAL, {}, {}}

typedef struct _zerm_permit_request_t {
  int32_t min_sw_threads;
  int32_t max_sw_threads;
  zerm_cpu_constraints_t* cpu_constraints;
  uint32_t constraints_size;
  zerm_request_priority_t priority;
  zerm_permit_flags_t flags;
  char reserved[4];
} zerm_permit_request_t;

//
// End support for requests
//

//
// Thread Composability Manager functions:
//

typedef ze_result_t (*zerm_callback_t)(zerm_permit_handle_t p, void* arg, zerm_callback_flags_t);

PERMIT_MANAGER_EXPORT ze_result_t zermConnect(zerm_callback_t callback,
                                              zerm_client_id_t *client_id);
PERMIT_MANAGER_EXPORT ze_result_t zermDisconnect(zerm_client_id_t client_id);

PERMIT_MANAGER_EXPORT ze_result_t zermRequestPermit(zerm_client_id_t client_id,
                                                    zerm_permit_request_t request,
                                                    void* callback_arg,
                                                    zerm_permit_handle_t* permit_handle,
                                                    zerm_permit_t* permit);

PERMIT_MANAGER_EXPORT ze_result_t zermGetPermitData(zerm_permit_handle_t permit_handle,
                                                    zerm_permit_t* permit);

PERMIT_MANAGER_EXPORT ze_result_t zermReleasePermit(zerm_permit_handle_t permit);

PERMIT_MANAGER_EXPORT ze_result_t zermIdlePermit(zerm_permit_handle_t permit_handle);

PERMIT_MANAGER_EXPORT ze_result_t zermDeactivatePermit(zerm_permit_handle_t permit_handle);

PERMIT_MANAGER_EXPORT ze_result_t zermActivatePermit(zerm_permit_handle_t permit_handle);

PERMIT_MANAGER_EXPORT ze_result_t zermRegisterThread(zerm_permit_handle_t permit_handle);

PERMIT_MANAGER_EXPORT ze_result_t zermUnregisterThread();

//
// End Thread Composability Manager functions:
//

#ifdef __cplusplus
} // extern "C"
#endif
