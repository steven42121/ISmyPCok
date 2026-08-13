#ifndef ISPCOK_PLUGINS_COMMON_MATMUL_WORKLOAD_H
#define ISPCOK_PLUGINS_COMMON_MATMUL_WORKLOAD_H

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ispcok::plugins {

// Generic FP32 matrix multiplication workload shared by every accelerator
// backend. A fixed problem size keeps results comparable across backends.
constexpr std::size_t kMatMulN = 1024;

constexpr double kMatMulFlops = 2.0 * static_cast<double>(kMatMulN) * static_cast<double>(kMatMulN) * static_cast<double>(kMatMulN);

inline double MatMulGflops(double elapsed_s)
{
    if (elapsed_s <= 0.0)
        return 0.0;
    return kMatMulFlops / elapsed_s / 1.0e9;
}

// Deterministic pseudo-random initializer so every backend computes the same
// matrices and the same reference result.
inline void FillRandomMatrices(std::vector<float>& a, std::vector<float>& b)
{
    a.assign(kMatMulN * kMatMulN, 0.0f);
    b.assign(kMatMulN * kMatMulN, 0.0f);
    std::uint64_t state = 0x9E3779B97F4A7C15ULL;
    auto next = [&state]()
    {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return static_cast<float>(((state >> 32) & 0xFFFFU) / 65536.0f);
    };
    for (auto& value : a)
        value = next();
    for (auto& value : b)
        value = next();
}

// Reference checksum computed from the expected matrix product. Each backend
// computes the product on the device, then the host reads back a strided
// sample of the result and compares its sum against this reference with a
// relative tolerance. Both the device-side checksum and this reference must
// sample the same positions (i % 64 == 0 and j % 64 == 0).
inline double ReferenceChecksum(const std::vector<float>& a, const std::vector<float>& b)
{
    const std::size_t n = kMatMulN;
    double sum = 0.0;
    for (std::size_t i = 0; i < n; i += 64)
    {
        for (std::size_t j = 0; j < n; j += 64)
        {
            double acc = 0.0;
            for (std::size_t k = 0; k < n; ++k)
                acc += static_cast<double>(a[i * n + k]) * static_cast<double>(b[k * n + j]);
            sum += acc;
        }
    }
    return sum;
}

// Device-side checksum over the same strided sample. `c` points to the full
// N x N result in row-major layout with `row_stride` floats per row (usually
// equal to N; padded buffers may pass a larger stride).
inline double ResultChecksum(const float* c, std::size_t row_stride)
{
    const std::size_t n = kMatMulN;
    double sum = 0.0;
    for (std::size_t i = 0; i < n; i += 64)
        for (std::size_t j = 0; j < n; j += 64)
            sum += static_cast<double>(c[i * row_stride + j]);
    return sum;
}

inline bool ChecksumMatches(double device_checksum, double reference_checksum, double tolerance = 1.0e-3)
{
    if (reference_checksum == 0.0)
        return std::fabs(device_checksum) <= tolerance;
    return std::fabs(device_checksum - reference_checksum) <= tolerance * std::fabs(reference_checksum);
}

} // namespace ispcok::plugins

#endif
