#pragma once

#include <cstddef>
#include <cstdint>

namespace obsidian {
namespace compute {
namespace simd {

enum class SimdLevel : uint8_t {
    Scalar = 0,
    SSE42  = 1,
    AVX2   = 2,
    AVX512 = 3,
    NEON   = 4
};

// Detect highest supported SIMD instruction set on host CPU
SimdLevel detect_cpu_simd_level() noexcept;

// High-performance vectorized kernels with runtime CPUID dispatch
void vector_add(const float* a, const float* b, float* out, size_t count) noexcept;
float dot_product(const float* a, const float* b, size_t count) noexcept;

} // namespace simd
} // namespace compute
} // namespace obsidian
