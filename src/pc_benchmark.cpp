#include "benchmark/cppbenchmark.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

namespace
{
CppBenchmark::Settings BuildThreadSettings()
{
    const unsigned int hw = std::thread::hardware_concurrency();
    int max_threads = (hw == 0) ? 8 : static_cast<int>(hw);
    max_threads = std::min(max_threads, 32);

    auto settings = CppBenchmark::Settings().Duration(3).Attempts(3);
    for (int threads = 1; threads <= max_threads; threads *= 2)
        settings.Threads(threads);
    return settings;
}

const auto kShortRun = CppBenchmark::Settings().Duration(3).Attempts(3);
const auto kPayloadSizes = CppBenchmark::Settings()
                               .Duration(3)
                               .Attempts(3)
                               .ParamRange(1024, 16 * 1024 * 1024,
                                           [](int from, int to, int& value)
                                           {
                                               const int current = value;
                                               value *= 2;
                                               return (current >= from && current <= to) ? current : (to + 1);
                                           });
const auto kThreadSettings = BuildThreadSettings();
} // namespace

BENCHMARK("cpu.integer_hash", kShortRun)
{
    static std::uint64_t seed = 0x9E3779B97F4A7C15ull;
    seed ^= seed >> 30;
    seed *= 0xBF58476D1CE4E5B9ull;
    seed ^= seed >> 27;
    seed *= 0x94D049BB133111EBull;
    seed ^= seed >> 31;

    context.metrics().SetCustom("digest", seed);
}

BENCHMARK("cpu.floating_point", kShortRun)
{
    static double sum = 0.0;
    const double v = std::sin(0.123456789) * std::cos(0.987654321);
    sum += std::sqrt(std::abs(v) + 1.0);

    context.metrics().SetCustom("checksum", sum);
}

class MemoryCopyFixture
{
protected:
    std::vector<std::uint8_t> source;
    std::vector<std::uint8_t> destination;

    MemoryCopyFixture() : source(16 * 1024 * 1024), destination(16 * 1024 * 1024)
    {
        for (std::size_t i = 0; i < source.size(); ++i)
            source[i] = static_cast<std::uint8_t>(i & 0xFFu);
    }
};

BENCHMARK_FIXTURE(MemoryCopyFixture, "memory.memcpy", kPayloadSizes)
{
    const std::size_t bytes = static_cast<std::size_t>(context.x());
    std::memcpy(destination.data(), source.data(), bytes);
    context.metrics().AddBytes(static_cast<std::int64_t>(bytes));
    context.metrics().SetCustom("edge", destination[0] + destination[bytes - 1]);
}

BENCHMARK("memory.vector_scan", kPayloadSizes)
{
    const std::size_t bytes = static_cast<std::size_t>(context.x());
    const std::size_t count = bytes / sizeof(std::uint64_t);

    std::vector<std::uint64_t> data(count, 0xA5A5A5A5A5A5A5A5ull);
    std::uint64_t acc = 0;
    for (std::uint64_t value : data)
        acc ^= (value + 0x9E3779B97F4A7C15ull);

    context.metrics().AddItems(static_cast<std::int64_t>(count));
    context.metrics().AddBytes(static_cast<std::int64_t>(bytes));
    context.metrics().SetCustom("xor", acc);
}

class AtomicIncFixture
{
protected:
    std::atomic<std::uint64_t> counter{0};
};

BENCHMARK_THREADS_FIXTURE(AtomicIncFixture, "threads.atomic_increment", kThreadSettings)
{
    context.metrics().AddItems(1);
    ++counter;
}

BENCHMARK_MAIN()
