# Copyright (c) 2021-2022 Intel Corporation

cmake_minimum_required(VERSION 3.1)
project(TCM LANGUAGES CXX VERSION 0.5)

if (NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_EXTENSIONS OFF) # use -std=c++... instead of -std=gnu++...

option(TCM_TEST "Enable testing" ON)
option(TCM_STRICT "Treat compiler warnings as errors" ON)

# include compile settings
set(TCM_COMPILER_SETTINGS_FILE ${CMAKE_CURRENT_SOURCE_DIR}/cmake/compilers/${CMAKE_CXX_COMPILER_ID}.cmake)
if (EXISTS ${TCM_COMPILER_SETTINGS_FILE})
    include(${TCM_COMPILER_SETTINGS_FILE})
else()
    message(WARNING "TCM compiler settings not found ${TCM_COMPILER_SETTINGS_FILE}")
endif()

# --------------------------------------------------------------------------------------------------
# Define Cpack variables + CMake installation related includes
set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
string(TOLOWER ${PROJECT_NAME}-${CPACK_PACKAGE_VERSION}-${CMAKE_SYSTEM_NAME} CPACK_PACKAGE_FILE_NAME)
set(CPACK_GENERATOR ZIP)

include(CPack)
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)
# --------------------------------------------------------------------------------------------------
set(CMAKE_DEBUG_POSTFIX _debug)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
    message(STATUS "CMAKE_BUILD_TYPE is not specified. Using default: ${CMAKE_BUILD_TYPE}")
    # Possible values of build type for cmake-gui
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

if (NOT DEFINED TCM_OUTPUT_DIR_BASE)
     string(REGEX MATCH "^([0-9]+\.[0-9]+|[0-9]+)" _tcm_compiler_version_short ${CMAKE_CXX_COMPILER_VERSION})
     string(TOLOWER ${CMAKE_CXX_COMPILER_ID}_${_tcm_compiler_version_short}_cxx${CMAKE_CXX_STANDARD} TCM_OUTPUT_DIR_BASE)
     unset(_tcm_compiler_version_short)
endif()

foreach(output_type LIBRARY ARCHIVE PDB RUNTIME)
    if (CMAKE_BUILD_TYPE)
        string(TOLOWER ${CMAKE_BUILD_TYPE} _tcm_build_type_lower)
        set(CMAKE_${output_type}_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${TCM_OUTPUT_DIR_BASE}_${_tcm_build_type_lower})
        unset(_tcm_build_type_lower)
    endif()

    if (CMAKE_CONFIGURATION_TYPES)
        foreach(suffix ${CMAKE_CONFIGURATION_TYPES})
            string(TOUPPER ${suffix} _tcm_suffix_upper)
            string(TOLOWER ${suffix} _tcm_suffix_lower)
            set(CMAKE_${output_type}_OUTPUT_DIRECTORY_${_tcm_suffix_upper} ${CMAKE_BINARY_DIR}/${TCM_OUTPUT_DIR_BASE}_${_tcm_suffix_lower})
        endforeach()
        unset(_tcm_suffix_lower)
        unset(_tcm_suffix_upper)
    endif()
endforeach()


if (TCM_DIR)
  # Allow specifying external TCM to test with.
  # Do not add main targets and installation instructions in that case.
  message(STATUS "Using external TCM for testing")
  find_package(TCM REQUIRED)
else()
  add_subdirectory(src)
  # Define common installation rules
  install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

  install(EXPORT ${PROJECT_NAME}Targets
    NAMESPACE TCM::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
  )
  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
    "include(\${CMAKE_CURRENT_LIST_DIR}/${PROJECT_NAME}Targets.cmake)\n"
  )

  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
    COMPATIBILITY AnyNewerVersion
  )

  install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
          "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
  )
endif()

if (TCM_TEST)
  enable_testing()
  add_subdirectory(test)
endif()

add_subdirectory(cmake/post_install)
