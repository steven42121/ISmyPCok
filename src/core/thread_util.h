#ifndef ISPCOK_CORE_THREAD_UTIL_H
#define ISPCOK_CORE_THREAD_UTIL_H

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

namespace ispcok {

inline unsigned CpuWorkerCount()
{
    const unsigned hw = std::thread::hardware_concurrency();
    return hw == 0 ? 1 : hw;
}

template <typename Work>
double RunParallel(unsigned threads, Work&& work)
{
    std::vector<std::thread> workers;
    workers.reserve(threads);
    const auto start = std::chrono::steady_clock::now();
    for (unsigned t = 0; t < threads; ++t)
        workers.emplace_back(work, t);
    for (auto& worker : workers)
        worker.join();
    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(end - start).count();
}

template <typename Work>
double CalibratePerSecond(Work&& work, std::uint64_t iterations)
{
    const auto start = std::chrono::steady_clock::now();
    volatile auto sink = work(iterations);
    (void)sink;
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return static_cast<double>(iterations) / std::max(elapsed, 1e-9);
}

} // namespace ispcok

#endif
