/*
   Copyright (c) 2023 Intel Corporation
   Copyright (c) 2026 UXL Foundation Contributors

   SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

#ifndef __TCM_ASSERT_HEADER
#define __TCM_ASSERT_HEADER

#include "_config.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>

namespace tcm {
namespace internal {

//! Utility template function to prevent "unused" warnings by various compilers.
template<typename T>
static void suppress_unused_warning(const T&) {}

#if TCM_USE_ASSERT
static void report_failed_assert(const char* location, int line, const char* condition,
                                 const char* message) {
  static std::atomic<bool> has_assert_reported{false};
  if (has_assert_reported.exchange(true, std::memory_order_relaxed))
    return;

  const char* description = message;
  if (!message || *message == '\0')
    description = "<no assert description provided>";

  std::fprintf(stderr, "Assertion \"%s\" failed (function %s, line %d)\n"
               "\tDescription: %s\n", condition, location, line, description);
  std::fflush(stderr);
  std::abort();
}
#endif  // TCM_USE_ASSERT

#if TCM_USE_ASSERT
#define __TCM_ASSERT(condition, message)                                \
  ((condition)? ((void)0) :                                             \
   ::tcm::internal::                                                    \
   report_failed_assert(__func__, __LINE__, #condition, message))

#define __TCM_ASSERT_EX(condition, message)     \
    __TCM_ASSERT((condition), (message))
#else  // TCM_USE_ASSERT
#define __TCM_ASSERT(condition, message) ((void)0)
#define __TCM_ASSERT_EX(condition, message) tcm::internal::suppress_unused_warning(condition)
#endif  // TCM_USE_ASSERT

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

} // namespace internal
} // namespace tcm

#endif  // __TCM_ASSERT_HEADER
