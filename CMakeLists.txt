cmake_minimum_required(VERSION 3.20)

project(ISmyPCok VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build only the CppBenchmark core library (skip upstream examples/tests/tools)
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/3rd_party/CppBenchmark/CMakeLists.txt")
    message(FATAL_ERROR "Missing 3rd_party/CppBenchmark. Run scripts/fetch_3rd_party.ps1 first.")
endif()
set(CPPBENCHMARK_MODULE ON CACHE BOOL "Build CppBenchmark as module" FORCE)
add_subdirectory(3rd_party/CppBenchmark)

add_executable(pc_benchmark
    src/pc_benchmark.cpp
)

target_link_libraries(pc_benchmark PRIVATE cppbenchmark)

if(MSVC)
    target_compile_options(pc_benchmark PRIVATE /W4 /permissive-)
else()
    target_compile_options(pc_benchmark PRIVATE -Wall -Wextra -Wpedantic)
endif()
