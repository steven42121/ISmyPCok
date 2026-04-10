#include "ispcok/plugin_api.h"

#include <limits>

extern "C" __declspec(dllexport) int ispcok_get_module_v1(IsPcOkPluginModuleV1* out_module);

namespace
{
int RunBadNanScore(IsPcOkPluginResultV1* out_result)
{
    if (out_result == nullptr)
        return 1;

    out_result->score = std::numeric_limits<double>::quiet_NaN();
    out_result->message = "bad plugin returns NaN score";
    out_result->metrics = nullptr;
    out_result->metric_count = 0;
    return 0;
}
} // namespace

extern "C" __declspec(dllexport) int ispcok_get_module_v1(IsPcOkPluginModuleV1* out_module)
{
    if (out_module == nullptr)
        return 1;
    out_module->id = "bad_nan_score";
    out_module->category = "test";
    out_module->run = &RunBadNanScore;
    return 0;
}
