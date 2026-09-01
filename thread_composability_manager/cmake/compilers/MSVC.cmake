# Copyright (c) 2023 Intel Corporation
# Copyright (c) 2026 UXL Foundation Contributors
#
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# Workaround for CMake issue https://gitlab.kitware.com/cmake/cmake/issues/18317.
# TODO: consider use of CMP0092 CMake policy.
string(REGEX REPLACE "/W[0-4]" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

set(TCM_WARNING_LEVEL $<$<NOT:$<CXX_COMPILER_ID:Intel>>:/W4> $<$<BOOL:${TCM_STRICT}>:/WX>)

# suppress warning: NULL pointer dereferenced
# suppress warning: Prefer 'enum class' over 'enum'
set(TCM_WARNING_SUPPRESS /wd6011 /wd26812)

# suppress warning: 'function' was declared deprecated
set(TCM_TEST_WARNING_FLAGS ${TCM_TEST_WARNING_FLAGS} /wd4996)

set(TCM_LIB_COMPILE_FLAGS -D_CRT_SECURE_NO_WARNINGS /GS /Gy /GL /sdl)
if (NOT CMAKE_CXX_COMPILER_ID MATCHES "(Intel|IntelLLVM|Clang)")
  set(TCM_LIB_COMPILE_FLAGS ${TCM_LIB_COMPILE_FLAGS} /analyze)
endif()

set(TCM_COMMON_COMPILE_FLAGS /volatile:iso /FS /EHsc)
set(TCM_COMMON_LINK_LIBS Kernel32.lib)

# The "/DEPENDENTLOADFLAG:0x2000" restricts the loader to look for dependencies in current working
# directory only if it is in the so-called "Safe load list".
set(TCM_LIB_LINK_FLAGS ${TCM_LIB_LINK_FLAGS}
  LINKER:/DYNAMICBASE LINKER:/NXCOMPAT LINKER:/DEPENDENTLOADFLAG:0x2000 LINKER:/LTCG
  LINKER:/INCREMENTAL:NO)
if (TCM_ARCH EQUAL 32)
    set(TCM_LIB_LINK_FLAGS ${TCM_LIB_LINK_FLAGS} LINKER:/SAFESEH)
endif()

# prevent Windows.h from adding unnecessary includes
# prevent Windows.h from definiting mix/max as macros
# prevent compilation warnings to suggest secure version of library functions
set(TCM_COMPILE_DEFINITIONS WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
set(TCM_OPENMP_MISSING_LINK_FLAG TRUE)
