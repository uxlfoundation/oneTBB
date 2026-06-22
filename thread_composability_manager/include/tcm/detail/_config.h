/*
   Copyright (c) 2023 Intel Corporation
   Copyright (c) 2026 UXL Foundation Contributors

   SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

#ifndef __TCM_CONFIG_HEADER
#define __TCM_CONFIG_HEADER

#ifndef TCM_USE_ASSERT
#define TCM_USE_ASSERT TCM_DEBUG
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

#endif  // __TCM_CONFIG_HEADER
