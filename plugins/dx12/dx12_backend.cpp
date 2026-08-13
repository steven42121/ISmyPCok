#include "ispcok/plugin_api.h"

#include "../common/matmul_workload.h"
#include "../common/report_helpers.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{
using Microsoft::WRL::ComPtr;
using namespace ispcok::plugins;

thread_local ResultStorage g_storage;

constexpr char kShaderSource[] = R"(
StructuredBuffer<float> A : register(t0);
StructuredBuffer<float> B : register(t1);
RWStructuredBuffer<float> C : register(u0);

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    const uint n = 1024;
    if (id.x >= n || id.y >= n) return;
    float sum = 0.0f;
    for (uint k = 0; k < n; ++k)
        sum += A[id.y * n + k] * B[k * n + id.x];
    C[id.y * n + id.x] = sum;
}
)";

std::string HrText(const char* operation, HRESULT hr)
{
    char text[160];
    std::snprintf(text, sizeof(text), "%s failed (HRESULT 0x%08lX)", operation,
                  static_cast<unsigned long>(hr));
    return text;
}

bool WarpAllowed()
{
    char value[8]{};
    return GetEnvironmentVariableA("ISPCOK_DX12_ALLOW_WARP", value,
                                   static_cast<DWORD>(sizeof(value))) > 0 &&
           std::strcmp(value, "1") == 0;
}

std::string AdapterName(IDXGIAdapter1* adapter)
{
    DXGI_ADAPTER_DESC1 desc{};
    if (FAILED(adapter->GetDesc1(&desc)))
        return "unknown adapter";
    char name[256]{};
    WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name,
                        static_cast<int>(sizeof(name)), nullptr, nullptr);
    return name;
}

HRESULT CreateBuffer(ID3D12Device* device, UINT64 size, D3D12_HEAP_TYPE heap_type,
                     D3D12_RESOURCE_STATES state, D3D12_RESOURCE_FLAGS flags,
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
                                           state, nullptr, IID_PPV_ARGS(resource));
}

int RunDx12Backend(IsPcOkPluginResultV1* out_result)
{
    if (out_result == nullptr)
        return 1;

    ComPtr<IDXGIFactory6> factory;
    HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
    if (FAILED(hr))
    {
        FillDegradedResult(g_storage, "gpu_dx12", HrText("CreateDXGIFactory2", hr).c_str());
        *out_result = g_storage.result;
        return 0;
    }

    ComPtr<IDXGIAdapter1> adapter;
    bool diagnostic_warp = false;
    for (UINT index = 0;; ++index)
    {
        ComPtr<IDXGIAdapter1> candidate;
        hr = factory->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                 IID_PPV_ARGS(&candidate));
        if (hr == DXGI_ERROR_NOT_FOUND)
            break;
        if (FAILED(hr))
            continue;
        DXGI_ADAPTER_DESC1 desc{};
        if (FAILED(candidate->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
            continue;
        if (SUCCEEDED(D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_11_0,
                                        __uuidof(ID3D12Device), nullptr)))
        {
            adapter = candidate;
            break;
        }
    }

    if (!adapter && WarpAllowed())
    {
        hr = factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter));
        diagnostic_warp = SUCCEEDED(hr);
    }
    if (!adapter)
    {
        FillDegradedResult(g_storage, "gpu_dx12",
                           "no usable hardware D3D12 adapter; set ISPCOK_DX12_ALLOW_WARP=1 for diagnostic WARP execution");
        *out_result = g_storage.result;
        return 0;
    }

    ComPtr<ID3D12Device> device;
    hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
    if (FAILED(hr))
    {
        FillDegradedResult(g_storage, "gpu_dx12", HrText("D3D12CreateDevice", hr).c_str());
        *out_result = g_storage.result;
        return 0;
    }

    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commands;
    if (FAILED(hr = device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))) ||
        FAILED(hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&allocator))) ||
        FAILED(hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, allocator.Get(), nullptr,
                                              IID_PPV_ARGS(&commands))))
    {
        FillErrorResult(g_storage, "gpu_dx12", HrText("D3D12 command setup", hr).c_str());
        *out_result = g_storage.result;
        return 1;
    }

    ComPtr<ID3DBlob> shader;
    ComPtr<ID3DBlob> errors;
    hr = D3DCompile(kShaderSource, sizeof(kShaderSource) - 1, "gpu_dx12_gemm", nullptr, nullptr,
                    "main", "cs_5_1", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &shader, &errors);
    if (FAILED(hr))
    {
        const char* reason = errors ? static_cast<const char*>(errors->GetBufferPointer()) : "shader compilation failed";
        FillErrorResult(g_storage, "gpu_dx12", reason);
        *out_result = g_storage.result;
        return 1;
    }

    D3D12_ROOT_PARAMETER parameters[3]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    parameters[0].Descriptor.ShaderRegister = 0;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    parameters[1].Descriptor.ShaderRegister = 1;
    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    parameters[2].Descriptor.ShaderRegister = 0;
    for (auto& parameter : parameters)
        parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC root_desc{};
    root_desc.NumParameters = 3;
    root_desc.pParameters = parameters;
    ComPtr<ID3DBlob> serialized_root;
    if (FAILED(hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                               &serialized_root, &errors)))
    {
        FillErrorResult(g_storage, "gpu_dx12", HrText("D3D12SerializeRootSignature", hr).c_str());
        *out_result = g_storage.result;
        return 1;
    }
    ComPtr<ID3D12RootSignature> root_signature;
    if (FAILED(hr = device->CreateRootSignature(0, serialized_root->GetBufferPointer(),
                                               serialized_root->GetBufferSize(), IID_PPV_ARGS(&root_signature))))
    {
        FillErrorResult(g_storage, "gpu_dx12", HrText("CreateRootSignature", hr).c_str());
        *out_result = g_storage.result;
        return 1;
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc{};
    pipeline_desc.pRootSignature = root_signature.Get();
    pipeline_desc.CS = {shader->GetBufferPointer(), shader->GetBufferSize()};
    ComPtr<ID3D12PipelineState> pipeline;
    if (FAILED(hr = device->CreateComputePipelineState(&pipeline_desc, IID_PPV_ARGS(&pipeline))))
    {
        FillErrorResult(g_storage, "gpu_dx12", HrText("CreateComputePipelineState", hr).c_str());
        *out_result = g_storage.result;
        return 1;
    }

    std::vector<float> a;
    std::vector<float> b;
    FillRandomMatrices(a, b);
    const double reference_checksum = ReferenceChecksum(a, b);
    const UINT64 buffer_size = static_cast<UINT64>(a.size() * sizeof(float));
    ComPtr<ID3D12Resource> a_buffer;
    ComPtr<ID3D12Resource> b_buffer;
    ComPtr<ID3D12Resource> c_buffer;
    ComPtr<ID3D12Resource> readback;
    if (FAILED(hr = CreateBuffer(device.Get(), buffer_size, D3D12_HEAP_TYPE_UPLOAD,
                                 D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE, &a_buffer)) ||
        FAILED(hr = CreateBuffer(device.Get(), buffer_size, D3D12_HEAP_TYPE_UPLOAD,
                                 D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE, &b_buffer)) ||
        FAILED(hr = CreateBuffer(device.Get(), buffer_size, D3D12_HEAP_TYPE_DEFAULT,
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                 D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, &c_buffer)) ||
        FAILED(hr = CreateBuffer(device.Get(), buffer_size, D3D12_HEAP_TYPE_READBACK,
                                 D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_NONE, &readback)))
    {
        FillErrorResult(g_storage, "gpu_dx12", HrText("D3D12 buffer creation", hr).c_str());
        *out_result = g_storage.result;
        return 1;
    }
    void* mapped = nullptr;
    if (FAILED(a_buffer->Map(0, nullptr, &mapped)))
        hr = E_FAIL;
    else
    {
        std::memcpy(mapped, a.data(), static_cast<size_t>(buffer_size));
        a_buffer->Unmap(0, nullptr);
        if (FAILED(b_buffer->Map(0, nullptr, &mapped)))
            hr = E_FAIL;
        else
        {
            std::memcpy(mapped, b.data(), static_cast<size_t>(buffer_size));
            b_buffer->Unmap(0, nullptr);
            hr = S_OK;
        }
    }
    if (FAILED(hr))
    {
        FillErrorResult(g_storage, "gpu_dx12", "input upload mapping failed");
        *out_result = g_storage.result;
        return 1;
    }

    D3D12_QUERY_HEAP_DESC query_desc{};
    query_desc.Count = 2;
    query_desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    ComPtr<ID3D12QueryHeap> query_heap;
    ComPtr<ID3D12Resource> query_readback;
    if (FAILED(hr = device->CreateQueryHeap(&query_desc, IID_PPV_ARGS(&query_heap))) ||
        FAILED(hr = CreateBuffer(device.Get(), sizeof(UINT64) * 2, D3D12_HEAP_TYPE_READBACK,
                                 D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_NONE, &query_readback)))
    {
        FillErrorResult(g_storage, "gpu_dx12", HrText("timestamp setup", hr).c_str());
        *out_result = g_storage.result;
        return 1;
    }

    commands->SetPipelineState(pipeline.Get());
    commands->SetComputeRootSignature(root_signature.Get());
    commands->SetComputeRootShaderResourceView(0, a_buffer->GetGPUVirtualAddress());
    commands->SetComputeRootShaderResourceView(1, b_buffer->GetGPUVirtualAddress());
    commands->SetComputeRootUnorderedAccessView(2, c_buffer->GetGPUVirtualAddress());
    commands->EndQuery(query_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
    commands->Dispatch(static_cast<UINT>(kMatMulN / 16), static_cast<UINT>(kMatMulN / 16), 1);
    commands->EndQuery(query_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
    commands->ResolveQueryData(query_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2,
                               query_readback.Get(), 0);
    D3D12_RESOURCE_BARRIER uav_barrier{};
    uav_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uav_barrier.UAV.pResource = c_buffer.Get();
    commands->ResourceBarrier(1, &uav_barrier);
    D3D12_RESOURCE_BARRIER transition{};
    transition.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    transition.Transition.pResource = c_buffer.Get();
    transition.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    transition.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    transition.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commands->ResourceBarrier(1, &transition);
    commands->CopyResource(readback.Get(), c_buffer.Get());
    if (FAILED(hr = commands->Close()))
    {
        FillErrorResult(g_storage, "gpu_dx12", HrText("command list close", hr).c_str());
        *out_result = g_storage.result;
        return 1;
    }
    ID3D12CommandList* lists[] = {commands.Get()};
    queue->ExecuteCommandLists(1, lists);

    ComPtr<ID3D12Fence> fence;
    HANDLE event_handle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (event_handle == nullptr || FAILED(hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))) ||
        FAILED(hr = queue->Signal(fence.Get(), 1)) ||
        FAILED(hr = fence->SetEventOnCompletion(1, event_handle)) ||
        WaitForSingleObject(event_handle, 120000) != WAIT_OBJECT_0)
    {
        if (event_handle != nullptr)
            CloseHandle(event_handle);
        FillErrorResult(g_storage, "gpu_dx12", "D3D12 execution or synchronization failed");
        *out_result = g_storage.result;
        return 1;
    }
    CloseHandle(event_handle);

    D3D12_RANGE result_range{0, static_cast<SIZE_T>(buffer_size)};
    if (FAILED(readback->Map(0, &result_range, &mapped)))
    {
        FillErrorResult(g_storage, "gpu_dx12", "result readback mapping failed");
        *out_result = g_storage.result;
        return 1;
    }
    const double checksum = ResultChecksum(static_cast<const float*>(mapped), kMatMulN);
    readback->Unmap(0, nullptr);
    if (!ChecksumMatches(checksum, reference_checksum))
    {
        FillErrorResult(g_storage, "gpu_dx12", "FP32 GEMM checksum mismatch");
        *out_result = g_storage.result;
        return 1;
    }

    UINT64 frequency = 0;
    UINT64* timestamps = nullptr;
    D3D12_RANGE timestamp_range{0, sizeof(UINT64) * 2};
    if (FAILED(queue->GetTimestampFrequency(&frequency)) || frequency == 0 ||
        FAILED(query_readback->Map(0, &timestamp_range, reinterpret_cast<void**>(&timestamps))))
    {
        FillErrorResult(g_storage, "gpu_dx12", "timestamp readback failed");
        *out_result = g_storage.result;
        return 1;
    }
    const double elapsed_ms = static_cast<double>(timestamps[1] - timestamps[0]) * 1000.0 /
                              static_cast<double>(frequency);
    query_readback->Unmap(0, nullptr);
    if (!std::isfinite(elapsed_ms) || elapsed_ms <= 0.0)
    {
        FillErrorResult(g_storage, "gpu_dx12", "invalid GPU timestamp duration");
        *out_result = g_storage.result;
        return 1;
    }

    const double gflops = MatMulGflops(elapsed_ms / 1000.0);
    IsPcOkPluginMetricV1 metrics[] = {
        {"fp32_gflops", gflops}, {"elapsed_ms", elapsed_ms}, {"checksum", checksum},
        {"diagnostic_warp", diagnostic_warp ? 1.0 : 0.0}};
    std::string message = "gpu_dx12: FP32 GEMM on " + AdapterName(adapter.Get());
    if (diagnostic_warp)
        message += " (diagnostic WARP software adapter)";
    FillResultStorage(g_storage, GflopsScore(gflops), std::move(message), metrics, 4);
    *out_result = g_storage.result;
    return 0;
}
} // namespace

extern "C" ISPCOK_PLUGIN_EXPORT int ispcok_get_module_v1(IsPcOkPluginModuleV1* out_module)
{
    if (out_module == nullptr)
        return 1;
    out_module->id = "gpu_dx12";
    out_module->category = "gpu";
    out_module->run = &RunDx12Backend;
    return 0;
}
