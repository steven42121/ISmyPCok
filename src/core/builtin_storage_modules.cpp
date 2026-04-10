#include "core/builtin_module_factories.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <system_error>
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

class DiskSeqModule final : public IModule
{
public:
    std::string id() const override { return "disk_seq"; }
    std::string category() const override { return "storage"; }

    ModuleResult run() override
    {
        ModuleResult result;
        result.id = id();
        result.category = category();
        result.status = "ok";

        const std::filesystem::path file = std::filesystem::temp_directory_path() / "ispcok_disk_seq.bin";
        constexpr std::size_t block_size = 1 * 1024 * 1024;
        constexpr std::size_t blocks = 64;
        std::vector<char> block(block_size, 'x');

        const auto write_start = std::chrono::high_resolution_clock::now();
        {
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            for (std::size_t i = 0; i < blocks; ++i)
                out.write(block.data(), static_cast<std::streamsize>(block.size()));
        }
        const auto write_end = std::chrono::high_resolution_clock::now();

        const auto read_start = std::chrono::high_resolution_clock::now();
        {
            std::ifstream in(file, std::ios::binary);
            for (std::size_t i = 0; i < blocks; ++i)
                in.read(block.data(), static_cast<std::streamsize>(block.size()));
        }
        const auto read_end = std::chrono::high_resolution_clock::now();

        std::error_code ec;
        std::filesystem::remove(file, ec);

        const double total_mib = static_cast<double>(blocks * block_size) / (1024.0 * 1024.0);
        const double write_s = std::chrono::duration<double>(write_end - write_start).count();
        const double read_s = std::chrono::duration<double>(read_end - read_start).count();
        const double write_mibs = total_mib / std::max(write_s, 0.000001);
        const double read_mibs = total_mib / std::max(read_s, 0.000001);

        result.metrics["write_mibps"] = write_mibs;
        result.metrics["read_mibps"] = read_mibs;
        result.score = ClampScore((write_mibs + read_mibs) / 20.0);
        result.message = "Temporary file sequential throughput";
        return result;
    }
};

class DiskRandModule final : public IModule
{
public:
    std::string id() const override { return "disk_rand"; }
    std::string category() const override { return "storage"; }

    ModuleResult run() override
    {
        ModuleResult result;
        result.id = id();
        result.category = category();
        result.status = "ok";

        const std::filesystem::path file = std::filesystem::temp_directory_path() / "ispcok_disk_rand.bin";
        constexpr std::size_t block_size = 4 * 1024;
        constexpr std::size_t blocks = 16 * 1024;
        std::vector<char> block(block_size, 'z');

        {
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            for (std::size_t i = 0; i < blocks; ++i)
                out.write(block.data(), static_cast<std::streamsize>(block.size()));
        }

        std::vector<std::size_t> indices(blocks);
        for (std::size_t i = 0; i < blocks; ++i)
            indices[i] = i;
        std::mt19937_64 rng(42);
        std::shuffle(indices.begin(), indices.end(), rng);

        const auto start = std::chrono::high_resolution_clock::now();
        {
            std::ifstream in(file, std::ios::binary);
            for (std::size_t idx : indices)
            {
                in.seekg(static_cast<std::streamoff>(idx * block_size));
                in.read(block.data(), static_cast<std::streamsize>(block.size()));
            }
        }
        const auto end = std::chrono::high_resolution_clock::now();

        std::error_code ec;
        std::filesystem::remove(file, ec);

        const double elapsed = std::chrono::duration<double>(end - start).count();
        const double iops = static_cast<double>(blocks) / std::max(elapsed, 0.000001);
        result.metrics["rand_read_iops_4k"] = iops;
        result.score = ClampScore(iops / 300.0);
        result.message = "Temporary file random read IOPS (4K)";
        return result;
    }
};

} // namespace

std::vector<ModulePtr> CreateBuiltinStorageModules()
{
    std::vector<ModulePtr> modules;
    modules.emplace_back(std::make_shared<DiskSeqModule>());
    modules.emplace_back(std::make_shared<DiskRandModule>());
    return modules;
}

} // namespace ispcok
