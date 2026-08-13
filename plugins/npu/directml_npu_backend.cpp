#include "ispcok/plugin_api.h"

#include "../common/npu_matmul_workload.h"
#include "../common/report_helpers.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <directml.h>
#include <dxcore.h>
#include <dxcore_interface.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
using Microsoft::WRL::ComPtr;
using namespace ispcok::plugins;

thread_local ResultStorage g_storage;

struct RuntimeModules
{
    HMODULE dxcore = nullptr;
    HMODULE d3d12 = nullptr;
    HMODULE directml = nullptr;

    ~RuntimeModules()
    {
        if (directml) FreeLibrary(directml);
        if (d3d12) FreeLibrary(d3d12);
        if (dxcore) FreeLibrary(dxcore);
    }
};

using DxCoreCreateAdapterFactoryFn = HRESULT(WINAPI*)(REFIID, void**);
using D3D12CreateDeviceFn = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
using DmlCreateDevice1Fn = HRESULT(WINAPI*)(ID3D12Device*, DML_CREATE_DEVICE_FLAGS,
                                           DML_FEATURE_LEVEL, REFIID, void**);

std::string HrText(const char* operation, HRESULT hr)
{
    char text[192];
    std::snprintf(text, sizeof(text), "%s failed (HRESULT 0x%08lX)", operation,
                  static_cast<unsigned long>(hr));
    return text;
}

HRESULT CreateBuffer(ID3D12Device* device, UINT64 size, D3D12_HEAP_TYPE heap_type,
                     D3D12_RESOURCE_STATES initial_state, D3D12_RESOURCE_FLAGS flags,
                     ID3D12Resource** resource)
{
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = heap_type;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = flags;
    return device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                           initial_state, nullptr, IID_PPV_ARGS(resource));
}

HRESULT ExecuteAndWait(ID3D12Device* device, ID3D12CommandQueue* queue,
                       ID3D12GraphicsCommandList* command_list, DWORD timeout_ms)
{
    HRESULT hr = command_list->Close();
    if (FAILED(hr))
        return hr;
    ID3D12CommandList* lists[] = {command_list};
    queue->ExecuteCommandLists(1, lists);
    ComPtr<ID3D12Fence> fence;
    hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    if (FAILED(hr))
        return hr;
    HANDLE event_handle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (event_handle == nullptr)
        return HRESULT_FROM_WIN32(GetLastError());
    hr = queue->Signal(fence.Get(), 1);
    if (SUCCEEDED(hr))
        hr = fence->SetEventOnCompletion(1, event_handle);
    if (SUCCEEDED(hr) && WaitForSingleObject(event_handle, timeout_ms) != WAIT_OBJECT_0)
        hr = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    CloseHandle(event_handle);
    if (SUCCEEDED(hr))
        hr = device->GetDeviceRemovedReason();
    return hr;
}

std::string AdapterDescription(IDXCoreAdapter* adapter)
{
    if (!adapter->IsPropertySupported(DXCoreAdapterProperty::DriverDescription))
        return "unknown NPU adapter";
    size_t size = 0;
    if (FAILED(adapter->GetPropertySize(DXCoreAdapterProperty::DriverDescription, &size)) || size == 0)
        return "unknown NPU adapter";
    std::string value(size, '\0');
    if (FAILED(adapter->GetProperty(DXCoreAdapterProperty::DriverDescription, size, value.data())))
        return "unknown NPU adapter";
    while (!value.empty() && value.back() == '\0')
        value.pop_back();
    return value.empty() ? "unknown NPU adapter" : value;
}

ComPtr<IDXCoreAdapter> FindNonGraphicsAdapter(IDXCoreAdapterFactory* factory, REFGUID attribute)
{
    ComPtr<IDXCoreAdapterList> list;
    if (FAILED(factory->CreateAdapterList(1, &attribute, IID_PPV_ARGS(&list))))
        return nullptr;
    for (uint32_t index = 0; index < list->GetAdapterCount(); ++index)
    {
        ComPtr<IDXCoreAdapter> adapter;
        if (SUCCEEDED(list->GetAdapter(index, IID_PPV_ARGS(&adapter))) &&
            !adapter->IsAttributeSupported(DXCORE_ADAPTER_ATTRIBUTE_D3D12_GRAPHICS))
            return adapter;
    }
    return nullptr;
}

int RunNpuBackend(IsPcOkPluginResultV1* out_result)
{
    if (out_result == nullptr)
        return 1;

    RuntimeModules modules;
    modules.dxcore = LoadLibraryW(L"DXCore.dll");
    modules.d3d12 = LoadLibraryW(L"d3d12.dll");
    modules.directml = LoadLibraryW(L"DirectML.dll");
    if (!modules.dxcore || !modules.d3d12 || !modules.directml)
    {
        FillDegradedResult(g_storage, "npu", "DXCore, D3D12, or DirectML runtime is unavailable");
        *out_result = g_storage.result;
        return 0;
    }

    const auto create_factory = reinterpret_cast<DxCoreCreateAdapterFactoryFn>(
        GetProcAddress(modules.dxcore, "DXCoreCreateAdapterFactory"));
    const auto create_d3d12_device = reinterpret_cast<D3D12CreateDeviceFn>(
        GetProcAddress(modules.d3d12, "D3D12CreateDevice"));
    const auto create_dml_device = reinterpret_cast<DmlCreateDevice1Fn>(
        GetProcAddress(modules.directml, "DMLCreateDevice1"));
    if (!create_factory || !create_d3d12_device || !create_dml_device)
    {
        FillDegradedResult(g_storage, "npu", "required DXCore, D3D12, or DirectML entry point is unavailable");
        *out_result = g_storage.result;
        return 0;
    }

    ComPtr<IDXCoreAdapterFactory> factory;
    HRESULT hr = create_factory(IID_PPV_ARGS(&factory));
    if (FAILED(hr))
    {
        FillDegradedResult(g_storage, "npu", "DXCore adapter factory creation failed");
        *out_result = g_storage.result;
        return 0;
    }

    D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_1_0_GENERIC;
    ComPtr<IDXCoreAdapter> adapter = FindNonGraphicsAdapter(factory.Get(),
                                                            DXCORE_ADAPTER_ATTRIBUTE_D3D12_GENERIC_ML);
    if (!adapter)
    {
        feature_level = D3D_FEATURE_LEVEL_1_0_CORE;
        adapter = FindNonGraphicsAdapter(factory.Get(), DXCORE_ADAPTER_ATTRIBUTE_D3D12_CORE_COMPUTE);
    }
    if (!adapter)
    {
        FillDegradedResult(g_storage, "npu", "no non-graphics DXCore ML/Core Compute adapter is available");
        *out_result = g_storage.result;
        return 0;
    }

    const std::string adapter_name = AdapterDescription(adapter.Get());
    ComPtr<ID3D12Device1> d3d12_device;
    hr = create_d3d12_device(adapter.Get(), feature_level, IID_PPV_ARGS(&d3d12_device));
    if (FAILED(hr))
    {
        FillErrorResult(g_storage, "npu", "selected DXCore adapter rejected D3D12 device creation");
        *out_result = g_storage.result;
        return 1;
    }

    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    ComPtr<ID3D12CommandQueue> queue;
    if (FAILED(hr = d3d12_device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))))
    {
        FillErrorResult(g_storage, "npu", "selected NPU adapter rejected a D3D12 compute queue");
        *out_result = g_storage.result;
        return 1;
    }

    ComPtr<IDMLDevice> dml_device;
    hr = create_dml_device(d3d12_device.Get(), DML_CREATE_DEVICE_FLAG_NONE,
                           DML_FEATURE_LEVEL_5_0, IID_PPV_ARGS(&dml_device));
    if (FAILED(hr))
    {
        FillDegradedResult(g_storage, "npu", "non-graphics adapter does not support DirectML feature level 5.0");
        *out_result = g_storage.result;
        return 0;
    }

    constexpr UINT tensor_sizes[4] = {1, 1, static_cast<UINT>(kNpuMatMulN),
                                      static_cast<UINT>(kNpuMatMulN)};
    DML_BUFFER_TENSOR_DESC tensor_buffer_desc{};
    tensor_buffer_desc.DataType = DML_TENSOR_DATA_TYPE_FLOAT16;
    tensor_buffer_desc.Flags = DML_TENSOR_FLAG_NONE;
    tensor_buffer_desc.DimensionCount = 4;
    tensor_buffer_desc.Sizes = tensor_sizes;
    constexpr UINT64 tensor_element_count = kNpuMatMulN * kNpuMatMulN;
    tensor_buffer_desc.TotalTensorSizeInBytes = (tensor_element_count * sizeof(std::uint16_t) + 3U) & ~3ULL;
    DML_TENSOR_DESC tensor_desc{DML_TENSOR_TYPE_BUFFER, &tensor_buffer_desc};

    DML_GEMM_OPERATOR_DESC gemm_desc{};
    gemm_desc.ATensor = &tensor_desc;
    gemm_desc.BTensor = &tensor_desc;
    gemm_desc.OutputTensor = &tensor_desc;
    gemm_desc.TransA = DML_MATRIX_TRANSFORM_NONE;
    gemm_desc.TransB = DML_MATRIX_TRANSFORM_NONE;
    gemm_desc.Alpha = 1.0f;
    DML_OPERATOR_DESC operator_desc{DML_OPERATOR_GEMM, &gemm_desc};

    ComPtr<IDMLOperator> gemm_operator;
    if (FAILED(hr = dml_device->CreateOperator(&operator_desc, IID_PPV_ARGS(&gemm_operator))))
    {
        FillErrorResult(g_storage, "npu", HrText("DirectML GEMM operator creation", hr).c_str());
        *out_result = g_storage.result;
        return 1;
    }
    ComPtr<IDMLCompiledOperator> compiled_operator;
    if (FAILED(hr = dml_device->CompileOperator(gemm_operator.Get(), DML_EXECUTION_FLAG_NONE,
                                                IID_PPV_ARGS(&compiled_operator))))
    {
        FillErrorResult(g_storage, "npu", HrText("DirectML GEMM compilation", hr).c_str());
        *out_result = g_storage.result;
        return 1;
    }
    IDMLCompiledOperator* operators[] = {compiled_operator.Get()};
    ComPtr<IDMLOperatorInitializer> initializer;
    if (FAILED(hr = dml_device->CreateOperatorInitializer(1, operators, IID_PPV_ARGS(&initializer))))
    {
        FillErrorResult(g_storage, "npu", HrText("DirectML operator initializer creation", hr).c_str());
        *out_result = g_storage.result;
        return 1;
    }

    const DML_BINDING_PROPERTIES init_properties = initializer->GetBindingProperties();
    const DML_BINDING_PROPERTIES execute_properties = compiled_operator->GetBindingProperties();
    const UINT descriptor_count = std::max<UINT>(1, std::max(
        init_properties.RequiredDescriptorCount, execute_properties.RequiredDescriptorCount));
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap_desc.NumDescriptors = descriptor_count;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> descriptor_heap;
    if (FAILED(hr = d3d12_device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&descriptor_heap))))
    {
        FillErrorResult(g_storage, "npu", HrText("DirectML descriptor heap creation", hr).c_str());
        *out_result = g_storage.result;
        return 1;
    }

    DML_BINDING_TABLE_DESC binding_table_desc{};
    binding_table_desc.Dispatchable = initializer.Get();
    binding_table_desc.CPUDescriptorHandle = descriptor_heap->GetCPUDescriptorHandleForHeapStart();
    binding_table_desc.GPUDescriptorHandle = descriptor_heap->GetGPUDescriptorHandleForHeapStart();
    binding_table_desc.SizeInDescriptors = descriptor_count;
    ComPtr<IDMLBindingTable> binding_table;
    if (FAILED(hr = dml_device->CreateBindingTable(&binding_table_desc, IID_PPV_ARGS(&binding_table))))
    {
        FillErrorResult(g_storage, "npu", HrText("DirectML binding table creation", hr).c_str());
        *out_result = g_storage.result;
        return 1;
    }

    const UINT64 temporary_size = std::max(init_properties.TemporaryResourceSize,
                                            execute_properties.TemporaryResourceSize);
    const UINT64 persistent_size = execute_properties.PersistentResourceSize;
    ComPtr<ID3D12Resource> temporary_buffer;
    ComPtr<ID3D12Resource> persistent_buffer;
    if ((temporary_size > 0 && FAILED(hr = CreateBuffer(d3d12_device.Get(), temporary_size,
            D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, &temporary_buffer))) ||
        (persistent_size > 0 && FAILED(hr = CreateBuffer(d3d12_device.Get(), persistent_size,
            D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, &persistent_buffer))))
    {
        FillErrorResult(g_storage, "npu", HrText("DirectML working resource creation", hr).c_str());
        *out_result = g_storage.result;
        return 1;
    }
    if (init_properties.TemporaryResourceSize > 0)
    {
        DML_BUFFER_BINDING buffer{temporary_buffer.Get(), 0, temporary_size};
        DML_BINDING_DESC binding{DML_BINDING_TYPE_BUFFER, &buffer};
        binding_table->BindTemporaryResource(&binding);
    }
    if (persistent_size > 0)
    {
        DML_BUFFER_BINDING buffer{persistent_buffer.Get(), 0, persistent_size};
        DML_BINDING_DESC binding{DML_BINDING_TYPE_BUFFER, &buffer};
        binding_table->BindOutputs(1, &binding);
    }

    ComPtr<IDMLCommandRecorder> recorder;
    ComPtr<ID3D12CommandAllocator> command_allocator;
    ComPtr<ID3D12GraphicsCommandList> command_list;
    if (FAILED(hr = dml_device->CreateCommandRecorder(IID_PPV_ARGS(&recorder))) ||
        FAILED(hr = d3d12_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE,
                                                         IID_PPV_ARGS(&command_allocator))) ||
        FAILED(hr = d3d12_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE,
                                                    command_allocator.Get(), nullptr,
                                                    IID_PPV_ARGS(&command_list))))
    {
        FillErrorResult(g_storage, "npu", HrText("DirectML command setup", hr).c_str());
        *out_result = g_storage.result;
        return 1;
    }
    ID3D12DescriptorHeap* heaps[] = {descriptor_heap.Get()};
    command_list->SetDescriptorHeaps(1, heaps);
    recorder->RecordDispatch(command_list.Get(), initializer.Get(), binding_table.Get());
    if (FAILED(hr = ExecuteAndWait(d3d12_device.Get(), queue.Get(), command_list.Get(), 120000)))
    {
        FillErrorResult(g_storage, "npu", HrText("DirectML operator initialization", hr).c_str());
        *out_result = g_storage.result;
        return 1;
    }

    if (FAILED(hr = command_allocator->Reset()) ||
        FAILED(hr = command_list->Reset(command_allocator.Get(), nullptr)))
    {
        FillErrorResult(g_storage, "npu", HrText("DirectML execution command reset", hr).c_str());
        *out_result = g_storage.result;
        return 1;
    }
    command_list->SetDescriptorHeaps(1, heaps);
    binding_table_desc.Dispatchable = compiled_operator.Get();
    if (FAILED(hr = binding_table->Reset(&binding_table_desc)))
    {
        FillErrorResult(g_storage, "npu", HrText("DirectML execution binding reset", hr).c_str());
        *out_result = g_storage.result;
        return 1;
    }
    if (execute_properties.TemporaryResourceSize > 0)
    {
        DML_BUFFER_BINDING buffer{temporary_buffer.Get(), 0, temporary_size};
        DML_BINDING_DESC binding{DML_BINDING_TYPE_BUFFER, &buffer};
        binding_table->BindTemporaryResource(&binding);
    }
    if (persistent_size > 0)
    {
        DML_BUFFER_BINDING buffer{persistent_buffer.Get(), 0, persistent_size};
        DML_BINDING_DESC binding{DML_BINDING_TYPE_BUFFER, &buffer};
        binding_table->BindPersistentResource(&binding);
    }

    std::vector<float> matrix_a;
    std::vector<float> matrix_b;
    FillNpuMatrices(matrix_a, matrix_b);
    const double reference_checksum = NpuReferenceChecksum(matrix_a, matrix_b);
    const std::vector<std::uint16_t> matrix_a_half = NpuMatrixToHalf(matrix_a);
    const std::vector<std::uint16_t> matrix_b_half = NpuMatrixToHalf(matrix_b);
    const UINT64 tensor_size = tensor_buffer_desc.TotalTensorSizeInBytes;
    ComPtr<ID3D12Resource> upload_buffer;
    ComPtr<ID3D12Resource> a_buffer;
    ComPtr<ID3D12Resource> b_buffer;
    ComPtr<ID3D12Resource> output_buffer;
    ComPtr<ID3D12Resource> readback_buffer;
    if (FAILED(hr = CreateBuffer(d3d12_device.Get(), tensor_size * 2, D3D12_HEAP_TYPE_UPLOAD,
                                 D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE,
                                 &upload_buffer)) ||
        FAILED(hr = CreateBuffer(d3d12_device.Get(), tensor_size, D3D12_HEAP_TYPE_DEFAULT,
                                 D3D12_RESOURCE_STATE_COPY_DEST,
                                 D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, &a_buffer)) ||
        FAILED(hr = CreateBuffer(d3d12_device.Get(), tensor_size, D3D12_HEAP_TYPE_DEFAULT,
                                 D3D12_RESOURCE_STATE_COPY_DEST,
                                 D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, &b_buffer)) ||
        FAILED(hr = CreateBuffer(d3d12_device.Get(), tensor_size, D3D12_HEAP_TYPE_DEFAULT,
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                 D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, &output_buffer)) ||
        FAILED(hr = CreateBuffer(d3d12_device.Get(), tensor_size, D3D12_HEAP_TYPE_READBACK,
                                 D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_NONE,
                                 &readback_buffer)))
    {
        FillErrorResult(g_storage, "npu", HrText("DirectML tensor resource creation", hr).c_str());
        *out_result = g_storage.result;
        return 1;
    }
    void* mapped = nullptr;
    if (FAILED(hr = upload_buffer->Map(0, nullptr, &mapped)))
    {
        FillErrorResult(g_storage, "npu", HrText("DirectML tensor upload mapping", hr).c_str());
        *out_result = g_storage.result;
        return 1;
    }
    std::memcpy(mapped, matrix_a_half.data(), matrix_a_half.size() * sizeof(std::uint16_t));
    std::memcpy(static_cast<unsigned char*>(mapped) + tensor_size,
                matrix_b_half.data(), matrix_b_half.size() * sizeof(std::uint16_t));
    upload_buffer->Unmap(0, nullptr);
    command_list->CopyBufferRegion(a_buffer.Get(), 0, upload_buffer.Get(), 0, tensor_size);
    command_list->CopyBufferRegion(b_buffer.Get(), 0, upload_buffer.Get(), tensor_size, tensor_size);
    D3D12_RESOURCE_BARRIER input_barriers[2]{};
    for (D3D12_RESOURCE_BARRIER& barrier : input_barriers)
    {
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    }
    input_barriers[0].Transition.pResource = a_buffer.Get();
    input_barriers[1].Transition.pResource = b_buffer.Get();
    command_list->ResourceBarrier(2, input_barriers);
    if (FAILED(hr = ExecuteAndWait(d3d12_device.Get(), queue.Get(), command_list.Get(), 120000)) ||
        FAILED(hr = command_allocator->Reset()) ||
        FAILED(hr = command_list->Reset(command_allocator.Get(), nullptr)))
    {
        FillErrorResult(g_storage, "npu", HrText("DirectML input upload", hr).c_str());
        *out_result = g_storage.result;
        return 1;
    }
    command_list->SetDescriptorHeaps(1, heaps);

    DML_BUFFER_BINDING a_binding{a_buffer.Get(), 0, tensor_size};
    DML_BUFFER_BINDING b_binding{b_buffer.Get(), 0, tensor_size};
    DML_BINDING_DESC input_bindings[3] = {
        {DML_BINDING_TYPE_BUFFER, &a_binding},
        {DML_BINDING_TYPE_BUFFER, &b_binding},
        {DML_BINDING_TYPE_NONE, nullptr}};
    binding_table->BindInputs(3, input_bindings);
    DML_BUFFER_BINDING output_binding{output_buffer.Get(), 0, tensor_size};
    DML_BINDING_DESC output_binding_desc{DML_BINDING_TYPE_BUFFER, &output_binding};
    binding_table->BindOutputs(1, &output_binding_desc);

    recorder->RecordDispatch(command_list.Get(), compiled_operator.Get(), binding_table.Get());
    if (FAILED(hr = ExecuteAndWait(d3d12_device.Get(), queue.Get(), command_list.Get(), 120000)) ||
        FAILED(hr = command_allocator->Reset()) ||
        FAILED(hr = command_list->Reset(command_allocator.Get(), nullptr)))
    {
        FillErrorResult(g_storage, "npu", HrText("DirectML GEMM warm-up", hr).c_str());
        *out_result = g_storage.result;
        return 1;
    }
    command_list->SetDescriptorHeaps(1, heaps);

    constexpr UINT repetitions = 5;
    for (UINT repetition = 0; repetition < repetitions; ++repetition)
    {
        recorder->RecordDispatch(command_list.Get(), compiled_operator.Get(), binding_table.Get());
        D3D12_RESOURCE_BARRIER uav_barrier{};
        uav_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uav_barrier.UAV.pResource = output_buffer.Get();
        command_list->ResourceBarrier(1, &uav_barrier);
    }
    const auto start = std::chrono::steady_clock::now();
    if (FAILED(hr = ExecuteAndWait(d3d12_device.Get(), queue.Get(), command_list.Get(), 120000)))
    {
        FillErrorResult(g_storage, "npu", HrText("DirectML GEMM execution", hr).c_str());
        *out_result = g_storage.result;
        return 1;
    }
    const auto end = std::chrono::steady_clock::now();
    const double elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count() /
                              static_cast<double>(repetitions);

    if (FAILED(hr = command_allocator->Reset()) ||
        FAILED(hr = command_list->Reset(command_allocator.Get(), nullptr)))
    {
        FillErrorResult(g_storage, "npu", HrText("DirectML readback command reset", hr).c_str());
        *out_result = g_storage.result;
        return 1;
    }
    D3D12_RESOURCE_BARRIER output_transition{};
    output_transition.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    output_transition.Transition.pResource = output_buffer.Get();
    output_transition.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    output_transition.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    output_transition.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command_list->ResourceBarrier(1, &output_transition);
    command_list->CopyResource(readback_buffer.Get(), output_buffer.Get());
    if (FAILED(hr = ExecuteAndWait(d3d12_device.Get(), queue.Get(), command_list.Get(), 120000)))
    {
        FillErrorResult(g_storage, "npu", HrText("DirectML result copy", hr).c_str());
        *out_result = g_storage.result;
        return 1;
    }

    D3D12_RANGE read_range{0, static_cast<SIZE_T>(tensor_size)};
    if (FAILED(hr = readback_buffer->Map(0, &read_range, &mapped)))
    {
        FillErrorResult(g_storage, "npu", HrText("DirectML result readback", hr).c_str());
        *out_result = g_storage.result;
        return 1;
    }
    const double checksum = NpuResultChecksum(static_cast<const std::uint16_t*>(mapped));
    D3D12_RANGE written_range{0, 0};
    readback_buffer->Unmap(0, &written_range);
    if (!NpuChecksumMatches(checksum, reference_checksum))
    {
        FillErrorResult(g_storage, "npu", "DirectML FP16 GEMM checksum mismatch");
        *out_result = g_storage.result;
        return 1;
    }

    const double gflops = NpuMatMulGflops(elapsed_ms);
    IsPcOkPluginMetricV1 metrics[] = {
        {"fp16_gflops", gflops}, {"elapsed_ms", elapsed_ms}, {"checksum", checksum},
        {"matrix_size", static_cast<double>(kNpuMatMulN)}};
    const std::string message = "npu: DirectML FP16 GEMM on " + adapter_name;
    FillResultStorage(g_storage, GflopsScore(gflops), message, metrics, 4);
    *out_result = g_storage.result;
    return 0;
}
} // namespace

extern "C" ISPCOK_PLUGIN_EXPORT int ispcok_get_module_v1(IsPcOkPluginModuleV1* out_module)
{
    if (out_module == nullptr)
        return 1;
    out_module->id = "npu";
    out_module->category = "npu";
    out_module->run = &RunNpuBackend;
    return 0;
}
