#ifndef ISPCOK_CORE_MODULE_H
#define ISPCOK_CORE_MODULE_H

#include "core/types.h"

#include <memory>
#include <string>

namespace ispcok {

class IModule
{
public:
    virtual ~IModule() = default;

    virtual std::string id() const = 0;
    virtual std::string category() const = 0;
    virtual bool is_plugin() const { return false; }
    virtual ModuleResult run() = 0;
};

using ModulePtr = std::shared_ptr<IModule>;

} // namespace ispcok

#endif
