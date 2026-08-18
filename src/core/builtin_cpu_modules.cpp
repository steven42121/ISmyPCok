#include "core/builtin_module_factories.h"
#include "core/thread_util.h"

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

constexpr double kTargetSeconds = 2.0;

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

        const unsigned threads = CpuWorkerCount();
        auto work = [](std::uint64_t iterations) -> double
        {
            volatile float acc = 1.0f;
            for (std::uint64_t i = 0; i < iterations; ++i)
            {
                acc = acc * 1.000001f + 0.000001f;
                if (acc > 10.0f)
                    acc = 1.0f;
            }
            return acc;
        };

        const double per_sec = CalibratePerSecond(work, 1'000'000ULL);
        const std::uint64_t per_thread = static_cast<std::uint64_t>(per_sec * kTargetSeconds) + 1;

        std::vector<double> checksums(threads, 0.0);
        const double elapsed = RunParallel(threads, [&](unsigned t)
        {
            checksums[t] = work(per_thread);
        });
        const double total_ops = static_cast<double>(per_thread) * threads;
        const double mops = (total_ops / 1'000'000.0) / elapsed;

        result.metrics["mops"] = mops;
        result.metrics["elapsed_s"] = elapsed;
        result.metrics["threads"] = static_cast<double>(threads);
        result.metrics["checksum"] = checksums[0];
        result.score = ClampScore(mops / 40.0);
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

        const unsigned threads = CpuWorkerCount();
        auto work = [](std::uint64_t iterations) -> double
        {
            std::uint64_t x = 0x123456789abcdef0ULL;
            for (std::uint64_t i = 0; i < iterations; ++i)
            {
                x ^= x << 13;
                x ^= x >> 7;
                x ^= x << 17;
                x += i;
            }
            return static_cast<double>(x & 0xFFFFFFFFULL);
        };

        const double per_sec = CalibratePerSecond(work, 2'000'000ULL);
        const std::uint64_t per_thread = static_cast<std::uint64_t>(per_sec * kTargetSeconds) + 1;

        std::vector<double> checksums(threads, 0.0);
        const double elapsed = RunParallel(threads, [&](unsigned t)
        {
            checksums[t] = work(per_thread);
        });
        const double total_ops = static_cast<double>(per_thread) * threads;
        const double mops = (total_ops / 1'000'000.0) / elapsed;

        result.metrics["mops"] = mops;
        result.metrics["elapsed_s"] = elapsed;
        result.metrics["threads"] = static_cast<double>(threads);
        result.metrics["checksum"] = checksums[0];
        result.score = ClampScore(mops / 100.0);
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

        const unsigned threads = CpuWorkerCount();
        constexpr std::size_t per_thread_n = 16 * 1024 * 1024;
        std::vector<std::vector<std::uint8_t>> random_bits(threads);
        for (unsigned t = 0; t < threads; ++t)
        {
            random_bits[t].resize(per_thread_n);
            std::mt19937 rng(1234 + t);
            std::uniform_int_distribution<int> dist(0, 1);
            for (std::size_t i = 0; i < per_thread_n; ++i)
                random_bits[t][i] = static_cast<std::uint8_t>(dist(rng));
        }

        auto run_random = [&random_bits](std::uint64_t rounds) -> double
        {
            volatile double sum = 0.0;
            for (std::uint64_t r = 0; r < rounds; ++r)
            {
                volatile std::uint64_t acc = 0;
                const std::vector<std::uint8_t>& bits = random_bits[0];
                for (std::size_t i = 0; i < bits.size(); ++i)
                {
                    if (bits[i] == 0)
                        acc += i;
                    else
                        acc += (i ^ 0x55u);
                }
                sum += static_cast<double>(acc);
            }
            return sum;
        };

        auto run_predictable = [per_thread_n](std::uint64_t rounds) -> double
        {
            volatile double sum = 0.0;
            for (std::uint64_t r = 0; r < rounds; ++r)
            {
                volatile std::uint64_t acc = 0;
                for (std::size_t i = 0; i < per_thread_n; ++i)
                {
                    if ((i & 1023u) != 0)
                        acc += i;
                    else
                        acc += (i ^ 0xAAu);
                }
                sum += static_cast<double>(acc);
            }
            return sum;
        };

        const double random_round_s = 1.0 / std::max(CalibratePerSecond(run_random, 1ULL), 1e-9);
        const double predictable_round_s = 1.0 / std::max(CalibratePerSecond(run_predictable, 1ULL), 1e-9);
        const std::uint64_t rounds = static_cast<std::uint64_t>(kTargetSeconds / std::max(random_round_s, predictable_round_s)) + 1;

        std::vector<double> random_checks(threads, 0.0);
        const double random_s = RunParallel(threads, [&](unsigned t)
        {
            volatile double sum = 0.0;
            for (std::uint64_t r = 0; r < rounds; ++r)
            {
                volatile std::uint64_t acc = 0;
                const std::vector<std::uint8_t>& bits = random_bits[t];
                for (std::size_t i = 0; i < bits.size(); ++i)
                {
                    if (bits[i] == 0)
                        acc += i;
                    else
                        acc += (i ^ 0x55u);
                }
                sum += static_cast<double>(acc);
            }
            random_checks[t] = sum;
        });

        std::vector<double> predictable_checks(threads, 0.0);
        const double predictable_s = RunParallel(threads, [&](unsigned t)
        {
            volatile double sum = 0.0;
            for (std::uint64_t r = 0; r < rounds; ++r)
            {
                volatile std::uint64_t acc = 0;
                for (std::size_t i = 0; i < per_thread_n; ++i)
                {
                    if ((i & 1023u) != 0)
                        acc += i;
                    else
                        acc += (i ^ 0xAAu);
                }
                sum += static_cast<double>(acc);
            }
            predictable_checks[t] = sum;
        });

        const double penalty = random_s / std::max(predictable_s, 0.000001);
        double checksum = 0.0;
        for (unsigned t = 0; t < threads; ++t)
            checksum += random_checks[t] + predictable_checks[t];

        result.metrics["random_branch_s"] = random_s;
        result.metrics["predictable_branch_s"] = predictable_s;
        result.metrics["penalty_ratio"] = penalty;
        result.metrics["threads"] = static_cast<double>(threads);
        result.metrics["checksum"] = checksum;
        result.score = ClampScore(100.0 - (penalty - 1.0) * 30.0);
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

        const unsigned threads = CpuWorkerCount();
        auto work = [](std::uint64_t iterations) -> double
        {
            alignas(32) float a[8] = {1, 2, 3, 4, 5, 6, 7, 8};
            alignas(32) float b[8] = {8, 7, 6, 5, 4, 3, 2, 1};
            __m256 va = _mm256_load_ps(a);
            __m256 vb = _mm256_load_ps(b);
            __m256 vc = _mm256_set1_ps(1.000001f);
            for (std::uint64_t i = 0; i < iterations; ++i)
            {
                va = _mm256_add_ps(va, _mm256_mul_ps(vb, vc));
                vb = _mm256_sub_ps(vb, _mm256_mul_ps(va, vc));
            }
            alignas(32) float out[8];
            _mm256_store_ps(out, va);
            return out[0];
        };

        const double per_sec = CalibratePerSecond(work, 500'000ULL);
        const std::uint64_t per_thread = static_cast<std::uint64_t>(per_sec * kTargetSeconds) + 1;

        std::vector<double> checksums(threads, 0.0);
        const double elapsed = RunParallel(threads, [&](unsigned t)
        {
            checksums[t] = work(per_thread);
        });
        const double vector_ops = static_cast<double>(per_thread) * threads * 8.0;
        const double mops = (vector_ops / 1'000'000.0) / elapsed;

        result.status = "ok";
        result.metrics["mops"] = mops;
        result.metrics["elapsed_s"] = elapsed;
        result.metrics["threads"] = static_cast<double>(threads);
        result.metrics["checksum"] = checksums[0];
        result.score = ClampScore(mops / 240.0);
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

        const unsigned threads = CpuWorkerCount();
        auto work = [](std::uint64_t iterations) -> double
        {
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
            for (std::uint64_t i = 0; i < iterations; ++i)
            {
                va = _mm512_fmadd_ps(vb, vc, va);
                vb = _mm512_fnmadd_ps(va, vc, vb);
            }

            alignas(64) float out[16];
            _mm512_store_ps(out, va);
            return out[0];
        };

        const double per_sec = CalibratePerSecond(work, 500'000ULL);
        const std::uint64_t per_thread = static_cast<std::uint64_t>(per_sec * kTargetSeconds) + 1;

        std::vector<double> checksums(threads, 0.0);
        const double elapsed = RunParallel(threads, [&](unsigned t)
        {
            checksums[t] = work(per_thread);
        });
        const double vector_ops = static_cast<double>(per_thread) * threads * 16.0;
        const double mops = (vector_ops / 1'000'000.0) / elapsed;

        result.status = "ok";
        result.metrics["mops"] = mops;
        result.metrics["elapsed_s"] = elapsed;
        result.metrics["threads"] = static_cast<double>(threads);
        result.metrics["checksum"] = checksums[0];
        result.score = ClampScore(mops / 480.0);
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
