#ifndef ISPCOK_PLUGINS_COMMON_REPORT_HELPERS_H
#define ISPCOK_PLUGINS_COMMON_REPORT_HELPERS_H

#include "ispcok/plugin_api.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace ispcok::plugins {

// Fixed capacity for metric arrays; plugin metrics are reported in one batch.
constexpr std::size_t kMaxBackendMetrics = 8;

// Scale factor mapping FP32 GFLOPS to a 0-100 score. Shared by every backend
// so scores are comparable; a mid-range consumer GPU scores near the middle.
constexpr double kScoreGflopsScale = 100.0;

inline double ClampScore(double value)
{
    if (value < 0.0)
        return 0.0;
    if (value > 100.0)
        return 100.0;
    return value;
}

inline double GflopsScore(double gflops)
{
    return ClampScore(gflops / kScoreGflopsScale);
}

// Per-thread storage for metrics and the plugin result. The caller owns one
// instance per thread, keeping returned pointers valid while the host copies
// them immediately after run() returns.
struct ResultStorage
{
    IsPcOkPluginMetricV1 metrics[kMaxBackendMetrics];
    std::size_t metric_count = 0;
    std::string message;
    IsPcOkPluginResultV1 result{};
};

inline void FillResultStorage(ResultStorage& storage, double score, std::string message,
                              const IsPcOkPluginMetricV1* metrics, std::size_t metric_count)
{
    storage.message = std::move(message);
    storage.metric_count = std::min(metric_count, kMaxBackendMetrics);
    for (std::size_t i = 0; i < storage.metric_count; ++i)
        storage.metrics[i] = metrics[i];

    storage.result.score = score;
    storage.result.message = storage.message.c_str();
    storage.result.metrics = storage.metrics;
    storage.result.metric_count = storage.metric_count;
}

inline void FillGflopsResult(ResultStorage& storage, double elapsed_ms, double gflops,
                             double checksum, const char* id)
{
    char message[256];
    std::snprintf(message, sizeof(message), "%s: %s %.1f GFLOPS in %.2f ms",
                  id, "FP32 matrix multiplication", gflops, elapsed_ms);

    IsPcOkPluginMetricV1 metrics[kMaxBackendMetrics];
    metrics[0] = IsPcOkPluginMetricV1{"fp32_gflops", gflops};
    metrics[1] = IsPcOkPluginMetricV1{"elapsed_ms", elapsed_ms};
    metrics[2] = IsPcOkPluginMetricV1{"checksum", checksum};
    FillResultStorage(storage, GflopsScore(gflops), message, metrics, 3);
}

inline void FillDegradedResult(ResultStorage& storage, const char* id, const char* reason)
{
    char message[256];
    std::snprintf(message, sizeof(message), "%s: %s", id, reason);
    FillResultStorage(storage, 0.0, message, nullptr, 0);
}

// Real failure (result mismatch, resource error): the plugin returns non-zero
// rc so the host reports status "error" with the given message.
inline int FillErrorResult(ResultStorage& storage, const char* id, const char* reason)
{
    char message[256];
    std::snprintf(message, sizeof(message), "%s: %s", id, reason);
    FillResultStorage(storage, 0.0, message, nullptr, 0);
    return 1;
}

} // namespace ispcok::plugins

#endif
