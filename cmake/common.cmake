# Copyright (c) 2021-2022 Intel Corporation

cmake_minimum_required(VERSION 3.1)

# compiler settings
if (MSVC)
  # Workaround for CMake issue https://gitlab.kitware.com/cmake/cmake/issues/18317.
  # TODO: consider use of CMP0092 CMake policy.
  string(REGEX REPLACE "/W[0-4]" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  string(REGEX REPLACE "-W[0-4]" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

  # MSVC special settings
  set(TCM_WARNING_LEVEL
      # warning level 4 and all warnings as errors
      /W4 /WX /EHsc
      # suppress warning: NULL pointer dereferenced
      /wd6011
      # suppress warning: Prefer 'enum class' over 'enum'
      /wd26812)

  set(TCM_COMPILE_DEFINITIONS
      # prevent Windows.h from adding unecessary includes
      WIN32_LEAN_AND_MEAN
      # prevent Windows.h from definiting mix/max as macros
      NOMINMAX
      # prevent compilation warnings to suggest secure version of library functions
      _CRT_SECURE_NO_WARNINGS)
else(MSVC)
  # non MSVC special settings
  set(TCM_WARNING_LEVEL
      # lots of warnings and all warnings as errors
      -Wall -Wextra -Wpedantic -Werror)

  set(TCM_COMPILE_DEFINITIONS "")
endif(MSVC)

if (NOT TCM_STRICT)
  unset(TCM_WARNING_LEVEL)
endif()
