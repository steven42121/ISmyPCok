#ifndef ISPCOK_CORE_BUILTIN_MODULE_FACTORIES_H
#define ISPCOK_CORE_BUILTIN_MODULE_FACTORIES_H

#include "core/module.h"

#include <vector>

namespace ispcok {

std::vector<ModulePtr> CreateBuiltinCpuModules();
std::vector<ModulePtr> CreateBuiltinMemoryModules();
std::vector<ModulePtr> CreateBuiltinStorageModules();
std::vector<ModulePtr> CreateBuiltinNetworkModules();
std::vector<ModulePtr> CreateBuiltinSystemModules();
std::vector<ModulePtr> CreateBuiltinPlaceholderModules();

} // namespace ispcok

#endif
