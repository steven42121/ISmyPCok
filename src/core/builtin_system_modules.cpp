#include "core/builtin_module_factories.h"

#include <memory>
#include <string>
#include <thread>
#include <vector>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace ispcok {
namespace {

bool HasHypervisor()
{
#if defined(_M_IX86) || defined(_M_X64)
    int cpu_info[4] = {0, 0, 0, 0};
    __cpuid(cpu_info, 1);
    return ((cpu_info[2] >> 31) & 0x1) != 0;
#elif defined(__i386__) || defined(__x86_64__)
    unsigned int eax = 0;
    unsigned int ebx = 0;
    unsigned int ecx = 0;
    unsigned int edx = 0;
#if defined(__GNUC__) || defined(__clang__)
    __asm__ __volatile__("cpuid"
                         : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                         : "a"(1), "c"(0));
    return ((ecx >> 31) & 0x1U) != 0;
#else
    return false;
#endif
#else
    return false;
#endif
}

class VirtStateModule final : public IModule
{
public:
    std::string id() const override { return "virt_state"; }
    std::string category() const override { return "system"; }

    ModuleResult run() override
    {
        ModuleResult result;
        result.id = id();
        result.category = category();
        result.status = "ok";

        const bool hv = HasHypervisor();
        const unsigned int hw = std::thread::hardware_concurrency();

        result.metrics["hypervisor"] = hv ? 1.0 : 0.0;
        result.metrics["logical_cores"] = static_cast<double>(hw == 0 ? 1 : hw);
        result.metrics["bits"] = static_cast<double>(sizeof(void*) * 8);
        result.score = hv ? 75.0 : 92.0;
        result.message = hv ? "Hypervisor detected" : "No hypervisor bit detected";
        return result;
    }
};

} // namespace

std::vector<ModulePtr> CreateBuiltinSystemModules()
{
    std::vector<ModulePtr> modules;
    modules.emplace_back(std::make_shared<VirtStateModule>());
    return modules;
}

} // namespace ispcok
