#include "ispcok/plugin_api.h"

extern "C" __declspec(dllexport) int ispcok_get_module_v1(IsPcOkPluginModuleV1* out_module);

namespace
{
int RunBadNullMetrics(IsPcOkPluginResultV1* out_result)
{
    if (out_result == nullptr)
        return 1;

    out_result->score = 42.0;
    out_result->message = "bad plugin returns null metrics with count > 0";
    out_result->metrics = nullptr;
    out_result->metric_count = 2;
    return 0;
}
} // namespace

extern "C" __declspec(dllexport) int ispcok_get_module_v1(IsPcOkPluginModuleV1* out_module)
{
    if (out_module == nullptr)
        return 1;
    out_module->id = "bad_null_metrics";
    out_module->category = "test";
    out_module->run = &RunBadNullMetrics;
    return 0;
}
