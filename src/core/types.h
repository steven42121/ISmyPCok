#ifndef ISPCOK_CORE_TYPES_H
#define ISPCOK_CORE_TYPES_H

#include <map>
#include <string>
#include <vector>

namespace ispcok {

struct ModuleResult
{
    std::string id;
    std::string category;
    std::string status;
    double score = 0.0;
    std::map<std::string, double> metrics;
    std::string message;
    bool plugin = false;
};

struct ScenarioResult
{
    std::string id;
    double score = 0.0;
    std::vector<std::string> bottlenecks;
};

struct RunOptions
{
    std::vector<std::string> modules;
    std::string scenario;
    std::string plugin_dir;
};

struct RunReport
{
    std::string version;
    std::vector<ModuleResult> modules;
    bool has_scenario = false;
    ScenarioResult scenario;
};

} // namespace ispcok

#endif
