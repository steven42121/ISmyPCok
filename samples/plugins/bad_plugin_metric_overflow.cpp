#include "ispcok/plugin_api.h"

extern "C" __declspec(dllexport) int ispcok_get_module_v1(IsPcOkPluginModuleV1* out_module);

namespace
{
IsPcOkPluginMetricV1 g_metric{"m", 1.0};

int RunBadMetricOverflow(IsPcOkPluginResultV1* out_result)
{
    if (out_result == nullptr)
        return 1;

    out_result->score = 43.0;
    out_result->message = "bad plugin returns too many metrics";
    out_result->metrics = &g_metric;
    out_result->metric_count = 5000;
    return 0;
}
} // namespace

extern "C" __declspec(dllexport) int ispcok_get_module_v1(IsPcOkPluginModuleV1* out_module)
{
    if (out_module == nullptr)
        return 1;
    out_module->id = "bad_metric_overflow";
    out_module->category = "test";
    out_module->run = &RunBadMetricOverflow;
    return 0;
}
