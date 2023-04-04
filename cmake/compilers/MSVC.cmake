# Copyright (C) 2023 Intel Corporation
#
# This software and the related documents are Intel copyrighted materials, and your use of them is
# governed by the express license under which they were provided to you ("License"). Unless the
# License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
# transmit this software or the related documents without Intel's prior written permission.
#
# This software and the related documents are provided as is, with no express or implied
# warranties, other than those that are expressly stated in the License.

# Workaround for CMake issue https://gitlab.kitware.com/cmake/cmake/issues/18317.
# TODO: consider use of CMP0092 CMake policy.
string(REGEX REPLACE "/W[0-4]" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

set(TCM_WARNING_LEVEL $<$<NOT:$<CXX_COMPILER_ID:Intel>>:/W4> $<$<BOOL:${TCM_STRICT}>:/WX>)

# suppress warning: NULL pointer dereferenced
# suppress warning: Prefer 'enum class' over 'enum'
set(TCM_WARNING_SUPPRESS /wd6011 /wd26812)

set(TCM_LIB_COMPILE_FLAGS -D_CRT_SECURE_NO_WARNINGS /GS)
set(TCM_COMMON_COMPILE_FLAGS /volatile:iso /FS /EHsc)
set(TCM_COMMON_LINK_LIBS Kernel32.lib)

# prevent Windows.h from adding unnecessary includes
# prevent Windows.h from definiting mix/max as macros
# prevent compilation warnings to suggest secure version of library functions
set(TCM_COMPILE_DEFINITIONS WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
