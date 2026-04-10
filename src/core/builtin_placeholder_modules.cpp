#include "core/builtin_module_factories.h"

#include <memory>
#include <string>
#include <vector>

namespace ispcok {
namespace {

class PlaceholderModule final : public IModule
{
public:
    PlaceholderModule(std::string module_id, std::string module_category, std::string module_message)
        : id_(std::move(module_id)),
          category_(std::move(module_category)),
          message_(std::move(module_message))
    {}

    std::string id() const override { return id_; }
    std::string category() const override { return category_; }

    ModuleResult run() override
    {
        ModuleResult result;
        result.id = id_;
        result.category = category_;
        result.status = "not_implemented";
        result.score = 0.0;
        result.message = message_;
        return result;
    }

private:
    std::string id_;
    std::string category_;
    std::string message_;
};

} // namespace

std::vector<ModulePtr> CreateBuiltinPlaceholderModules()
{
    std::vector<ModulePtr> modules;
    modules.emplace_back(std::make_shared<PlaceholderModule>("gpu_vulkan", "gpu", "Planned: Vulkan graphics/compute benchmark"));
    modules.emplace_back(std::make_shared<PlaceholderModule>("gpu_dx12", "gpu", "Planned: DX12 graphics benchmark"));
    modules.emplace_back(std::make_shared<PlaceholderModule>("cuda", "gpu", "Planned: CUDA compute benchmark"));
    modules.emplace_back(std::make_shared<PlaceholderModule>("hip", "gpu", "Planned: HIP compute benchmark"));
    modules.emplace_back(std::make_shared<PlaceholderModule>("xpu", "gpu", "Planned: oneAPI/Level Zero benchmark"));
    modules.emplace_back(std::make_shared<PlaceholderModule>("npu", "npu", "Planned: NPU inference benchmark"));
    return modules;
}

} // namespace ispcok
