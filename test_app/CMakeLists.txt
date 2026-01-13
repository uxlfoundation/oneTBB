cmake_minimum_required(VERSION 3.28)
project(test_tbb_module LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(.. ../build/gnu_13.3_cxx20_64_debug)

add_executable(test_tbb_module test.cpp)
target_compile_options(test_tbb_module PUBLIC -mrtm -mwaitpkg -fcf-protection=branch -fstack-protector-strong)
target_link_libraries(test_tbb_module PRIVATE TBB::tbb)
