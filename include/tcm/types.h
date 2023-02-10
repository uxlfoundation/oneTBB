/*
 *
 * Copyright (C) 2021-2022 Intel Corporation
 *
 */

#ifndef __TCM_TYPES_HEADER
#define __TCM_TYPES_HEADER

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Support for the TCM API return value

typedef enum _tcm_result_t {
  TCM_RESULT_SUCCESS = 0x0,
  TCM_RESULT_ERROR_INVALID_ARGUMENT = 0x78000004,
  TCM_RESULT_ERROR_UNKNOWN = 0x7ffffffe
} tcm_result_t;

// Support for permit states

enum tcm_permit_states_t {
  TCM_PERMIT_STATE_VOID,
  TCM_PERMIT_STATE_INACTIVE,
  TCM_PERMIT_STATE_PENDING,
  TCM_PERMIT_STATE_IDLE,
  TCM_PERMIT_STATE_ACTIVE
};

typedef uint8_t tcm_permit_state_t;

// Support for permit flags

typedef struct _tcm_permit_flags_t {
  uint32_t stale : 1;
  uint32_t rigid_concurrency : 1;
  uint32_t exclusive : 1;
  uint32_t reserved : 29;
} tcm_permit_flags_t;

typedef struct _tcm_callback_flags_t {
  uint32_t new_concurrency : 1;
  uint32_t new_state : 1;
  uint32_t reserved : 30;
} tcm_callback_flags_t;

// Support for cpu masks

struct hwloc_bitmap_s;
typedef struct hwloc_bitmap_s* hwloc_bitmap_t;
typedef hwloc_bitmap_t tcm_cpu_mask_t;

// Support for ids

typedef uint64_t tcm_client_id_t;

// Support for permits

typedef struct _tcm_permit_t {
  uint32_t* concurrencies;
  tcm_cpu_mask_t* cpu_masks;
  uint32_t size;
  tcm_permit_state_t state;
  tcm_permit_flags_t flags;
} tcm_permit_t;

// Support for permit handle

typedef struct tcm_permit_rep_t* tcm_permit_handle_t;

// Support for constraints

typedef int32_t tcm_numa_node_t;
typedef int32_t tcm_core_type_t;

const int8_t tcm_automatic = -1;
const int8_t tcm_any = -2;

#define TCM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER {tcm_automatic, tcm_automatic, NULL, \
                                                     tcm_automatic, tcm_automatic, tcm_automatic}

typedef struct _tcm_cpu_constraints_t {
  int32_t min_concurrency;
  int32_t max_concurrency;
  tcm_cpu_mask_t mask;
  tcm_numa_node_t numa_id;
  tcm_core_type_t core_type_id;
  int32_t threads_per_core;
} tcm_cpu_constraints_t;

// Support for priorities

enum tcm_request_priorities_t {
  TCM_REQUEST_PRIORITY_LOW    = (INT32_MAX / 4) * 1,
  TCM_REQUEST_PRIORITY_NORMAL = (INT32_MAX / 4) * 2,
  TCM_REQUEST_PRIORITY_HIGH   = (INT32_MAX / 4) * 3
};

typedef int32_t tcm_request_priority_t;

// Support for requests

#define TCM_PERMIT_REQUEST_INITIALIZER {tcm_automatic, tcm_automatic, NULL, 0, \
                                        TCM_REQUEST_PRIORITY_NORMAL, {}, {}}

typedef struct _tcm_permit_request_t {
  int32_t min_sw_threads;
  int32_t max_sw_threads;
  tcm_cpu_constraints_t* cpu_constraints;
  uint32_t constraints_size;
  tcm_request_priority_t priority;
  tcm_permit_flags_t flags;
  char reserved[4];
} tcm_permit_request_t;

// Support for client callback

typedef tcm_result_t (*tcm_callback_t)(tcm_permit_handle_t p, void* callback_arg, tcm_callback_flags_t);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* __TCM_TYPES_HEADER */

