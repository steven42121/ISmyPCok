#include "ispcok/plugin_api.h"

#include "../common/matmul_workload.h"
#include "../common/matmul_kernel.cuh"
#include "../common/report_helpers.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include <hip/hip_runtime.h>

namespace
{
using namespace ispcok::plugins;

thread_local ResultStorage g_storage;

const char* HipErrorText(hipError_t error)
{
    return hipGetErrorString(error);
}

int RunHipBackend(IsPcOkPluginResultV1* out_result)
{
    if (out_result == nullptr)
        return 1;

    int device_count = 0;
    hipError_t error = hipGetDeviceCount(&device_count);
    if (error != hipSuccess)
    {
        FillDegradedResult(g_storage, "hip", "no HIP device available (hipGetDeviceCount failed)");
        *out_result = g_storage.result;
        return 0;
    }
    if (device_count == 0)
    {
        FillDegradedResult(g_storage, "hip", "no HIP device available");
        *out_result = g_storage.result;
        return 0;
    }

    error = hipSetDevice(0);
    if (error != hipSuccess)
    {
        FillDegradedResult(g_storage, "hip", "no usable HIP device available (hipSetDevice failed)");
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
            hipFree(d_a);
        if (d_b != nullptr)
            hipFree(d_b);
        if (d_c != nullptr)
            hipFree(d_c);
        delete[] c;
    };

    if ((hipMalloc(&d_a, buffer_size) != hipSuccess) ||
        (hipMalloc(&d_b, buffer_size) != hipSuccess) ||
        (hipMalloc(&d_c, buffer_size) != hipSuccess) ||
        (hipMemcpy(d_a, a.data(), buffer_size, hipMemcpyHostToDevice) != hipSuccess) ||
        (hipMemcpy(d_b, b.data(), buffer_size, hipMemcpyHostToDevice) != hipSuccess))
    {
        cleanup();
        FillErrorResult(g_storage, "hip", HipErrorText(hipGetLastError()));
        *out_result = g_storage.result;
        return 1;
    }

    const dim3 block(16, 16, 1);
    const dim3 grid((n + block.x - 1) / block.x, (n + block.y - 1) / block.y, 1);

    constexpr int kRepetitions = 5;
    double elapsed_ms = 0.0;
    for (int i = 0; i < kRepetitions; ++i)
    {
        hipEvent_t start_event = nullptr;
        hipEvent_t end_event = nullptr;
        if ((hipEventCreate(&start_event) != hipSuccess) ||
            (hipEventCreate(&end_event) != hipSuccess) ||
            (hipEventRecord(start_event) != hipSuccess))
        {
            if (start_event != nullptr)
                hipEventDestroy(start_event);
            if (end_event != nullptr)
                hipEventDestroy(end_event);
            cleanup();
            FillErrorResult(g_storage, "hip", HipErrorText(hipGetLastError()));
            *out_result = g_storage.result;
            return 1;
        }
        matmul_fp32_kernel<<<grid, block>>>(d_a, d_b, d_c, n);
        error = hipGetLastError();
        if ((error != hipSuccess) ||
            (hipEventRecord(end_event) != hipSuccess) ||
            (hipEventSynchronize(end_event) != hipSuccess))
        {
            hipEventDestroy(start_event);
            hipEventDestroy(end_event);
            cleanup();
            FillErrorResult(g_storage, "hip", HipErrorText(error != hipSuccess ? error : hipGetLastError()));
            *out_result = g_storage.result;
            return 1;
        }
        float event_ms = 0.0f;
        if (hipEventElapsedTime(&event_ms, start_event, end_event) != hipSuccess)
        {
            hipEventDestroy(start_event);
            hipEventDestroy(end_event);
            cleanup();
            FillErrorResult(g_storage, "hip", HipErrorText(hipGetLastError()));
            *out_result = g_storage.result;
            return 1;
        }
        elapsed_ms += static_cast<double>(event_ms);
        hipEventDestroy(start_event);
        hipEventDestroy(end_event);
    }
    elapsed_ms /= static_cast<double>(kRepetitions);

    if (hipMemcpy(c, d_c, buffer_size, hipMemcpyDeviceToHost) != hipSuccess)
    {
        cleanup();
        FillErrorResult(g_storage, "hip", HipErrorText(hipGetLastError()));
        *out_result = g_storage.result;
        return 1;
    }

    const double device_checksum = ResultChecksum(c, n);
    if (!ChecksumMatches(device_checksum, reference))
    {
        cleanup();
        FillErrorResult(g_storage, "hip", "FP32 matrix multiplication result mismatch");
        *out_result = g_storage.result;
        return 1;
    }

    cleanup();
    const double gflops = MatMulGflops(elapsed_ms / 1000.0);
    FillGflopsResult(g_storage, elapsed_ms, gflops, device_checksum, "hip");
    *out_result = g_storage.result;
    return 0;
}

} // namespace

extern "C" ISPCOK_PLUGIN_EXPORT int ispcok_get_module_v1(IsPcOkPluginModuleV1* out_module)
{
    if (out_module == nullptr)
        return 1;

    out_module->id = "hip";
    out_module->category = "gpu";
    out_module->run = &RunHipBackend;
    return 0;
}
