# Real GPU/accelerator backend plugins.
#
# Each backend is an optional MODULE library that links its GPU SDK. When the
# SDK is absent (or a backend is disabled via ISPCOK_ENABLE_<BACKEND>_BACKEND),
# the corresponding plugin is not built and the builtin placeholder module
# reports "not_supported: backend not compiled".

option(ISPCOK_ENABLE_GPU_BACKENDS "Build real GPU backend plugins when their SDK is available" ON)
option(ISPCOK_ENABLE_VULKAN_BACKEND "Build the Vulkan GPU backend plugin" ON)
option(ISPCOK_ENABLE_CUDA_BACKEND "Build the CUDA GPU backend plugin" ON)
option(ISPCOK_ENABLE_XPU_BACKEND "Build the Level Zero (xpu) backend plugin" ON)
option(ISPCOK_ENABLE_HIP_BACKEND "Build the HIP GPU backend plugin" ON)
option(ISPCOK_ENABLE_DX12_BACKEND "Build the Direct3D 12 GPU backend plugin" ON)
option(ISPCOK_ENABLE_NPU_BACKEND "Build the DXCore/DirectML NPU backend plugin" ON)

function(ispcok_configure_plugin plugin_name)
    cmake_parse_arguments(ARG "" "OUTPUT_NAME" "" ${ARGN})
    if(NOT ARG_OUTPUT_NAME)
        set(ARG_OUTPUT_NAME ${plugin_name})
    endif()
    target_include_directories(${plugin_name} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../include"
        "${CMAKE_CURRENT_SOURCE_DIR}/common"
    )
    target_compile_definitions(${plugin_name} PRIVATE ISPCOK_PLUGIN_BUILDING_DLL)
    set_target_properties(${plugin_name} PROPERTIES
        PREFIX ""
        OUTPUT_NAME "${ARG_OUTPUT_NAME}"
        POSITION_INDEPENDENT_CODE ON
    )
    if(WIN32)
        set_target_properties(${plugin_name} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        )
    endif()
    if(MSVC)
        target_compile_options(${plugin_name} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${plugin_name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

if(WIN32 AND ISPCOK_ENABLE_GPU_BACKENDS AND ISPCOK_ENABLE_DX12_BACKEND)
    add_library(ispcok_plugin_gpu_dx12 MODULE dx12/dx12_backend.cpp)
    ispcok_configure_plugin(ispcok_plugin_gpu_dx12 OUTPUT_NAME "ispcok_plugin_gpu_dx12")
    target_link_libraries(ispcok_plugin_gpu_dx12 PRIVATE d3d12 dxgi d3dcompiler)
endif()

if(WIN32 AND ISPCOK_ENABLE_GPU_BACKENDS AND ISPCOK_ENABLE_NPU_BACKEND)
    include(CheckIncludeFileCXX)
    check_include_file_cxx(dxcore.h ISPCOK_HAS_DXCORE_H)
    check_include_file_cxx(dxcore_interface.h ISPCOK_HAS_DXCORE_INTERFACE_H)
    check_include_file_cxx(directml.h ISPCOK_HAS_DIRECTML_H)
    if(ISPCOK_HAS_DXCORE_H AND ISPCOK_HAS_DXCORE_INTERFACE_H AND ISPCOK_HAS_DIRECTML_H)
        add_library(ispcok_plugin_npu MODULE npu/directml_npu_backend.cpp)
        ispcok_configure_plugin(ispcok_plugin_npu OUTPUT_NAME "ispcok_plugin_npu")
        target_link_libraries(ispcok_plugin_npu PRIVATE dxguid)
    else()
        message(STATUS "npu backend plugin disabled: DXCore and DirectML SDK headers are required")
    endif()
endif()

if(ISPCOK_ENABLE_GPU_BACKENDS AND ISPCOK_ENABLE_VULKAN_BACKEND)
    find_package(Vulkan QUIET)
    if(Vulkan_FOUND)
        add_library(ispcok_plugin_gpu_vulkan MODULE vulkan/vulkan_backend.cpp)
        ispcok_configure_plugin(ispcok_plugin_gpu_vulkan
            OUTPUT_NAME "ispcok_plugin_gpu_vulkan")
        target_link_libraries(ispcok_plugin_gpu_vulkan PRIVATE Vulkan::Vulkan)
    else()
        message(STATUS "Vulkan backend plugin disabled: Vulkan SDK not found")
    endif()
endif()

if(ISPCOK_ENABLE_GPU_BACKENDS AND ISPCOK_ENABLE_CUDA_BACKEND)
    include(CheckLanguage)
    check_language(CUDA)
    if(CMAKE_CUDA_COMPILER)
        enable_language(CUDA)
        find_package(CUDAToolkit QUIET)
        if(CUDAToolkit_FOUND AND TARGET CUDA::cudart)
            add_library(ispcok_plugin_cuda MODULE cuda/cuda_backend.cu)
            target_include_directories(ispcok_plugin_cuda PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/../include"
                "${CMAKE_CURRENT_SOURCE_DIR}/common"
            )
            target_compile_definitions(ispcok_plugin_cuda PRIVATE ISPCOK_PLUGIN_BUILDING_DLL)
            set_target_properties(ispcok_plugin_cuda PROPERTIES
                PREFIX ""
                OUTPUT_NAME "ispcok_plugin_cuda"
                POSITION_INDEPENDENT_CODE ON
            )
            target_link_libraries(ispcok_plugin_cuda PRIVATE CUDA::cudart)
        else()
            message(STATUS "CUDA backend plugin disabled: complete CUDA toolkit not found")
        endif()
    else()
        message(STATUS "CUDA backend plugin disabled: CUDA toolkit not found")
    endif()
endif()

if(ISPCOK_ENABLE_GPU_BACKENDS AND ISPCOK_ENABLE_XPU_BACKEND)
    find_path(ISPCOK_LEVEL_ZERO_INCLUDE_DIR NAMES level_zero/ze_api.h)
    find_library(ISPCOK_LEVEL_ZERO_LIBRARY NAMES ze_loader)
    if(ISPCOK_LEVEL_ZERO_INCLUDE_DIR AND ISPCOK_LEVEL_ZERO_LIBRARY)
        add_library(ispcok_plugin_xpu MODULE xpu/ze_backend.cpp)
        ispcok_configure_plugin(ispcok_plugin_xpu
            OUTPUT_NAME "ispcok_plugin_xpu")
        target_include_directories(ispcok_plugin_xpu PRIVATE "${ISPCOK_LEVEL_ZERO_INCLUDE_DIR}")
        target_link_libraries(ispcok_plugin_xpu PRIVATE ${ISPCOK_LEVEL_ZERO_LIBRARY})
    else()
        message(STATUS "xpu backend plugin disabled: Level Zero loader not found")
    endif()
endif()

if(ISPCOK_ENABLE_GPU_BACKENDS AND ISPCOK_ENABLE_HIP_BACKEND AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.21)
    include(CheckLanguage)
    check_language(HIP)
    if(CMAKE_HIP_COMPILER)
        enable_language(HIP)
        set_source_files_properties(hip/hip_backend.hip.cpp PROPERTIES LANGUAGE HIP)
        add_library(ispcok_plugin_hip MODULE hip/hip_backend.hip.cpp)
        ispcok_configure_plugin(ispcok_plugin_hip
            OUTPUT_NAME "ispcok_plugin_hip")
        find_package(hip QUIET)
        if(TARGET hip::host)
            target_link_libraries(ispcok_plugin_hip PRIVATE hip::host)
        endif()
    else()
        message(STATUS "HIP backend plugin disabled: HIP compiler not found")
    endif()
elseif(ISPCOK_ENABLE_GPU_BACKENDS AND ISPCOK_ENABLE_HIP_BACKEND)
    message(STATUS "HIP backend plugin disabled: CMake 3.21 or newer is required")
endif()
