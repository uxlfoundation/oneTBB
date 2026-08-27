# Copyright (c) 2025 Intel Corporation
# Copyright (c) 2026 UXL Foundation Contributors
#
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# Return if not executed in script mode
if (NOT DEFINED CMAKE_SCRIPT_MODE_FILE)
    return()
endif()

set(LIBRARY_PATH_PLACEHOLDER "${TCM_LIBDIR}")
set(BINARY_PATH_PLACEHOLDER "${TCM_BINDIR}")

configure_file("${VARS_TEMPLATE}" "${OUTPUT_VARS_NAME}" @ONLY)
