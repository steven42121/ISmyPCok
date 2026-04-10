#include "ispcok/plugin_api.h"

#include <chrono>
#include <cmath>
#include <cstdint>

namespace
{
IsPcOkPluginMetricV1 g_metrics[2];
IsPcOkPluginResultV1 g_result;

int RunGpuVulkanSample(IsPcOkPluginResultV1* out_result)
{
    if (out_result == nullptr)
        return 1;

    constexpr std::uint64_t iterations = 60'000'000ULL;
    volatile double acc = 0.1;
    const auto start = std::chrono::high_resolution_clock::now();
    for (std::uint64_t i = 0; i < iterations; ++i)
    {
        acc += std::sin(acc + 0.001) * 0.00001;
        if (acc > 3.14)
            acc = 0.1;
    }
    const auto end = std::chrono::high_resolution_clock::now();
    const double elapsed = std::chrono::duration<double>(end - start).count();
    const double pseudo_score = std::fmax(0.0, std::fmin(100.0, 100.0 / (elapsed + 0.3)));

    g_metrics[0] = IsPcOkPluginMetricV1{"elapsed_s", elapsed};
    g_metrics[1] = IsPcOkPluginMetricV1{"pseudo_gpu_score", pseudo_score};

    g_result.score = pseudo_score;
    g_result.message = "Sample plugin: pseudo Vulkan load (replace with real Vulkan backend)";
    g_result.metrics = g_metrics;
    g_result.metric_count = 2;

    *out_result = g_result;
    return 0;
}
} // namespace

extern "C" __declspec(dllexport) int ispcok_get_module_v1(IsPcOkPluginModuleV1* out_module)
{
    if (out_module == nullptr)
        return 1;

    out_module->id = "gpu_vulkan";
    out_module->category = "gpu";
    out_module->run = &RunGpuVulkanSample;
    return 0;
}
