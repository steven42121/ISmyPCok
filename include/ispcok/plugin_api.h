#ifndef ISPCOK_PLUGIN_API_H
#define ISPCOK_PLUGIN_API_H

#include <stddef.h>

#if defined(_WIN32)
#if defined(ISPCOK_PLUGIN_BUILDING_DLL)
#define ISPCOK_PLUGIN_EXPORT __declspec(dllexport)
#else
#define ISPCOK_PLUGIN_EXPORT __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define ISPCOK_PLUGIN_EXPORT __attribute__((visibility("default")))
#else
#define ISPCOK_PLUGIN_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct IsPcOkPluginMetricV1
{
    const char* name;
    double value;
} IsPcOkPluginMetricV1;

typedef struct IsPcOkPluginResultV1
{
    double score;
    const char* message;
    const IsPcOkPluginMetricV1* metrics;
    size_t metric_count;
} IsPcOkPluginResultV1;

typedef int (*IsPcOkPluginRunV1)(IsPcOkPluginResultV1* out_result);

typedef struct IsPcOkPluginModuleV1
{
    const char* id;
    const char* category;
    IsPcOkPluginRunV1 run;
} IsPcOkPluginModuleV1;

typedef int (*IsPcOkGetModuleV1)(IsPcOkPluginModuleV1* out_module);

#define ISPCOK_PLUGIN_ENTRYPOINT_V1 "ispcok_get_module_v1"

#ifdef __cplusplus
}
#endif

#endif
