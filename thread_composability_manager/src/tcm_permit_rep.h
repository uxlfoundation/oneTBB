/*
   Copyright (c) 2024 Intel Corporation

   SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

#ifndef __TCM_PERMIT_REP_HEADER
#define __TCM_PERMIT_REP_HEADER

#include <atomic>
#include <cstdint>

#include "tcm/types.h"

struct tcm_permit_data_t {
  tcm_client_id_t client_id;
  std::atomic<uint32_t>* concurrency;
  tcm_cpu_mask_t* cpu_mask;
  uint32_t size;
  std::atomic<tcm_permit_state_t> state;
  tcm_permit_flags_t flags;
  uint32_t tcm_epoch_snapshot; // Updated whenever this permit asks for resources by itself (via request permit or activate)
  std::atomic<bool> is_nested; // Indicates whether this permit is nested
  std::atomic<uint32_t> inherited_concurrency_idx; // Index of constraint where inherited concurrency will be stored
};

extern "C" {

typedef uint64_t tcm_permit_epoch_t;

struct tcm_permit_rep_t {
  std::atomic<tcm_permit_epoch_t> epoch;
  tcm_permit_request_t request; // Holds latest corresponding request
  tcm_permit_data_t data;
  void* callback_arg;
};

} // extern "C"

#endif // __TCM_PERMIT_REP_HEADER
