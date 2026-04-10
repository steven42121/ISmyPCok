#ifndef ISPCOK_CORE_ENGINE_H
#define ISPCOK_CORE_ENGINE_H

#include "core/types.h"

#include <string>
#include <vector>

namespace ispcok {

std::string Version();
std::vector<std::string> ListModules(const std::string& plugin_dir);
std::vector<std::string> ListScenarios();
RunReport Run(const RunOptions& options);
std::string ToJson(const RunReport& report);

} // namespace ispcok

#endif
