# Copyright (C) 2023 Intel Corporation
#
# This software and the related documents are Intel copyrighted materials, and your use of them is
# governed by the express license under which they were provided to you ("License"). Unless the
# License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
# transmit this software or the related documents without Intel's prior written permission.
#
# This software and the related documents are provided as is, with no express or implied
# warranties, other than those that are expressly stated in the License.

# Add code signing as post-install step.
if (DEFINED TCM_SIGNTOOL)
    file(TO_CMAKE_PATH "${TCM_SIGNTOOL}" TCM_SIGNTOOL)
    install(CODE "
    file(GLOB_RECURSE FILES_TO_SIGN \${CMAKE_INSTALL_PREFIX}/*tcm*${CMAKE_SHARED_LIBRARY_SUFFIX})
    execute_process(COMMAND ${TCM_SIGNTOOL} \${FILES_TO_SIGN} ${TCM_SIGNTOOL_ARGS})
")
endif()
