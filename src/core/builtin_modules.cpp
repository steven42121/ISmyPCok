#include "core/builtin_modules.h"

#include "core/builtin_module_factories.h"

namespace ispcok {

std::vector<ModulePtr> CreateBuiltinModules()
{
    std::vector<ModulePtr> modules;
    auto append = [&modules](std::vector<ModulePtr>&& part)
    {
        modules.insert(modules.end(), part.begin(), part.end());
    };

    append(CreateBuiltinCpuModules());
    append(CreateBuiltinMemoryModules());
    append(CreateBuiltinStorageModules());
    append(CreateBuiltinNetworkModules());
    append(CreateBuiltinSystemModules());
    append(CreateBuiltinPlaceholderModules());
    return modules;
}

} // namespace ispcok
