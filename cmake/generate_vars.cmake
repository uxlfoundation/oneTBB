# Copyright (C) 2025 Intel Corporation
#
# This software and the related documents are Intel copyrighted materials, and your use of them is
# governed by the express license under which they were provided to you ("License"). Unless the
# License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
# transmit this software or the related documents without Intel's prior written permission.
#
# This software and the related documents are provided as is, with no express or implied
# warranties, other than those that are expressly stated in the License.

# Return if not executed in script mode
if (NOT DEFINED CMAKE_SCRIPT_MODE_FILE)
    return()
endif()

set(LIBRARY_PATH_PLACEHOLDER "${TCM_LIBDIR}")
set(BINARY_PATH_PLACEHOLDER "${TCM_BINDIR}")

configure_file("${VARS_TEMPLATE}" "${OUTPUT_VARS_NAME}" @ONLY)
