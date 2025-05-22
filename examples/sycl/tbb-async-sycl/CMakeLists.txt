cmake_minimum_required (VERSION 3.4)

if (${CMAKE_HOST_SYSTEM_NAME} STREQUAL "Windows")
    set(CMAKE_CXX_COMPILER "icx-cl")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsycl")
else()
    set(CMAKE_CXX_COMPILER "icpx")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsycl")
endif()

project (TBB-ASYNC-SYCL LANGUAGES CXX)
add_subdirectory (src)
