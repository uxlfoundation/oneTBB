# Copyright (C) 2025 Intel Corporation
#
# This software and the related documents are Intel copyrighted materials, and your use of them is
# governed by the express license under which they were provided to you ("License"). Unless the
# License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
# transmit this software or the related documents without Intel's prior written permission.
#
# This software and the related documents are provided as is, with no express or implied
# warranties, other than those that are expressly stated in the License.

if (UNIX)
    set(tcm_vars_name "vars.sh")
    set(tcm_vars_template "${CMAKE_SOURCE_DIR}/integration/linux/vars/vars.sh.in")
    set(TCM_LIBDIR "$TCMROOT/${CMAKE_INSTALL_LIBDIR}")
    set(TCM_BINDIR "$TCMROOT/${CMAKE_INSTALL_BINDIR}")
else()
    set(tcm_vars_name "vars.bat")
    set(tcm_vars_template "${CMAKE_SOURCE_DIR}/integration/windows/vars/vars.bat.in")
    set(TCM_LIBDIR "%TCMROOT%\\${CMAKE_INSTALL_LIBDIR}")
    set(TCM_BINDIR "%TCMROOT%\\${CMAKE_INSTALL_BINDIR}")
endif()

macro(tcm_generate_vars target)
    # Generate vars as part of build
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND
        ${CMAKE_COMMAND}
        -DVARS_TEMPLATE=${tcm_vars_template}
        -DOUTPUT_VARS_NAME=${tcm_vars_name}
        -DTCM_LIBDIR="$<TARGET_FILE_DIR:${target}>"
        -DTCM_BINDIR="$<TARGET_FILE_DIR:${target}>"
        -P "${CMAKE_SOURCE_DIR}/cmake/generate_vars.cmake"
        COMMAND
        ${CMAKE_COMMAND} -E copy
        "${CMAKE_CURRENT_BINARY_DIR}/${tcm_vars_name}"
        "$<TARGET_FILE_DIR:${target}>/${tcm_vars_name}"
    )

    # Generate vars as part of install
    add_custom_command(TARGET ${target} POST_BUILD COMMAND
        ${CMAKE_COMMAND}
        -DVARS_TEMPLATE=${tcm_vars_template}
        -DOUTPUT_VARS_NAME=${tcm_vars_name}_install
        -DTCM_LIBDIR=${TCM_LIBDIR}
        -DTCM_BINDIR=${TCM_BINDIR}
        -P "${CMAKE_SOURCE_DIR}/cmake/generate_vars.cmake"
        VERBATIM
    )

    install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/${tcm_vars_name}_install"
        DESTINATION env
        RENAME "${tcm_vars_name}"
    )
endmacro()
