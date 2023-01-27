# Copyright (c) 2021-2022 Intel Corporation

cmake_minimum_required(VERSION 3.1)
project(tcm LANGUAGES CXX VERSION 0.5)

if (NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_EXTENSIONS OFF) # use -std=c++... instead of -std=gnu++...

option(TCM_TEST "Enable testing" ON)
option(TCM_STRICT "Treat compiler warnings as errors" ON)

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

add_subdirectory(src)

# --------------------------------------------------------------------------------------------------
# Define common installation rules
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT ${PROJECT_NAME}Targets
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
# --------------------------------------------------------------------------------------------------

# include compile settings
set(TCM_COMPILER_SETTINGS_FILE ${CMAKE_CURRENT_SOURCE_DIR}/cmake/compilers/${CMAKE_CXX_COMPILER_ID}.cmake)
if (EXISTS ${TCM_COMPILER_SETTINGS_FILE})
    include(${TCM_COMPILER_SETTINGS_FILE})
else()
    message(WARNING "TCM compiler settings not found ${TCM_COMPILER_SETTINGS_FILE}")
endif()

if (TCM_TEST)
  enable_testing()
  add_subdirectory(test)
endif()
