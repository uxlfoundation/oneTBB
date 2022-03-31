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

#if _MSC_VER && !__INTEL_COMPILER
#define __TCM_SUPPRESS_WARNING_PUSH __pragma(warning(push))
#define __TCM_SUPPRESS_WARNING(w) __pragma(warning(disable : w))
#define __TCM_SUPPRESS_WARNING_POP __pragma(warning(pop))
#define __TCM_SUPPRESS_WARNING_WITH_PUSH(w)                             \
    __TCM_SUPPRESS_WARNING_PUSH __TCM_SUPPRESS_WARNING(w)
#else
#define __TCM_SUPPRESS_WARNING_PUSH
#define __TCM_SUPPRESS_WARNING(w)
#define __TCM_SUPPRESS_WARNING_POP
#define __TCM_SUPPRESS_WARNING_WITH_PUSH(w)
#endif

//! Utility template function to prevent "unused" warnings by various compilers.
template<typename T>
void suppress_unused_warning(const T&) {}

} // namespace internal
} // namespace tcm
