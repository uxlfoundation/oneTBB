cmake_minimum_required(VERSION 3.5.0...3.31.3)

project(benchmark_sort CXX)

set(CMAKE_CXX_STANDARD 11)

find_package(TBB REQUIRED)
find_package(Threads REQUIRED)
find_package(benchmark REQUIRED)

add_executable(benchmark_sort benchmark_sort.cpp)

target_link_libraries(benchmark_sort TBB::tbb Threads::Threads benchmark::benchmark)
