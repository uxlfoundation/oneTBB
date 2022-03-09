# Copyright (c) 2021-2022 Intel Corporation

cmake_minimum_required(VERSION 3.10)

if (NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_EXTENSIONS OFF) # use -std=c++... instead of -std=gnu++...

project(tcm LANGUAGES CXX VERSION 0.5)

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

option(TCM_TEST "Enable testing" ON)

if (TCM_TEST)
  enable_testing()
  add_subdirectory(test)
endif()
