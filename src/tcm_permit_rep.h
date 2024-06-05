/*
    Copyright (C) 2024 Intel Corporation

    This software and the related documents are Intel copyrighted materials, and your use of them is
    governed by the express license under which they were provided to you ("License"). Unless the
    License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
    transmit this software or the related documents without Intel's prior written permission.

    This software and the related documents are provided as is, with no express or implied
    warranties, other than those that are expressly stated in the License.
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
