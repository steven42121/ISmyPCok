#include "ispcok/plugin_api.h"

#include "../common/matmul_workload.h"
#include "../common/matmul_kernel.cuh"
#include "../common/report_helpers.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include <cuda_runtime.h>

namespace
{
using namespace ispcok::plugins;

thread_local ResultStorage g_storage;

const char* CudaErrorText(cudaError_t error)
{
    return cudaGetErrorString(error);
}

int RunCudaBackend(IsPcOkPluginResultV1* out_result)
{
    if (out_result == nullptr)
        return 1;

    int device_count = 0;
    cudaError_t error = cudaGetDeviceCount(&device_count);
    if (error != cudaSuccess)
    {
        FillDegradedResult(g_storage, "cuda", "no CUDA device available (cudaGetDeviceCount failed)");
        *out_result = g_storage.result;
        return 0;
    }
    if (device_count == 0)
    {
        FillDegradedResult(g_storage, "cuda", "no CUDA device available");
        *out_result = g_storage.result;
        return 0;
    }

    error = cudaSetDevice(0);
    if (error != cudaSuccess)
    {
        FillDegradedResult(g_storage, "cuda", "no usable CUDA device available (cudaSetDevice failed)");
        *out_result = g_storage.result;
        return 0;
    }

    std::vector<float> a;
    std::vector<float> b;
    FillRandomMatrices(a, b);
    const double reference = ReferenceChecksum(a, b);
    const std::size_t buffer_size = a.size() * sizeof(float);
    const int n = static_cast<int>(kMatMulN);

    float* d_a = nullptr;
    float* d_b = nullptr;
    float* d_c = nullptr;
    float* c = new float[kMatMulN * kMatMulN];

    auto cleanup = [&]()
    {
        if (d_a != nullptr)
            cudaFree(d_a);
        if (d_b != nullptr)
            cudaFree(d_b);
        if (d_c != nullptr)
            cudaFree(d_c);
        delete[] c;
    };

    if ((cudaMalloc(&d_a, buffer_size) != cudaSuccess) ||
        (cudaMalloc(&d_b, buffer_size) != cudaSuccess) ||
        (cudaMalloc(&d_c, buffer_size) != cudaSuccess) ||
        (cudaMemcpy(d_a, a.data(), buffer_size, cudaMemcpyHostToDevice) != cudaSuccess) ||
        (cudaMemcpy(d_b, b.data(), buffer_size, cudaMemcpyHostToDevice) != cudaSuccess))
    {
        cleanup();
        FillErrorResult(g_storage, "cuda", CudaErrorText(cudaGetLastError()));
        *out_result = g_storage.result;
        return 1;
    }

    const dim3 block(16, 16, 1);
    const dim3 grid((n + block.x - 1) / block.x, (n + block.y - 1) / block.y, 1);

    constexpr int kRepetitions = 5;
    double elapsed_ms = 0.0;
    for (int i = 0; i < kRepetitions; ++i)
    {
        cudaEvent_t start_event = nullptr;
        cudaEvent_t end_event = nullptr;
        if ((cudaEventCreate(&start_event) != cudaSuccess) ||
            (cudaEventCreate(&end_event) != cudaSuccess) ||
            (cudaEventRecord(start_event) != cudaSuccess))
        {
            if (start_event != nullptr)
                cudaEventDestroy(start_event);
            if (end_event != nullptr)
                cudaEventDestroy(end_event);
            cleanup();
            FillErrorResult(g_storage, "cuda", CudaErrorText(cudaGetLastError()));
            *out_result = g_storage.result;
            return 1;
        }
        matmul_fp32_kernel<<<grid, block>>>(d_a, d_b, d_c, n);
        error = cudaGetLastError();
        if ((error != cudaSuccess) ||
            (cudaEventRecord(end_event) != cudaSuccess) ||
            (cudaEventSynchronize(end_event) != cudaSuccess))
        {
            cudaEventDestroy(start_event);
            cudaEventDestroy(end_event);
            cleanup();
            FillErrorResult(g_storage, "cuda", CudaErrorText(error != cudaSuccess ? error : cudaGetLastError()));
            *out_result = g_storage.result;
            return 1;
        }
        float event_ms = 0.0f;
        if (cudaEventElapsedTime(&event_ms, start_event, end_event) != cudaSuccess)
        {
            cudaEventDestroy(start_event);
            cudaEventDestroy(end_event);
            cleanup();
            FillErrorResult(g_storage, "cuda", CudaErrorText(cudaGetLastError()));
            *out_result = g_storage.result;
            return 1;
        }
        elapsed_ms += static_cast<double>(event_ms);
        cudaEventDestroy(start_event);
        cudaEventDestroy(end_event);
    }
    elapsed_ms /= static_cast<double>(kRepetitions);

    if (cudaMemcpy(c, d_c, buffer_size, cudaMemcpyDeviceToHost) != cudaSuccess)
    {
        cleanup();
        FillErrorResult(g_storage, "cuda", CudaErrorText(cudaGetLastError()));
        *out_result = g_storage.result;
        return 1;
    }

    const double device_checksum = ResultChecksum(c, n);
    if (!ChecksumMatches(device_checksum, reference))
    {
        cleanup();
        FillErrorResult(g_storage, "cuda", "FP32 matrix multiplication result mismatch");
        *out_result = g_storage.result;
        return 1;
    }

    cleanup();
    const double gflops = MatMulGflops(elapsed_ms / 1000.0);
    FillGflopsResult(g_storage, elapsed_ms, gflops, device_checksum, "cuda");
    *out_result = g_storage.result;
    return 0;
}

} // namespace

extern "C" ISPCOK_PLUGIN_EXPORT int ispcok_get_module_v1(IsPcOkPluginModuleV1* out_module)
{
    if (out_module == nullptr)
        return 1;

    out_module->id = "cuda";
    out_module->category = "gpu";
    out_module->run = &RunCudaBackend;
    return 0;
}
