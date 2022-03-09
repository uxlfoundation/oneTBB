/*
 *
 * Copyright (C) 2021-2022 Intel Corporation
 *
 */

#pragma once

#include <atomic>
#include <cstdio>
#include <cstdlib>

namespace tcm {
namespace internal {

void report_failed_assert(const char* location, int line, const char* condition, const char* message) {
  static std::atomic<bool> has_assert_reported{false};
  if (has_assert_reported.exchange(true, std::memory_order_relaxed))
    return;

  const char* description = message;
  if (!message || *message == '\0')
    description = "<no assert description provided>";

  std::fprintf(stderr, "Assertion %s failed (function %s, line %d)\n"
               "\tDescription: %s\n", condition, location, line, description);
  std::fflush(stderr);
  std::abort();
}

#if TCM_DEBUG
#define __TCM_ASSERT(condition, message)                                \
  ((condition)? ((void)0) :                                             \
   ::tcm::internal::                                                    \
   report_failed_assert(__func__, __LINE__, #condition, message))
#else
#define __TCM_ASSERT(condition, message) ((void)0)
#endif

} // namespace internal
} // namespace tcm
