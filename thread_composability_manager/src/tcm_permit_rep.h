/*
   Copyright (c) 2024 Intel Corporation
   Copyright (c) 2026 UXL Foundation Contributors

   SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

#ifndef __TCM_PERMIT_REP_HEADER
#define __TCM_PERMIT_REP_HEADER

#include <atomic>
#include <cstdint>
#include <cstddef>

#include <mutex>
#include <unordered_set>
#include <stack>

#include "tcm/types.h"

namespace tcm {
namespace internal {

struct thread_registration_data_t {
  std::mutex mutex;
  std::stack<tcm_permit_handle_t> permits; // Permits with which a thread has been registered

  std::unordered_set<std::size_t> seen; // Previous permits with which this thread once has been
                                        // registered.
  ~thread_registration_data_t();
};

struct permit_registrations_t {
  std::mutex mutex;
  std::unordered_set<thread_registration_data_t*> thread_locals;
};

} // namespace internal
} // namespace tcm

struct tcm_permit_data_t {
  tcm_client_id_t client_id;
  std::atomic<uint32_t>* concurrency;
  tcm_cpu_mask_t* cpu_mask;
  uint32_t size;
  std::atomic<tcm_permit_state_t> state;
  tcm_permit_flags_t flags;
  uint32_t tcm_epoch_snapshot; // Updated whenever this permit asks for resources by itself (via
                               // request permit or activate)

  std::atomic<bool> is_nested; // Indicates whether this permit is nested
  std::atomic<uint32_t> inherited_concurrency_idx; // Index of constraint where inherited
                                                   // concurrency will be stored

  tcm::internal::permit_registrations_t registrations; // Pointer to the global threads register
};

extern "C" {

typedef uint64_t tcm_permit_epoch_t;

struct tcm_permit_rep_t {
  const std::size_t permit_id{0}; // Unique permit identifier
  std::atomic<tcm_permit_epoch_t> epoch{0};
  tcm_permit_request_t request{}; // Holds latest corresponding request
  tcm_permit_data_t data{};
  void* callback_arg{};
};

} // extern "C"

#endif // __TCM_PERMIT_REP_HEADER
