#ifndef ISPCOK_CORE_PLUGIN_LOADER_H
#define ISPCOK_CORE_PLUGIN_LOADER_H

#include "core/module.h"

#include <vector>

namespace ispcok {

std::vector<ModulePtr> LoadPluginModules(const std::string& plugin_dir);

} // namespace ispcok

#endif
