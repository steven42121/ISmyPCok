#include "ispcok/plugin_api.h"

#include "../common/matmul_workload.h"
#include "../common/matmul_spv.h"
#include "../common/report_helpers.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

#include <vulkan/vulkan.h>

namespace
{
using namespace ispcok::plugins;

thread_local ResultStorage g_storage;
std::mutex g_run_mutex;

VkInstance g_instance = VK_NULL_HANDLE;
VkPhysicalDevice g_physical_device = VK_NULL_HANDLE;
VkDevice g_device = VK_NULL_HANDLE;
VkQueue g_queue = VK_NULL_HANDLE;
uint32_t g_queue_family = 0;

VkBuffer g_a_buffer = VK_NULL_HANDLE;
VkBuffer g_b_buffer = VK_NULL_HANDLE;
VkBuffer g_c_buffer = VK_NULL_HANDLE;
VkDeviceMemory g_a_memory = VK_NULL_HANDLE;
VkDeviceMemory g_b_memory = VK_NULL_HANDLE;
VkDeviceMemory g_c_memory = VK_NULL_HANDLE;

VkShaderModule g_shader_module = VK_NULL_HANDLE;
VkDescriptorSetLayout g_descriptor_layout = VK_NULL_HANDLE;
VkDescriptorPool g_descriptor_pool = VK_NULL_HANDLE;
VkDescriptorSet g_descriptor_set = VK_NULL_HANDLE;
VkPipelineLayout g_pipeline_layout = VK_NULL_HANDLE;
VkPipeline g_pipeline = VK_NULL_HANDLE;
VkCommandPool g_command_pool = VK_NULL_HANDLE;
VkCommandBuffer g_command_buffer = VK_NULL_HANDLE;

bool g_initialized = false;
bool g_available = false;
std::string g_init_message;

void ReleaseComputeResources();
void ShutdownVulkan();

struct VulkanDeviceInfo
{
    VkPhysicalDeviceProperties properties{};
};

bool InitializeVulkan()
{
    if (g_initialized)
        return g_available;
    g_initialized = true;

    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "ISmyPCok";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "ISmyPCok";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_0;

    const char* layer = "VK_LAYER_KHRONOS_validation";
    uint32_t layer_count = 0;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
    std::vector<VkLayerProperties> layers(layer_count);
    vkEnumerateInstanceLayerProperties(&layer_count, layers.data());
    bool has_validation = false;
    for (const auto& layer_props : layers)
    {
        if (std::strcmp(layer_props.layerName, layer) == 0)
        {
            has_validation = true;
            break;
        }
    }

    VkInstanceCreateInfo instance_info{};
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = &app_info;
    const char* layers_to_enable = layer;
    if (has_validation)
    {
        instance_info.enabledLayerCount = 1;
        instance_info.ppEnabledLayerNames = &layers_to_enable;
    }

    if (vkCreateInstance(&instance_info, nullptr, &g_instance) != VK_SUCCESS)
    {
        g_init_message = "no usable Vulkan device available (vkCreateInstance failed)";
        return false;
    }

    uint32_t device_count = 0;
    if ((vkEnumeratePhysicalDevices(g_instance, &device_count, nullptr) != VK_SUCCESS) || (device_count == 0))
    {
        vkDestroyInstance(g_instance, nullptr);
        g_instance = VK_NULL_HANDLE;
        g_init_message = "no usable Vulkan device available (no physical devices)";
        return false;
    }
    std::vector<VkPhysicalDevice> devices(device_count);
    vkEnumeratePhysicalDevices(g_instance, &device_count, devices.data());

    // Prefer a discrete GPU, otherwise fall back to the first device
    // (including software devices such as lavapipe).
    g_physical_device = devices[0];
    VulkanDeviceInfo info;
    vkGetPhysicalDeviceProperties(g_physical_device, &info.properties);
    for (const auto& candidate : devices)
    {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(candidate, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        {
            g_physical_device = candidate;
            info.properties = props;
            break;
        }
    }

    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(g_physical_device, &queue_family_count, nullptr);
    std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(g_physical_device, &queue_family_count, queue_families.data());

    bool found_queue_family = false;
    for (uint32_t i = 0; i < queue_family_count; ++i)
    {
        if (queue_families[i].queueFlags & VK_QUEUE_COMPUTE_BIT)
        {
            g_queue_family = i;
            found_queue_family = true;
            break;
        }
    }
    if (!found_queue_family)
    {
        vkDestroyInstance(g_instance, nullptr);
        g_instance = VK_NULL_HANDLE;
        g_init_message = "no usable Vulkan device available (no compute queue family)";
        return false;
    }

    const float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info{};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = g_queue_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &queue_priority;

    VkDeviceCreateInfo device_info{};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;

    if (vkCreateDevice(g_physical_device, &device_info, nullptr, &g_device) != VK_SUCCESS)
    {
        vkDestroyInstance(g_instance, nullptr);
        g_instance = VK_NULL_HANDLE;
        g_init_message = "no usable Vulkan device available (vkCreateDevice failed)";
        return false;
    }
    vkGetDeviceQueue(g_device, g_queue_family, 0, &g_queue);

    g_available = true;
    return true;
}

bool CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer* buffer, VkDeviceMemory* memory)
{
    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(g_device, &buffer_info, nullptr, buffer) != VK_SUCCESS)
        return false;

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(g_device, *buffer, &requirements);

    VkPhysicalDeviceMemoryProperties memory_properties{};
    vkGetPhysicalDeviceMemoryProperties(g_physical_device, &memory_properties);

    uint32_t memory_type = UINT32_MAX;
    const VkMemoryPropertyFlags desired = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i)
    {
        if ((requirements.memoryTypeBits & (1u << i)) && ((memory_properties.memoryTypes[i].propertyFlags & desired) == desired))
        {
            memory_type = i;
            break;
        }
    }
    if (memory_type == UINT32_MAX)
    {
        vkDestroyBuffer(g_device, *buffer, nullptr);
        *buffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo allocate_info{};
    allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate_info.allocationSize = requirements.size;
    allocate_info.memoryTypeIndex = memory_type;
    if (vkAllocateMemory(g_device, &allocate_info, nullptr, memory) != VK_SUCCESS)
    {
        vkDestroyBuffer(g_device, *buffer, nullptr);
        *buffer = VK_NULL_HANDLE;
        return false;
    }
    if (vkBindBufferMemory(g_device, *buffer, *memory, 0) != VK_SUCCESS)
    {
        vkFreeMemory(g_device, *memory, nullptr);
        vkDestroyBuffer(g_device, *buffer, nullptr);
        *memory = VK_NULL_HANDLE;
        *buffer = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

void ReleaseComputeResources()
{
    if (g_device == VK_NULL_HANDLE)
        return;
    if (g_command_pool != VK_NULL_HANDLE)
        vkDestroyCommandPool(g_device, g_command_pool, nullptr);
    if (g_pipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(g_device, g_pipeline, nullptr);
    if (g_pipeline_layout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(g_device, g_pipeline_layout, nullptr);
    if (g_descriptor_pool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(g_device, g_descriptor_pool, nullptr);
    if (g_descriptor_layout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(g_device, g_descriptor_layout, nullptr);
    if (g_shader_module != VK_NULL_HANDLE)
        vkDestroyShaderModule(g_device, g_shader_module, nullptr);
    if (g_c_buffer != VK_NULL_HANDLE)
        vkDestroyBuffer(g_device, g_c_buffer, nullptr);
    if (g_b_buffer != VK_NULL_HANDLE)
        vkDestroyBuffer(g_device, g_b_buffer, nullptr);
    if (g_a_buffer != VK_NULL_HANDLE)
        vkDestroyBuffer(g_device, g_a_buffer, nullptr);
    if (g_c_memory != VK_NULL_HANDLE)
        vkFreeMemory(g_device, g_c_memory, nullptr);
    if (g_b_memory != VK_NULL_HANDLE)
        vkFreeMemory(g_device, g_b_memory, nullptr);
    if (g_a_memory != VK_NULL_HANDLE)
        vkFreeMemory(g_device, g_a_memory, nullptr);
    g_command_pool = VK_NULL_HANDLE;
    g_pipeline = VK_NULL_HANDLE;
    g_pipeline_layout = VK_NULL_HANDLE;
    g_descriptor_pool = VK_NULL_HANDLE;
    g_descriptor_layout = VK_NULL_HANDLE;
    g_shader_module = VK_NULL_HANDLE;
    g_c_memory = VK_NULL_HANDLE;
    g_b_memory = VK_NULL_HANDLE;
    g_a_memory = VK_NULL_HANDLE;
    g_c_buffer = VK_NULL_HANDLE;
    g_b_buffer = VK_NULL_HANDLE;
    g_a_buffer = VK_NULL_HANDLE;
}

void ShutdownVulkan()
{
    if (g_device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(g_device);
        ReleaseComputeResources();
        vkDestroyDevice(g_device, nullptr);
    }
    if (g_instance != VK_NULL_HANDLE)
        vkDestroyInstance(g_instance, nullptr);
    g_device = VK_NULL_HANDLE;
    g_instance = VK_NULL_HANDLE;
    g_queue = VK_NULL_HANDLE;
    g_physical_device = VK_NULL_HANDLE;
    g_initialized = false;
    g_available = false;
}

bool SetupComputeResources(const std::vector<float>& a, const std::vector<float>& b)
{
    const VkDeviceSize buffer_size = static_cast<VkDeviceSize>(a.size()) * sizeof(float);

    if (!CreateBuffer(buffer_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &g_a_buffer, &g_a_memory))
        return false;
    if (!CreateBuffer(buffer_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &g_b_buffer, &g_b_memory))
        return false;
    if (!CreateBuffer(buffer_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &g_c_buffer, &g_c_memory))
        return false;

    void* data = nullptr;
    if (vkMapMemory(g_device, g_a_memory, 0, buffer_size, 0, &data) != VK_SUCCESS)
        return false;
    std::memcpy(data, a.data(), buffer_size);
    vkUnmapMemory(g_device, g_a_memory);
    if (vkMapMemory(g_device, g_b_memory, 0, buffer_size, 0, &data) != VK_SUCCESS)
        return false;
    std::memcpy(data, b.data(), buffer_size);
    vkUnmapMemory(g_device, g_b_memory);

    VkShaderModuleCreateInfo shader_info{};
    shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader_info.codeSize = kMatMulSpvSize;
    shader_info.pCode = reinterpret_cast<const uint32_t*>(kMatMulSpv);
    if (vkCreateShaderModule(g_device, &shader_info, nullptr, &g_shader_module) != VK_SUCCESS)
        return false;

    VkDescriptorSetLayoutBinding bindings[3] = {};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 3;
    layout_info.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(g_device, &layout_info, nullptr, &g_descriptor_layout) != VK_SUCCESS)
        return false;

    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    pool_size.descriptorCount = 3;
    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    pool_info.maxSets = 1;
    if (vkCreateDescriptorPool(g_device, &pool_info, nullptr, &g_descriptor_pool) != VK_SUCCESS)
        return false;

    VkDescriptorSetAllocateInfo set_info{};
    set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    set_info.descriptorPool = g_descriptor_pool;
    set_info.descriptorSetCount = 1;
    set_info.pSetLayouts = &g_descriptor_layout;
    if (vkAllocateDescriptorSets(g_device, &set_info, &g_descriptor_set) != VK_SUCCESS)
        return false;

    VkDescriptorBufferInfo a_desc{};
    a_desc.buffer = g_a_buffer;
    a_desc.offset = 0;
    a_desc.range = buffer_size;
    VkDescriptorBufferInfo b_desc{};
    b_desc.buffer = g_b_buffer;
    b_desc.offset = 0;
    b_desc.range = buffer_size;
    VkDescriptorBufferInfo c_desc{};
    c_desc.buffer = g_c_buffer;
    c_desc.offset = 0;
    c_desc.range = buffer_size;

    VkWriteDescriptorSet writes[3] = {};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = g_descriptor_set;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].pBufferInfo = &a_desc;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = g_descriptor_set;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].pBufferInfo = &b_desc;
    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = g_descriptor_set;
    writes[2].dstBinding = 2;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].pBufferInfo = &c_desc;
    vkUpdateDescriptorSets(g_device, 3, writes, 0, nullptr);

    VkPipelineLayoutCreateInfo pipeline_layout_info{};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &g_descriptor_layout;
    if (vkCreatePipelineLayout(g_device, &pipeline_layout_info, nullptr, &g_pipeline_layout) != VK_SUCCESS)
        return false;

    VkComputePipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline_info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeline_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeline_info.stage.module = g_shader_module;
    pipeline_info.stage.pName = "main";
    pipeline_info.layout = g_pipeline_layout;
    if (vkCreateComputePipelines(g_device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &g_pipeline) != VK_SUCCESS)
        return false;

    VkCommandPoolCreateInfo command_pool_info{};
    command_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    command_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    command_pool_info.queueFamilyIndex = g_queue_family;
    if (vkCreateCommandPool(g_device, &command_pool_info, nullptr, &g_command_pool) != VK_SUCCESS)
        return false;

    VkCommandBufferAllocateInfo command_buffer_info{};
    command_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_buffer_info.commandPool = g_command_pool;
    command_buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_buffer_info.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(g_device, &command_buffer_info, &g_command_buffer) != VK_SUCCESS)
        return false;

    return true;
}

int RunVulkanBackend(IsPcOkPluginResultV1* out_result)
{
    if (out_result == nullptr)
        return 1;

    const std::lock_guard<std::mutex> lock(g_run_mutex);

    if (!InitializeVulkan())
    {
        FillDegradedResult(g_storage, "gpu_vulkan", g_init_message.c_str());
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
        ShutdownVulkan();
        FillErrorResult(g_storage, "gpu_vulkan", "FP32 matrix multiplication resource setup failed");
        *out_result = g_storage.result;
        return 1;
    }

    const uint32_t n = static_cast<uint32_t>(kMatMulN);
    const uint32_t groups_x = (n + 15) / 16;
    const uint32_t groups_y = (n + 15) / 16;

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(g_command_buffer, &begin_info);
    vkCmdBindPipeline(g_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, g_pipeline);
    vkCmdBindDescriptorSets(g_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, g_pipeline_layout, 0, 1, &g_descriptor_set, 0, nullptr);
    vkCmdDispatch(g_command_buffer, groups_x, groups_y, 1);
    vkEndCommandBuffer(g_command_buffer);

    constexpr int kRepetitions = 5;
    double elapsed_ms = 0.0;
    for (int i = 0; i < kRepetitions; ++i)
    {
        const auto start = std::chrono::high_resolution_clock::now();
        VkSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &g_command_buffer;
        vkQueueSubmit(g_queue, 1, &submit_info, VK_NULL_HANDLE);
        vkQueueWaitIdle(g_queue);
        const auto end = std::chrono::high_resolution_clock::now();
        elapsed_ms += std::chrono::duration<double, std::milli>(end - start).count();
    }
    elapsed_ms /= static_cast<double>(kRepetitions);

    void* data = nullptr;
    if (vkMapMemory(g_device, g_c_memory, 0, static_cast<VkDeviceSize>(n) * n * sizeof(float), 0, &data) != VK_SUCCESS)
    {
        ShutdownVulkan();
        FillErrorResult(g_storage, "gpu_vulkan", "FP32 matrix multiplication result readback failed");
        *out_result = g_storage.result;
        return 1;
    }
    const double device_checksum = ResultChecksum(static_cast<const float*>(data), n);
    vkUnmapMemory(g_device, g_c_memory);

    if (!ChecksumMatches(device_checksum, reference))
    {
        ShutdownVulkan();
        FillErrorResult(g_storage, "gpu_vulkan", "FP32 matrix multiplication result mismatch");
        *out_result = g_storage.result;
        return 1;
    }

    const double gflops = MatMulGflops(elapsed_ms / 1000.0);
    FillGflopsResult(g_storage, elapsed_ms, gflops, device_checksum, "gpu_vulkan");
    ShutdownVulkan();
    *out_result = g_storage.result;
    return 0;
}

} // namespace

extern "C" ISPCOK_PLUGIN_EXPORT int ispcok_get_module_v1(IsPcOkPluginModuleV1* out_module)
{
    if (out_module == nullptr)
        return 1;

    out_module->id = "gpu_vulkan";
    out_module->category = "gpu";
    out_module->run = &RunVulkanBackend;
    return 0;
}
