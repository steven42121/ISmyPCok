cmake_minimum_required(VERSION 3.20)

project(ISmyPCok VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(ISPCOK_ENABLE_AVX2 "Enable AVX2 benchmark implementation in ispcok_core" OFF)
option(ISPCOK_ENABLE_AVX512 "Enable AVX-512 benchmark implementation in ispcok_core" OFF)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/CppBenchmark/CMakeLists.txt")
    set(ISPCOK_CPPBENCHMARK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/CppBenchmark")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/3rd_party/CppBenchmark/CMakeLists.txt")
    set(ISPCOK_CPPBENCHMARK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/3rd_party/CppBenchmark")
else()
    message(FATAL_ERROR "Missing third_party/CppBenchmark. Run scripts/fetch_3rd_party.ps1 first.")
endif()

# Build only the CppBenchmark core library (skip upstream examples/tests/tools)
set(CPPBENCHMARK_MODULE ON CACHE BOOL "Build CppBenchmark as module" FORCE)
add_subdirectory("${ISPCOK_CPPBENCHMARK_ROOT}" "third_party_build/CppBenchmark")

add_library(ispcok_core
    src/core/builtin_modules.cpp
    src/core/builtin_cpu_modules.cpp
    src/core/builtin_memory_modules.cpp
    src/core/builtin_storage_modules.cpp
    src/core/builtin_network_modules.cpp
    src/core/builtin_system_modules.cpp
    src/core/builtin_placeholder_modules.cpp
    src/core/engine.cpp
    src/core/json_writer.cpp
    src/core/plugin_loader.cpp
)
target_include_directories(ispcok_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
if(WIN32)
    target_link_libraries(ispcok_core PRIVATE ws2_32)
endif()
if(ISPCOK_ENABLE_AVX512)
    if(MSVC)
        target_compile_options(ispcok_core PRIVATE /arch:AVX512)
    else()
        target_compile_options(ispcok_core PRIVATE -mavx512f -mfma)
    endif()
elseif(ISPCOK_ENABLE_AVX2)
    if(MSVC)
        target_compile_options(ispcok_core PRIVATE /arch:AVX2)
    else()
        target_compile_options(ispcok_core PRIVATE -mavx2 -mfma)
    endif()
endif()
if(MSVC)
    target_compile_options(ispcok_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(ispcok_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(ispcok_capi SHARED
    src/capi/ispcok_capi.cpp
)
target_link_libraries(ispcok_capi PRIVATE ispcok_core)
target_include_directories(ispcok_capi
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_compile_definitions(ispcok_capi PRIVATE ISPCOK_CAPI_EXPORTS)

add_executable(ispcok_cli
    src/cli/main.cpp
)
target_link_libraries(ispcok_cli PRIVATE ispcok_core)
target_include_directories(ispcok_cli
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

add_executable(pc_benchmark
    src/pc_benchmark.cpp
)
target_link_libraries(pc_benchmark PRIVATE cppbenchmark)
target_include_directories(pc_benchmark PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_executable(ispcok_tests
    tests/ispcok_tests.cpp
)
target_link_libraries(ispcok_tests PRIVATE ispcok_core)
target_include_directories(ispcok_tests
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/samples/plugins/mock_gpu_plugin.cpp")
    add_library(ispcok_plugin_sample_gpu MODULE
        samples/plugins/mock_gpu_plugin.cpp
    )
    target_include_directories(ispcok_plugin_sample_gpu PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    set_target_properties(ispcok_plugin_sample_gpu PROPERTIES
        PREFIX ""
        OUTPUT_NAME "ispcok_plugin_gpu_vulkan_sample"
    )
endif()

if(MSVC)
    target_compile_options(ispcok_tests PRIVATE /W4 /permissive-)
    target_compile_options(ispcok_cli PRIVATE /W4 /permissive-)
    target_compile_options(ispcok_capi PRIVATE /W4 /permissive-)
    target_compile_options(pc_benchmark PRIVATE /W4 /permissive-)
else()
    target_compile_options(ispcok_tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(ispcok_cli PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(ispcok_capi PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(pc_benchmark PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_test(NAME ispcok_tests COMMAND ispcok_tests)
