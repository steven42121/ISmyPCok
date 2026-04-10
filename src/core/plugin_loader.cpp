#include "core/plugin_loader.h"

#include "ispcok/plugin_api.h"

#include <cmath>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace ispcok {
namespace {

#if defined(_WIN32)
constexpr size_t kMaxPluginMetrics = 1024;

#if defined(_MSC_VER)
bool SafeInvokePluginRun(IsPcOkPluginRunV1 run, IsPcOkPluginResultV1* out_result, int* out_rc)
{
    __try
    {
        *out_rc = run(out_result);
        return true;
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
#endif

class PluginModule final : public IModule
{
public:
    PluginModule(HMODULE handle, IsPcOkPluginModuleV1 plugin_module)
        : handle_(handle),
          plugin_module_(plugin_module),
          id_(plugin_module.id != nullptr ? plugin_module.id : "unknown_plugin"),
          category_(plugin_module.category != nullptr ? plugin_module.category : "plugin")
    {}

    ~PluginModule() override
    {
        if (handle_ != nullptr)
            FreeLibrary(handle_);
    }

    std::string id() const override { return id_; }
    std::string category() const override { return category_; }
    bool is_plugin() const override { return true; }

    ModuleResult run() override
    {
        ModuleResult result;
        result.id = id();
        result.category = category();
        result.plugin = true;

        if (plugin_module_.run == nullptr)
        {
            result.status = "error";
            result.message = "plugin run callback is null";
            return result;
        }

        IsPcOkPluginResultV1 plugin_result{};
        int rc = -1;
#if defined(_MSC_VER)
        if (!SafeInvokePluginRun(plugin_module_.run, &plugin_result, &rc))
        {
            result.status = "error";
            result.message = "plugin crashed during run()";
            return result;
        }
#else
        rc = plugin_module_.run(&plugin_result);
#endif
        if (rc != 0)
        {
            result.status = "error";
            result.message = "plugin returned non-zero status";
            return result;
        }

        if (!std::isfinite(plugin_result.score))
        {
            result.status = "error";
            result.message = "plugin returned non-finite score";
            return result;
        }
        if ((plugin_result.metric_count > 0) && (plugin_result.metrics == nullptr))
        {
            result.status = "error";
            result.message = "plugin returned null metrics with non-zero count";
            return result;
        }
        if (plugin_result.metric_count > kMaxPluginMetrics)
        {
            result.status = "error";
            result.message = "plugin metric_count exceeds host limit";
            return result;
        }

        result.status = "ok";
        result.score = plugin_result.score;
        if (plugin_result.message != nullptr)
            result.message = plugin_result.message;
        for (size_t i = 0; i < plugin_result.metric_count; ++i)
        {
            const IsPcOkPluginMetricV1 metric = plugin_result.metrics[i];
            if ((metric.name == nullptr) || !std::isfinite(metric.value))
                continue;
            result.metrics[metric.name] = metric.value;
        }
        return result;
    }

private:
    HMODULE handle_ = nullptr;
    IsPcOkPluginModuleV1 plugin_module_{};
    std::string id_;
    std::string category_;
};

ModulePtr TryLoadPlugin(const std::filesystem::path& path)
{
    const std::wstring wide = path.wstring();
    HMODULE handle = LoadLibraryW(wide.c_str());
    if (handle == nullptr)
        return nullptr;

    auto entry = reinterpret_cast<IsPcOkGetModuleV1>(GetProcAddress(handle, ISPCOK_PLUGIN_ENTRYPOINT_V1));
    if (entry == nullptr)
    {
        FreeLibrary(handle);
        return nullptr;
    }

    IsPcOkPluginModuleV1 module{};
    if ((entry(&module) != 0) || (module.id == nullptr) || (module.run == nullptr))
    {
        FreeLibrary(handle);
        return nullptr;
    }

    return std::make_shared<PluginModule>(handle, module);
}
#endif

} // namespace

std::vector<ModulePtr> LoadPluginModules(const std::string& plugin_dir)
{
    std::vector<ModulePtr> modules;
    if (plugin_dir.empty())
        return modules;

#if defined(_WIN32)
    std::error_code ec;
    const std::filesystem::path base(plugin_dir);
    if (!std::filesystem::exists(base, ec))
        return modules;

    for (const auto& entry : std::filesystem::directory_iterator(base, ec))
    {
        if (ec)
            break;
        if (!entry.is_regular_file())
            continue;
        if (entry.path().extension() != ".dll")
            continue;
        ModulePtr module = TryLoadPlugin(entry.path());
        if (module != nullptr)
            modules.emplace_back(std::move(module));
    }
#else
    (void)plugin_dir;
#endif

    return modules;
}

} // namespace ispcok
