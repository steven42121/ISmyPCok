#include "core/builtin_module_factories.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <immintrin.h>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

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

class CpuFp32Module final : public IModule
{
public:
    std::string id() const override { return "cpu_fp32"; }
    std::string category() const override { return "cpu"; }

    ModuleResult run() override
    {
        ModuleResult result;
        result.id = id();
        result.category = category();
        result.status = "ok";

        constexpr std::uint64_t iterations = 50'000'000ULL;
        volatile float acc = 1.0f;
        const auto start = std::chrono::high_resolution_clock::now();
        for (std::uint64_t i = 0; i < iterations; ++i)
        {
            acc = acc * 1.000001f + 0.000001f;
            if (acc > 10.0f)
                acc = 1.0f;
        }
        const auto end = std::chrono::high_resolution_clock::now();
        const auto elapsed = std::chrono::duration<double>(end - start).count();
        const double mops = (static_cast<double>(iterations) / 1'000'000.0) / elapsed;

        result.metrics["mops"] = mops;
        result.metrics["elapsed_s"] = elapsed;
        result.score = ClampScore(mops / 1.8);
        result.message = "FP32 loop throughput";
        return result;
    }
};

class CpuScalarIntModule final : public IModule
{
public:
    std::string id() const override { return "cpu_scalar_int"; }
    std::string category() const override { return "cpu"; }

    ModuleResult run() override
    {
        ModuleResult result;
        result.id = id();
        result.category = category();
        result.status = "ok";

        constexpr std::uint64_t iterations = 120'000'000ULL;
        std::uint64_t x = 0x123456789abcdef0ULL;
        const auto start = std::chrono::high_resolution_clock::now();
        for (std::uint64_t i = 0; i < iterations; ++i)
        {
            x ^= x << 13;
            x ^= x >> 7;
            x ^= x << 17;
            x += i;
        }
        const auto end = std::chrono::high_resolution_clock::now();
        const double elapsed = std::chrono::duration<double>(end - start).count();
        const double mops = (static_cast<double>(iterations) / 1'000'000.0) / std::max(elapsed, 0.000001);

        result.metrics["mops"] = mops;
        result.metrics["checksum"] = static_cast<double>(x & 0xFFFFFFFFULL);
        result.metrics["elapsed_s"] = elapsed;
        result.score = ClampScore(mops / 3.2);
        result.message = "Scalar integer throughput";
        return result;
    }
};

class CpuBranchPredictModule final : public IModule
{
public:
    std::string id() const override { return "cpu_branch_predict"; }
    std::string category() const override { return "cpu"; }

    ModuleResult run() override
    {
        ModuleResult result;
        result.id = id();
        result.category = category();
        result.status = "ok";

        constexpr std::size_t n = 16 * 1024 * 1024;
        std::vector<std::uint8_t> random_bits(n);
        std::mt19937 rng(1234);
        std::uniform_int_distribution<int> dist(0, 1);
        for (std::size_t i = 0; i < n; ++i)
            random_bits[i] = static_cast<std::uint8_t>(dist(rng));

        volatile std::uint64_t sum_random = 0;
        volatile std::uint64_t sum_predictable = 0;

        const auto start_random = std::chrono::high_resolution_clock::now();
        for (std::size_t i = 0; i < n; ++i)
        {
            if (random_bits[i] == 0)
                sum_random += i;
            else
                sum_random += (i ^ 0x55u);
        }
        const auto end_random = std::chrono::high_resolution_clock::now();

        const auto start_predictable = std::chrono::high_resolution_clock::now();
        for (std::size_t i = 0; i < n; ++i)
        {
            if ((i & 1023u) != 0)
                sum_predictable += i;
            else
                sum_predictable += (i ^ 0xAAu);
        }
        const auto end_predictable = std::chrono::high_resolution_clock::now();

        const double random_s = std::chrono::duration<double>(end_random - start_random).count();
        const double predictable_s = std::chrono::duration<double>(end_predictable - start_predictable).count();
        const double penalty = random_s / std::max(predictable_s, 0.000001);

        result.metrics["random_branch_s"] = random_s;
        result.metrics["predictable_branch_s"] = predictable_s;
        result.metrics["penalty_ratio"] = penalty;
        result.metrics["checksum"] = static_cast<double>((sum_random ^ sum_predictable) & 0xFFFFFFFFULL);
        result.score = ClampScore(120.0 - penalty * 40.0);
        result.message = "Branch predictability sensitivity";
        return result;
    }
};

class CpuAvx2Module final : public IModule
{
public:
    std::string id() const override { return "cpu_avx2"; }
    std::string category() const override { return "cpu"; }

    ModuleResult run() override
    {
        ModuleResult result;
        result.id = id();
        result.category = category();

#if !defined(__AVX2__)
        result.status = "not_supported";
        result.message = "Compiler target does not enable AVX2 instructions";
        return result;
#else
        if (!SupportsAvx2())
        {
            result.status = "not_supported";
            result.message = "AVX2 not supported by current CPU/OS";
            return result;
        }

        alignas(32) float a[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        alignas(32) float b[8] = {8, 7, 6, 5, 4, 3, 2, 1};
        __m256 va = _mm256_load_ps(a);
        __m256 vb = _mm256_load_ps(b);
        __m256 vc = _mm256_set1_ps(1.000001f);
        constexpr std::uint64_t iterations = 50'000'000ULL;

        const auto start = std::chrono::high_resolution_clock::now();
        for (std::uint64_t i = 0; i < iterations; ++i)
        {
            va = _mm256_add_ps(va, _mm256_mul_ps(vb, vc));
            vb = _mm256_sub_ps(vb, _mm256_mul_ps(va, vc));
        }
        const auto end = std::chrono::high_resolution_clock::now();

        alignas(32) float out[8];
        _mm256_store_ps(out, va);
        const double elapsed = std::chrono::duration<double>(end - start).count();
        const double vector_ops = static_cast<double>(iterations) * 8.0;
        const double mops = (vector_ops / 1'000'000.0) / std::max(elapsed, 0.000001);

        result.status = "ok";
        result.metrics["mops"] = mops;
        result.metrics["elapsed_s"] = elapsed;
        result.metrics["checksum"] = out[0];
        result.score = ClampScore(mops / 6.0);
        result.message = "AVX2 vector FP throughput";
        return result;
#endif
    }

private:
    static bool SupportsAvx2()
    {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
        int regs[4] = {0, 0, 0, 0};
        __cpuid(regs, 1);
        const bool osxsave = (regs[2] & (1 << 27)) != 0;
        const bool avx = (regs[2] & (1 << 28)) != 0;
        if (!(osxsave && avx))
            return false;

        unsigned long long xcr0 = _xgetbv(0);
        if ((xcr0 & 0x6) != 0x6)
            return false;

        __cpuidex(regs, 7, 0);
        return (regs[1] & (1 << 5)) != 0;
#elif (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__i386__))
        return __builtin_cpu_supports("avx2") != 0;
#else
        return false;
#endif
    }
};

class CpuAvx512Module final : public IModule
{
public:
    std::string id() const override { return "cpu_avx512"; }
    std::string category() const override { return "cpu"; }

    ModuleResult run() override
    {
        ModuleResult result;
        result.id = id();
        result.category = category();

#if !defined(__AVX512F__)
        result.status = "not_supported";
        result.message = "Compiler target does not enable AVX-512 instructions";
        return result;
#else
        if (!SupportsAvx512())
        {
            result.status = "not_supported";
            result.message = "AVX-512 not supported by current CPU/OS";
            return result;
        }

        alignas(64) float a[16];
        alignas(64) float b[16];
        for (int i = 0; i < 16; ++i)
        {
            a[i] = static_cast<float>(i + 1);
            b[i] = static_cast<float>(16 - i);
        }

        __m512 va = _mm512_load_ps(a);
        __m512 vb = _mm512_load_ps(b);
        __m512 vc = _mm512_set1_ps(1.0000005f);
        constexpr std::uint64_t iterations = 40'000'000ULL;

        const auto start = std::chrono::high_resolution_clock::now();
        for (std::uint64_t i = 0; i < iterations; ++i)
        {
            va = _mm512_fmadd_ps(vb, vc, va);
            vb = _mm512_fnmadd_ps(va, vc, vb);
        }
        const auto end = std::chrono::high_resolution_clock::now();

        alignas(64) float out[16];
        _mm512_store_ps(out, va);
        const double elapsed = std::chrono::duration<double>(end - start).count();
        const double vector_ops = static_cast<double>(iterations) * 16.0;
        const double mops = (vector_ops / 1'000'000.0) / std::max(elapsed, 0.000001);

        result.status = "ok";
        result.metrics["mops"] = mops;
        result.metrics["elapsed_s"] = elapsed;
        result.metrics["checksum"] = out[0];
        result.score = ClampScore(mops / 10.0);
        result.message = "AVX-512 vector FP throughput";
        return result;
#endif
    }

private:
    static bool SupportsAvx512()
    {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
        int regs[4] = {0, 0, 0, 0};
        __cpuid(regs, 1);
        if ((regs[2] & (1 << 27)) == 0)
            return false;
        unsigned long long xcr0 = _xgetbv(0);
        if ((xcr0 & 0xE6) != 0xE6)
            return false;
        __cpuidex(regs, 7, 0);
        return (regs[1] & (1 << 16)) != 0;
#elif (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__i386__))
        return __builtin_cpu_supports("avx512f") != 0;
#else
        return false;
#endif
    }
};

} // namespace

std::vector<ModulePtr> CreateBuiltinCpuModules()
{
    std::vector<ModulePtr> modules;
    modules.emplace_back(std::make_shared<CpuScalarIntModule>());
    modules.emplace_back(std::make_shared<CpuFp32Module>());
    modules.emplace_back(std::make_shared<CpuBranchPredictModule>());
    modules.emplace_back(std::make_shared<CpuAvx2Module>());
    modules.emplace_back(std::make_shared<CpuAvx512Module>());
    return modules;
}

} // namespace ispcok
