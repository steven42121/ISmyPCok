#include "core/builtin_module_factories.h"

#include <memory>
#include <string>
#include <vector>

namespace ispcok {
namespace {

class PlaceholderModule final : public IModule
{
public:
    PlaceholderModule(std::string module_id, std::string module_category, std::string module_status, std::string module_message)
        : id_(std::move(module_id)),
          category_(std::move(module_category)),
          status_(std::move(module_status)),
          message_(std::move(module_message))
    {}

    std::string id() const override { return id_; }
    std::string category() const override { return category_; }

    ModuleResult run() override
    {
        ModuleResult result;
        result.id = id_;
        result.category = category_;
        result.status = status_;
        result.score = 0.0;
        result.message = message_;
        return result;
    }

private:
    std::string id_;
    std::string category_;
    std::string status_;
    std::string message_;
};

} // namespace

std::vector<ModulePtr> CreateBuiltinPlaceholderModules()
{
    std::vector<ModulePtr> modules;
    // Real accelerator backends ship as optional dynamic plugins. When a plugin
    // is not built (its SDK was absent at configure time), the placeholder
    // reports "not_supported" with a "backend not compiled" message so the run
    // degrades gracefully instead of marking the module as unimplemented.
    modules.emplace_back(std::make_shared<PlaceholderModule>("gpu_vulkan", "gpu", "not_supported", "gpu_vulkan: backend not compiled (Vulkan SDK required)"));
    modules.emplace_back(std::make_shared<PlaceholderModule>("cuda", "gpu", "not_supported", "cuda: backend not compiled (CUDA toolkit required)"));
    modules.emplace_back(std::make_shared<PlaceholderModule>("hip", "gpu", "not_supported", "hip: backend not compiled (HIP runtime required)"));
    modules.emplace_back(std::make_shared<PlaceholderModule>("xpu", "gpu", "not_supported", "xpu: backend not compiled (Level Zero loader required)"));
    modules.emplace_back(std::make_shared<PlaceholderModule>("gpu_dx12", "gpu", "not_supported", "gpu_dx12: backend not compiled (Windows D3D12 SDK required)"));
    modules.emplace_back(std::make_shared<PlaceholderModule>("npu", "npu", "not_supported", "npu: backend not compiled (Windows DXCore and DirectML SDK required)"));
    return modules;
}

} // namespace ispcok
