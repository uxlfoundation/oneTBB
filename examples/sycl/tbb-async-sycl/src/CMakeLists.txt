if (${CMAKE_HOST_SYSTEM_NAME} STREQUAL "Windows")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /Qtbb")
else()
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -qtbb")
endif()

# Set default build type to RelWithDebInfo if not specified
if (NOT CMAKE_BUILD_TYPE)
    message (STATUS "Default CMAKE_BUILD_TYPE not set. Using Release with Debug Info")
    set (CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE
        STRING "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel"
        FORCE)
endif()

add_executable (tbb-async-sycl tbb-async-sycl.cpp)

find_package(Threads REQUIRED)
target_link_libraries(tbb-async-sycl Threads::Threads)

add_custom_target (run ./tbb-async-sycl)
