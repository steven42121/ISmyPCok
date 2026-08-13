#include "ispcok/plugin_api.h"

#include "../common/matmul_workload.h"
#include "../common/report_helpers.h"
#include "matmul_spv.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include <level_zero/ze_api.h>

namespace
{
using namespace ispcok::plugins;

thread_local ResultStorage g_storage;
std::mutex g_run_mutex;

ze_context_handle_t g_context = nullptr;
ze_device_handle_t g_device = nullptr;
ze_command_queue_handle_t g_queue = nullptr;
uint32_t g_compute_ordinal = 0;
ze_module_handle_t g_module = nullptr;
ze_kernel_handle_t g_kernel = nullptr;
ze_command_list_handle_t g_command_list = nullptr;

void* g_a_memory = nullptr;
void* g_b_memory = nullptr;
void* g_c_memory = nullptr;

bool g_initialized = false;
bool g_available = false;
std::string g_init_message;

void ReleaseComputeResources();
void ShutdownLevelZero();

bool InitializeLevelZero()
{
    if (g_initialized)
        return g_available;
    g_initialized = true;

    if (zeInit(0) != ZE_RESULT_SUCCESS)
    {
        g_init_message = "no usable Level Zero device available (zeInit failed)";
        return false;
    }

    uint32_t driver_count = 0;
    if ((zeDriverGet(&driver_count, nullptr) != ZE_RESULT_SUCCESS) || (driver_count == 0))
    {
        g_init_message = "no usable Level Zero device available (no drivers)";
        return false;
    }
    std::vector<ze_driver_handle_t> drivers(driver_count);
    zeDriverGet(&driver_count, drivers.data());

    ze_driver_handle_t driver = drivers[0];
    uint32_t device_count = 0;
    if ((zeDeviceGet(driver, &device_count, nullptr) != ZE_RESULT_SUCCESS) || (device_count == 0))
    {
        g_init_message = "no usable Level Zero device available (no devices)";
        return false;
    }
    std::vector<ze_device_handle_t> devices(device_count);
    zeDeviceGet(driver, &device_count, devices.data());
    g_device = devices[0];

    ze_context_desc_t context_desc{};
    context_desc.stype = ZE_STRUCTURE_TYPE_CONTEXT_DESC;
    if (zeContextCreate(driver, &context_desc, &g_context) != ZE_RESULT_SUCCESS)
    {
        g_init_message = "no usable Level Zero device available (zeContextCreate failed)";
        return false;
    }

    // Find a compute-capable command queue group.
    uint32_t group_count = 0;
    zeDeviceGetCommandQueueGroupProperties(g_device, &group_count, nullptr);
    std::vector<ze_command_queue_group_properties_t> groups(group_count);
    for (auto& group : groups)
        group.stype = ZE_STRUCTURE_TYPE_COMMAND_QUEUE_GROUP_PROPERTIES;
    zeDeviceGetCommandQueueGroupProperties(g_device, &group_count, groups.data());

    uint32_t compute_ordinal = UINT32_MAX;
    for (uint32_t i = 0; i < group_count; ++i)
    {
        if (groups[i].flags & ZE_COMMAND_QUEUE_GROUP_PROPERTY_FLAG_COMPUTE)
        {
            compute_ordinal = i;
            break;
        }
    }
    if (compute_ordinal == UINT32_MAX)
    {
        zeContextDestroy(g_context);
        g_context = nullptr;
        g_init_message = "no usable Level Zero device available (no compute queue group)";
        return false;
    }
    g_compute_ordinal = compute_ordinal;

    ze_command_queue_desc_t queue_desc{};
    queue_desc.stype = ZE_STRUCTURE_TYPE_COMMAND_QUEUE_DESC;
    queue_desc.ordinal = compute_ordinal;
    if (zeCommandQueueCreate(g_context, g_device, &queue_desc, &g_queue) != ZE_RESULT_SUCCESS)
    {
        zeContextDestroy(g_context);
        g_context = nullptr;
        g_init_message = "no usable Level Zero device available (zeCommandQueueCreate failed)";
        return false;
    }

    g_available = true;
    return true;
}

bool SetupComputeResources(const std::vector<float>& a, const std::vector<float>& b)
{
    const std::size_t buffer_size = a.size() * sizeof(float);

    ze_host_mem_alloc_desc_t host_memory_desc{};
    host_memory_desc.stype = ZE_STRUCTURE_TYPE_HOST_MEM_ALLOC_DESC;
    if ((zeMemAllocHost(g_context, &host_memory_desc, buffer_size, 0, &g_a_memory) != ZE_RESULT_SUCCESS) ||
        (zeMemAllocHost(g_context, &host_memory_desc, buffer_size, 0, &g_b_memory) != ZE_RESULT_SUCCESS) ||
        (zeMemAllocHost(g_context, &host_memory_desc, buffer_size, 0, &g_c_memory) != ZE_RESULT_SUCCESS))
        return false;

    std::memcpy(g_a_memory, a.data(), buffer_size);
    std::memcpy(g_b_memory, b.data(), buffer_size);

    ze_module_desc_t module_desc{};
    module_desc.stype = ZE_STRUCTURE_TYPE_MODULE_DESC;
    module_desc.format = ZE_MODULE_FORMAT_IL_SPIRV;
    module_desc.pInputModule = kXpuMatMulSpv;
    module_desc.inputSize = kXpuMatMulSpvSize;
    if (zeModuleCreate(g_context, g_device, &module_desc, &g_module, nullptr) != ZE_RESULT_SUCCESS)
        return false;

    ze_kernel_desc_t kernel_desc{};
    kernel_desc.stype = ZE_STRUCTURE_TYPE_KERNEL_DESC;
    kernel_desc.pKernelName = "matmul_fp32_kernel";
    if (zeKernelCreate(g_module, &kernel_desc, &g_kernel) != ZE_RESULT_SUCCESS)
        return false;

    if (zeKernelSetGroupSize(g_kernel, 16, 16, 1) != ZE_RESULT_SUCCESS)
        return false;

    if ((zeKernelSetArgumentValue(g_kernel, 0, sizeof(void*), &g_a_memory) != ZE_RESULT_SUCCESS) ||
        (zeKernelSetArgumentValue(g_kernel, 1, sizeof(void*), &g_b_memory) != ZE_RESULT_SUCCESS) ||
        (zeKernelSetArgumentValue(g_kernel, 2, sizeof(void*), &g_c_memory) != ZE_RESULT_SUCCESS))
        return false;

    const int n = static_cast<int>(kMatMulN);
    if (zeKernelSetArgumentValue(g_kernel, 3, sizeof(n), &n) != ZE_RESULT_SUCCESS)
        return false;

    ze_command_list_desc_t list_desc{};
    list_desc.stype = ZE_STRUCTURE_TYPE_COMMAND_LIST_DESC;
    list_desc.commandQueueGroupOrdinal = g_compute_ordinal;
    if (zeCommandListCreate(g_context, g_device, &list_desc, &g_command_list) != ZE_RESULT_SUCCESS)
        return false;

    return true;
}

void ReleaseComputeResources()
{
    if (g_command_list != nullptr)
        zeCommandListDestroy(g_command_list);
    if (g_kernel != nullptr)
        zeKernelDestroy(g_kernel);
    if (g_module != nullptr)
        zeModuleDestroy(g_module);
    if (g_c_memory != nullptr)
        zeMemFree(g_context, g_c_memory);
    if (g_b_memory != nullptr)
        zeMemFree(g_context, g_b_memory);
    if (g_a_memory != nullptr)
        zeMemFree(g_context, g_a_memory);
    g_command_list = nullptr;
    g_kernel = nullptr;
    g_module = nullptr;
    g_c_memory = nullptr;
    g_b_memory = nullptr;
    g_a_memory = nullptr;
}

void ShutdownLevelZero()
{
    ReleaseComputeResources();
    if (g_queue != nullptr)
        zeCommandQueueDestroy(g_queue);
    if (g_context != nullptr)
        zeContextDestroy(g_context);
    g_queue = nullptr;
    g_context = nullptr;
    g_device = nullptr;
    g_initialized = false;
    g_available = false;
}

int RunXpuBackend(IsPcOkPluginResultV1* out_result)
{
    if (out_result == nullptr)
        return 1;

    const std::lock_guard<std::mutex> lock(g_run_mutex);

    if (!InitializeLevelZero())
    {
        FillDegradedResult(g_storage, "xpu", g_init_message.c_str());
        *out_result = g_storage.result;
        return 0;
    }

    ReleaseComputeResources();

    std::vector<float> a;
    std::vector<float> b;
    FillRandomMatrices(a, b);
    const double reference = ReferenceChecksum(a, b);

    if (!SetupComputeResources(a, b))
    {
        ShutdownLevelZero();
        FillErrorResult(g_storage, "xpu", "FP32 matrix multiplication resource setup failed");
        *out_result = g_storage.result;
        return 1;
    }

    const uint32_t n = static_cast<uint32_t>(kMatMulN);
    ze_group_count_t group_count{};
    group_count.groupCountX = (n + 15) / 16;
    group_count.groupCountY = (n + 15) / 16;
    group_count.groupCountZ = 1;

    constexpr int kRepetitions = 5;
    double elapsed_ms = 0.0;
    for (int i = 0; i < kRepetitions; ++i)
    {
        if (zeCommandListAppendLaunchKernel(g_command_list, g_kernel, &group_count, nullptr, 0, nullptr) != ZE_RESULT_SUCCESS)
        {
            ShutdownLevelZero();
            FillErrorResult(g_storage, "xpu", "FP32 matrix multiplication kernel launch failed");
            *out_result = g_storage.result;
            return 1;
        }
        if (zeCommandListClose(g_command_list) != ZE_RESULT_SUCCESS)
        {
            ShutdownLevelZero();
            FillErrorResult(g_storage, "xpu", "FP32 matrix multiplication command list close failed");
            *out_result = g_storage.result;
            return 1;
        }

        const auto start = std::chrono::high_resolution_clock::now();
        if (zeCommandQueueExecuteCommandLists(g_queue, 1, &g_command_list, nullptr) != ZE_RESULT_SUCCESS)
        {
            ShutdownLevelZero();
            FillErrorResult(g_storage, "xpu", "FP32 matrix multiplication execution failed");
            *out_result = g_storage.result;
            return 1;
        }
        if (zeCommandQueueSynchronize(g_queue, UINT64_MAX) != ZE_RESULT_SUCCESS)
        {
            ShutdownLevelZero();
            FillErrorResult(g_storage, "xpu", "FP32 matrix multiplication synchronization failed");
            *out_result = g_storage.result;
            return 1;
        }
        const auto end = std::chrono::high_resolution_clock::now();
        elapsed_ms += std::chrono::duration<double, std::milli>(end - start).count();

        if (i + 1 < kRepetitions)
            zeCommandListReset(g_command_list);
    }
    elapsed_ms /= static_cast<double>(kRepetitions);

    const double device_checksum = ResultChecksum(static_cast<const float*>(g_c_memory), n);
    if (!ChecksumMatches(device_checksum, reference))
    {
        ShutdownLevelZero();
        FillErrorResult(g_storage, "xpu", "FP32 matrix multiplication result mismatch");
        *out_result = g_storage.result;
        return 1;
    }

    const double gflops = MatMulGflops(elapsed_ms / 1000.0);
    FillGflopsResult(g_storage, elapsed_ms, gflops, device_checksum, "xpu");
    ShutdownLevelZero();
    *out_result = g_storage.result;
    return 0;
}

} // namespace

extern "C" ISPCOK_PLUGIN_EXPORT int ispcok_get_module_v1(IsPcOkPluginModuleV1* out_module)
{
    if (out_module == nullptr)
        return 1;

    out_module->id = "xpu";
    out_module->category = "gpu";
    out_module->run = &RunXpuBackend;
    return 0;
}
