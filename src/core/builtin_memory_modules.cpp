#include "core/builtin_module_factories.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace ispcok {
namespace {

double ClampScore(double value)
{
    if (value < 0.0)
        return 0.0;
    if (value > 100.0)
        return 100.0;
    return value;
}

class MemoryBandwidthModule final : public IModule
{
public:
    std::string id() const override { return "memory_bw"; }
    std::string category() const override { return "memory"; }

    ModuleResult run() override
    {
        ModuleResult result;
        result.id = id();
        result.category = category();
        result.status = "ok";

        constexpr std::size_t size = 64 * 1024 * 1024;
        constexpr int loops = 8;
        std::vector<std::uint8_t> src(size, 0x5A);
        std::vector<std::uint8_t> dst(size, 0x00);

        const auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < loops; ++i)
            std::memcpy(dst.data(), src.data(), size);
        const auto end = std::chrono::high_resolution_clock::now();

        const auto elapsed = std::chrono::duration<double>(end - start).count();
        const double gib = (static_cast<double>(size) * loops) / (1024.0 * 1024.0 * 1024.0);
        const double gibps = gib / elapsed;

        result.metrics["gibps"] = gibps;
        result.metrics["elapsed_s"] = elapsed;
        result.score = ClampScore(gibps * 4.0);
        result.message = "Sequential memcpy bandwidth";
        return result;
    }
};

class MemoryLatencyModule final : public IModule
{
public:
    std::string id() const override { return "memory_latency"; }
    std::string category() const override { return "memory"; }

    ModuleResult run() override
    {
        ModuleResult result;
        result.id = id();
        result.category = category();
        result.status = "ok";

        constexpr std::size_t elements = 8 * 1024 * 1024;
        std::vector<std::uint32_t> next(elements);
        std::vector<std::uint32_t> indices(elements);
        for (std::size_t i = 0; i < elements; ++i)
            indices[i] = static_cast<std::uint32_t>(i);
        std::mt19937 rng(42);
        std::shuffle(indices.begin(), indices.end(), rng);
        for (std::size_t i = 0; i + 1 < elements; ++i)
            next[indices[i]] = indices[i + 1];
        next[indices.back()] = indices.front();

        std::uint32_t p = indices.front();
        constexpr std::uint64_t steps = 50'000'000ULL;
        const auto start = std::chrono::high_resolution_clock::now();
        for (std::uint64_t i = 0; i < steps; ++i)
            p = next[p];
        const auto end = std::chrono::high_resolution_clock::now();

        const double elapsed = std::chrono::duration<double>(end - start).count();
        const double ns_per_access = (elapsed * 1e9) / static_cast<double>(steps);
        result.metrics["ns_per_access"] = ns_per_access;
        result.metrics["checksum"] = static_cast<double>(p);
        result.metrics["elapsed_s"] = elapsed;
        result.score = ClampScore(140.0 - ns_per_access * 2.0);
        result.message = "Pointer-chasing memory latency";
        return result;
    }
};

} // namespace

std::vector<ModulePtr> CreateBuiltinMemoryModules()
{
    std::vector<ModulePtr> modules;
    modules.emplace_back(std::make_shared<MemoryBandwidthModule>());
    modules.emplace_back(std::make_shared<MemoryLatencyModule>());
    return modules;
}

} // namespace ispcok
