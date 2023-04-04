# Copyright (C) 2023 Intel Corporation
#
# This software and the related documents are Intel copyrighted materials, and your use of them is
# governed by the express license under which they were provided to you ("License"). Unless the
# License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
# transmit this software or the related documents without Intel's prior written permission.
#
# This software and the related documents are provided as is, with no express or implied
# warranties, other than those that are expressly stated in the License.

include(FindPackageHandleStandardArgs)

# Search for HWLOC in config mode (i.e. search for HWLOCConfig.cmake)
find_package(HWLOC QUIET CONFIG)
if (HWLOC_FOUND)
    find_package_handle_standard_args(HWLOC CONFIG_MODE)
    return()
endif()

# Check if HWLOC target already exists
if (TARGET HWLOC::hwloc)
    return()
endif()

# Define additional paths to search libs/includes
if (WIN32)
    list(APPEND _additional_lib_dirs ENV PATH ENV LIB)
    list(APPEND _additional_include_dirs ENV INCLUDE ENV CPATH)
else()
    list(APPEND _additional_lib_dirs ENV LIBRARY_PATH ENV LD_LIBRARY_PATH ENV DYLD_LIBRARY_PATH)
    list(APPEND _additional_include_dirs ENV CPATH ENV C_INCLUDE_PATH ENV CPLUS_INCLUDE_PATH ENV INCLUDE_PATH)
    set(_hwloc_lib_name hwloc)
endif()

# Parse the HWLOC version using hwloc-info executable
find_program(_hwloc_info_exe
    NAMES hwloc-info
    PATHS ENV HWLOC_ROOT ENV PATH
    PATH_SUFFIXES bin
)

if (_hwloc_info_exe)
    execute_process(
        COMMAND ${_hwloc_info_exe} "--version"
        OUTPUT_VARIABLE _hwloc_info_output
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    string(REGEX MATCH "([0-9]+.[0-9]+.[0-9]+)" HWLOC_VERSION "${_hwloc_info_output}")
    if ("${HWLOC_VERSION}" STREQUAL "")
        unset(HWLOC_VERSION)
    endif()

    unset(_hwloc_info_output)
endif()
unset(_hwloc_info_exe)

if (HWLOC_VERSION VERSION_GREATER_EQUAL "2.0")
    # Search for the includes path
    find_path(_hwloc_include_dirs
        NAMES hwloc.h
        PATHS ${_additional_include_dirs}
        PATH_SUFFIXES "hwloc"
    )

    if (_hwloc_include_dirs)
        # Define the shared library target
        add_library(HWLOC::hwloc SHARED IMPORTED)
        set_property(TARGET HWLOC::hwloc APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${_hwloc_include_dirs})

        # Search for the library
        if (WIN32)
            find_library(_hwloc_lib libhwloc PATHS ${_additional_lib_dirs})
            find_file(_hwloc_dll libhwloc-15.dll PATHS ${ADDITIONAL_LIB_DIRS})
            if (_hwloc_lib AND _hwloc_dll)
                set_target_properties(
                    HWLOC::hwloc PROPERTIES
                    IMPORTED_LOCATION "${_hwloc_dll}"
                    IMPORTED_IMPLIB   "${_hwloc_lib}"
                )
            endif()
            unset(_hwloc_dll)
        else()
            find_library(_hwloc_lib hwloc PATHS ${_additional_lib_dirs})
            if (_hwloc_lib)
                set_target_properties(
                    HWLOC::hwloc PROPERTIES
                    IMPORTED_LOCATION "${_hwloc_lib}"
                )
                endif()
        endif()

        set(HWLOC_FOUND 1)
    endif()
endif()

unset(_additional_include_dirs CACHE)
unset(_additional_lib_dirs CACHE)

find_package_handle_standard_args(
    HWLOC
    REQUIRED_VARS HWLOC_VERSION _hwloc_include_dirs _hwloc_lib
    VERSION_VAR HWLOC_VERSION
    FAIL_MESSAGE "Cannot find HWLOC: HWLOC >= 2.0 required."
)

unset(_hwloc_include_dirs CACHE)
unset(_hwloc_lib CACHE)
