#ifndef ISPCOK_PLUGINS_COMMON_NPU_MATMUL_WORKLOAD_H
#define ISPCOK_PLUGINS_COMMON_NPU_MATMUL_WORKLOAD_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace ispcok::plugins {

constexpr std::size_t kNpuMatMulN = 256;
constexpr std::size_t kNpuChecksumStride = 16;
constexpr double kNpuMatMulFlops = 2.0 * static_cast<double>(kNpuMatMulN) *
    static_cast<double>(kNpuMatMulN) * static_cast<double>(kNpuMatMulN);

inline std::uint16_t FloatToHalf(float value)
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const std::uint32_t sign = (bits >> 16) & 0x8000U;
    const std::uint32_t exponent = (bits >> 23) & 0xFFU;
    std::uint32_t mantissa = bits & 0x7FFFFFU;

    if (exponent == 0xFFU)
        return static_cast<std::uint16_t>(sign | 0x7C00U | (mantissa != 0 ? 0x0200U : 0U));

    const int half_exponent = static_cast<int>(exponent) - 127 + 15;
    if (half_exponent >= 31)
        return static_cast<std::uint16_t>(sign | 0x7C00U);
    if (half_exponent <= 0)
    {
        if (half_exponent < -10)
            return static_cast<std::uint16_t>(sign);
        mantissa |= 0x800000U;
        const unsigned shift = static_cast<unsigned>(14 - half_exponent);
        const std::uint32_t truncated = mantissa >> shift;
        const std::uint32_t remainder = mantissa & ((1U << shift) - 1U);
        const std::uint32_t halfway = 1U << (shift - 1U);
        const std::uint32_t rounded = truncated +
            ((remainder > halfway || (remainder == halfway && (truncated & 1U))) ? 1U : 0U);
        return static_cast<std::uint16_t>(sign | rounded);
    }

    std::uint32_t half_mantissa = mantissa >> 13;
    const std::uint32_t remainder = mantissa & 0x1FFFU;
    if (remainder > 0x1000U || (remainder == 0x1000U && (half_mantissa & 1U)))
    {
        ++half_mantissa;
        if (half_mantissa == 0x400U)
        {
            half_mantissa = 0;
            if (half_exponent + 1 >= 31)
                return static_cast<std::uint16_t>(sign | 0x7C00U);
            return static_cast<std::uint16_t>(sign | (static_cast<std::uint32_t>(half_exponent + 1) << 10));
        }
    }
    return static_cast<std::uint16_t>(sign |
        (static_cast<std::uint32_t>(half_exponent) << 10) | half_mantissa);
}

inline float HalfToFloat(std::uint16_t value)
{
    const std::uint32_t sign = static_cast<std::uint32_t>(value & 0x8000U) << 16;
    const std::uint32_t exponent = (value >> 10) & 0x1FU;
    std::uint32_t mantissa = value & 0x03FFU;
    std::uint32_t bits = 0;
    if (exponent == 0)
    {
        if (mantissa == 0)
            bits = sign;
        else
        {
            int normalized_exponent = -14;
            while ((mantissa & 0x0400U) == 0)
            {
                mantissa <<= 1;
                --normalized_exponent;
            }
            mantissa &= 0x03FFU;
            bits = sign | (static_cast<std::uint32_t>(normalized_exponent + 127) << 23) |
                (mantissa << 13);
        }
    }
    else if (exponent == 0x1FU)
        bits = sign | 0x7F800000U | (mantissa << 13);
    else
        bits = sign | ((exponent - 15U + 127U) << 23) | (mantissa << 13);

    float result = 0.0f;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

inline void FillNpuMatrices(std::vector<float>& a, std::vector<float>& b)
{
    a.resize(kNpuMatMulN * kNpuMatMulN);
    b.resize(kNpuMatMulN * kNpuMatMulN);
    std::uint32_t state = 0xA341316CU;
    auto next = [&state]()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        // Multiples of 1/256 in this range are represented exactly by binary16.
        return static_cast<float>((state & 0x3FU) + 1U) / 256.0f;
    };
    for (float& value : a)
        value = next();
    for (float& value : b)
        value = next();
}

inline double NpuReferenceChecksum(const std::vector<float>& a, const std::vector<float>& b)
{
    double checksum = 0.0;
    for (std::size_t row = 0; row < kNpuMatMulN; row += kNpuChecksumStride)
    {
        for (std::size_t column = 0; column < kNpuMatMulN; column += kNpuChecksumStride)
        {
            double value = 0.0;
            for (std::size_t inner = 0; inner < kNpuMatMulN; ++inner)
                value += static_cast<double>(a[row * kNpuMatMulN + inner]) *
                    static_cast<double>(b[inner * kNpuMatMulN + column]);
            checksum += value;
        }
    }
    return checksum;
}

inline std::vector<std::uint16_t> NpuMatrixToHalf(const std::vector<float>& matrix)
{
    std::vector<std::uint16_t> half(matrix.size());
    for (std::size_t index = 0; index < matrix.size(); ++index)
        half[index] = FloatToHalf(matrix[index]);
    return half;
}

inline double NpuResultChecksum(const std::uint16_t* result)
{
    double checksum = 0.0;
    for (std::size_t row = 0; row < kNpuMatMulN; row += kNpuChecksumStride)
        for (std::size_t column = 0; column < kNpuMatMulN; column += kNpuChecksumStride)
            checksum += static_cast<double>(HalfToFloat(result[row * kNpuMatMulN + column]));
    return checksum;
}

inline bool NpuChecksumMatches(double actual, double expected)
{
    return std::fabs(actual - expected) <= std::max(1.0, std::fabs(expected)) * 2.0e-2;
}

inline double NpuMatMulGflops(double elapsed_ms)
{
    return elapsed_ms > 0.0 ? kNpuMatMulFlops / (elapsed_ms / 1000.0) / 1.0e9 : 0.0;
}

} // namespace ispcok::plugins

#endif
