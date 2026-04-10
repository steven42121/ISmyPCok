#include "core/json_writer.h"

#include <cstdint>
#include <cstdio>
#include <iomanip>
#include <sstream>

namespace ispcok {
namespace {

std::string Escape(const std::string& text)
{
    std::ostringstream out;
    for (unsigned char c : text)
    {
        switch (c)
        {
        case '\\': out << "\\\\"; break;
        case '\"': out << "\\\""; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (c <= 0x1F)
            {
                char buf[7] = {0};
                std::snprintf(buf, sizeof(buf), "\\u%04X", static_cast<unsigned int>(c));
                out << buf;
            }
            else
            {
                out << static_cast<char>(c);
            }
            break;
        }
    }
    return out.str();
}

} // namespace

std::string ReportToJson(const RunReport& report)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(4);
    out << "{";
    out << "\"version\":\"" << Escape(report.version) << "\",";
    out << "\"modules\":[";
    for (size_t i = 0; i < report.modules.size(); ++i)
    {
        const ModuleResult& module = report.modules[i];
        if (i > 0)
            out << ",";
        out << "{";
        out << "\"id\":\"" << Escape(module.id) << "\",";
        out << "\"category\":\"" << Escape(module.category) << "\",";
        out << "\"status\":\"" << Escape(module.status) << "\",";
        out << "\"score\":" << module.score << ",";
        out << "\"plugin\":" << (module.plugin ? "true" : "false") << ",";
        out << "\"message\":\"" << Escape(module.message) << "\",";
        out << "\"metrics\":{";
        size_t idx = 0;
        for (const auto& kv : module.metrics)
        {
            if (idx++ > 0)
                out << ",";
            out << "\"" << Escape(kv.first) << "\":" << kv.second;
        }
        out << "}";
        out << "}";
    }
    out << "]";

    if (report.has_scenario)
    {
        out << ",\"scenario\":{";
        out << "\"id\":\"" << Escape(report.scenario.id) << "\",";
        out << "\"score\":" << report.scenario.score << ",";
        out << "\"bottlenecks\":[";
        for (size_t i = 0; i < report.scenario.bottlenecks.size(); ++i)
        {
            if (i > 0)
                out << ",";
            out << "\"" << Escape(report.scenario.bottlenecks[i]) << "\"";
        }
        out << "]";
        out << "}";
    }
    out << "}";
    return out.str();
}

} // namespace ispcok
