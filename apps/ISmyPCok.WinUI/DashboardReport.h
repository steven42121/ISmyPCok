#pragma once

#include <optional>
#include <string>
#include <vector>

namespace ispcok::dashboard
{
    struct DashboardMetric
    {
        std::wstring name;
        double value = 0.0;
    };

    struct DashboardModule
    {
        std::wstring id;
        std::wstring category;
        std::wstring status;
        double score = 0.0;
        bool plugin = false;
        std::wstring message;
        std::vector<DashboardMetric> metrics;
    };

    struct DashboardScenario
    {
        std::wstring id;
        double score = 0.0;
        std::vector<std::wstring> bottlenecks;
    };

    struct DashboardReport
    {
        std::wstring version;
        std::vector<DashboardModule> modules;
        std::optional<DashboardScenario> scenario;
    };

    struct DashboardParseResult
    {
        std::optional<DashboardReport> report;
        std::wstring error;

        explicit operator bool() const noexcept
        {
            return report.has_value();
        }
    };

    DashboardParseResult ParseDashboardReport(std::wstring const& json) noexcept;
}
