# Copyright (C) 2023 Intel Corporation
#
# This software and the related documents are Intel copyrighted materials, and your use of them is
# governed by the express license under which they were provided to you ("License"). Unless the
# License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
# transmit this software or the related documents without Intel's prior written permission.
#
# This software and the related documents are provided as is, with no express or implied
# warranties, other than those that are expressly stated in the License.

# Depfile options (e.g. -MD) are inserted automatically in some cases.
# Don't add -MMD to avoid conflicts in such cases.
if (NOT CMAKE_GENERATOR MATCHES "Ninja" AND NOT CMAKE_CXX_DEPENDS_USE_COMPILER)
    set(TCM_MMD_FLAG -MMD)
endif()

set(TCM_WARNING_LEVEL -Wall -Wextra -Wpedantic $<$<BOOL:${TCM_STRICT}>:-Werror>)
set(TCM_TEST_WARNING_FLAGS -Wshadow -Wcast-qual -Woverloaded-virtual -Wnon-virtual-dtor)

set(TCM_COMPILE_DEFINITIONS "")

set(TCM_COMMON_LINK_LIBS ${CMAKE_DL_LIBS})
