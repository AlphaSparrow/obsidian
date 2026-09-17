#include "simd_dispatch.hpp"

#include <cstddef>
#include <cstdint>

#if defined(OBSIDIAN_SIMD_ISA)

// ---------------------------------------------------------------------------
// Per-ISA compiled kernel implementations
// ---------------------------------------------------------------------------

#if OBSIDIAN_SIMD_ISA == SSE42

#include <nmmintrin.h>

namespace obsidian {
namespace compute {
namespace simd {

void vector_add_sse42(const float* a, const float* b, float* out, size_t count) noexcept {
    size_t i = 0;
    for (; i + 4 <= count; i += 4) {
        __m128 va = _mm_loadu_ps(a + i);
        __m128 vb = _mm_loadu_ps(b + i);
        _mm_storeu_ps(out + i, _mm_add_ps(va, vb));
    }
    for (; i < count; ++i) {
        out[i] = a[i] + b[i];
    }
}

float dot_product_sse42(const float* a, const float* b, size_t count) noexcept {
    __m128 acc = _mm_setzero_ps();
    size_t i = 0;
    for (; i + 4 <= count; i += 4) {
        __m128 va = _mm_loadu_ps(a + i);
        __m128 vb = _mm_loadu_ps(b + i);
        acc = _mm_add_ps(acc, _mm_mul_ps(va, vb));
    }
    alignas(16) float buf[4];
    _mm_store_ps(buf, acc);
    float sum = buf[0] + buf[1] + buf[2] + buf[3];
    for (; i < count; ++i) {
        sum += a[i] * b[i];
    }
    return sum;
}

} // namespace simd
} // namespace compute
} // namespace obsidian

#elif OBSIDIAN_SIMD_ISA == AVX2

#include <immintrin.h>

namespace obsidian {
namespace compute {
namespace simd {

void vector_add_avx2(const float* a, const float* b, float* out, size_t count) noexcept {
    size_t i = 0;
    for (; i + 8 <= count; i += 8) {
        __m256 va = _mm256_loadu_ps(a + i);
        __m256 vb = _mm256_loadu_ps(b + i);
        _mm256_storeu_ps(out + i, _mm256_add_ps(va, vb));
    }
    for (; i < count; ++i) {
        out[i] = a[i] + b[i];
    }
}

float dot_product_avx2(const float* a, const float* b, size_t count) noexcept {
    __m256 acc = _mm256_setzero_ps();
    size_t i = 0;
    for (; i + 8 <= count; i += 8) {
        __m256 va = _mm256_loadu_ps(a + i);
        __m256 vb = _mm256_loadu_ps(b + i);
#if defined(__FMA__) || defined(_MSC_VER)
        acc = _mm256_fmadd_ps(va, vb, acc);
#else
        acc = _mm256_add_ps(acc, _mm256_mul_ps(va, vb));
#endif
    }
    alignas(32) float buf[8];
    _mm256_store_ps(buf, acc);
    float sum = buf[0] + buf[1] + buf[2] + buf[3] + buf[4] + buf[5] + buf[6] + buf[7];
    for (; i < count; ++i) {
        sum += a[i] * b[i];
    }
    return sum;
}

} // namespace simd
} // namespace compute
} // namespace obsidian

#elif OBSIDIAN_SIMD_ISA == AVX512

#include <immintrin.h>

namespace obsidian {
namespace compute {
namespace simd {

void vector_add_avx512(const float* a, const float* b, float* out, size_t count) noexcept {
    size_t i = 0;
    for (; i + 16 <= count; i += 16) {
        __m512 va = _mm512_loadu_ps(a + i);
        __m512 vb = _mm512_loadu_ps(b + i);
        _mm512_storeu_ps(out + i, _mm512_add_ps(va, vb));
    }
    for (; i < count; ++i) {
        out[i] = a[i] + b[i];
    }
}

float dot_product_avx512(const float* a, const float* b, size_t count) noexcept {
    __m512 acc = _mm512_setzero_ps();
    size_t i = 0;
    for (; i + 16 <= count; i += 16) {
        __m512 va = _mm512_loadu_ps(a + i);
        __m512 vb = _mm512_loadu_ps(b + i);
        acc = _mm512_fmadd_ps(va, vb, acc);
    }
    float sum = _mm512_reduce_add_ps(acc);
    for (; i < count; ++i) {
        sum += a[i] * b[i];
    }
    return sum;
}

} // namespace simd
} // namespace compute
} // namespace obsidian

#elif OBSIDIAN_SIMD_ISA == NEON

#include <arm_neon.h>

namespace obsidian {
namespace compute {
namespace simd {

void vector_add_neon(const float* a, const float* b, float* out, size_t count) noexcept {
    size_t i = 0;
    for (; i + 4 <= count; i += 4) {
        float32x4_t va = vld1q_f32(a + i);
        float32x4_t vb = vld1q_f32(b + i);
        vst1q_f32(out + i, vaddq_f32(va, vb));
    }
    for (; i < count; ++i) {
        out[i] = a[i] + b[i];
    }
}

float dot_product_neon(const float* a, const float* b, size_t count) noexcept {
    float32x4_t acc = vdupq_n_f32(0.0f);
    size_t i = 0;
    for (; i + 4 <= count; i += 4) {
        float32x4_t va = vld1q_f32(a + i);
        float32x4_t vb = vld1q_f32(b + i);
        acc = vmlaq_f32(acc, va, vb);
    }
    float sum = vaddvq_f32(acc);
    for (; i < count; ++i) {
        sum += a[i] * b[i];
    }
    return sum;
}

} // namespace simd
} // namespace compute
} // namespace obsidian

#endif

#else

// ---------------------------------------------------------------------------
// Aggregate dispatcher and CPUID detection
// ---------------------------------------------------------------------------

#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#include <immintrin.h>
#endif

namespace obsidian {
namespace compute {
namespace simd {

// Forward declare ISA implementations
void vector_add_sse42(const float* a, const float* b, float* out, size_t count) noexcept;
float dot_product_sse42(const float* a, const float* b, size_t count) noexcept;

void vector_add_avx2(const float* a, const float* b, float* out, size_t count) noexcept;
float dot_product_avx2(const float* a, const float* b, size_t count) noexcept;

#if defined(OBSIDIAN_HAS_AVX512)
void vector_add_avx512(const float* a, const float* b, float* out, size_t count) noexcept;
float dot_product_avx512(const float* a, const float* b, size_t count) noexcept;
#endif

// Scalar baseline
static void vector_add_scalar(const float* a, const float* b, float* out, size_t count) noexcept {
    for (size_t i = 0; i < count; ++i) {
        out[i] = a[i] + b[i];
    }
}

static float dot_product_scalar(const float* a, const float* b, size_t count) noexcept {
    float sum = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        sum += a[i] * b[i];
    }
    return sum;
}

SimdLevel detect_cpu_simd_level() noexcept {
#if defined(_MSC_VER) || defined(__x86_64__) || defined(__i386__)
    int info[4] = {0};

    // Query basic CPUID features (function 1)
#if defined(_MSC_VER)
    __cpuid(info, 1);
#else
    __cpuid(1, info[0], info[1], info[2], info[3]);
#endif

    const bool has_sse42   = (info[2] & (1 << 19)) != 0;
    const bool has_osxsave = (info[2] & (1 << 27)) != 0;
    const bool has_avx     = (info[2] & (1 << 28)) != 0;

    bool os_avx_support = false;
    bool os_avx512_support = false;

    if (has_osxsave) {
        uint64_t xcr0 = 0;
#if defined(_MSC_VER)
        xcr0 = _xgetbv(0);
#else
        uint32_t lo, hi;
        __asm__ __volatile__("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
        xcr0 = (static_cast<uint64_t>(hi) << 32) | lo;
#endif
        // Bits 1 and 2 check XMM and YMM state saving
        os_avx_support = (xcr0 & 0x6) == 0x6;
        // Bits 5, 6, 7 check opmask and ZMM state saving
        os_avx512_support = (xcr0 & 0xE6) == 0xE6;
    }

    // Query extended features (function 7, subleaf 0)
    int ext_info[4] = {0};
#if defined(_MSC_VER)
    __cpuidex(ext_info, 7, 0);
#else
    __cpuid_count(7, 0, ext_info[0], ext_info[1], ext_info[2], ext_info[3]);
#endif

    const bool has_avx2   = has_avx && os_avx_support && ((ext_info[1] & (1 << 5)) != 0);
    const bool has_avx512 = os_avx512_support && ((ext_info[1] & (1 << 16)) != 0);

    if (has_avx512) {
        return SimdLevel::AVX512;
    }
    if (has_avx2) {
        return SimdLevel::AVX2;
    }
    if (has_sse42) {
        return SimdLevel::SSE42;
    }
    return SimdLevel::Scalar;

#elif defined(__aarch64__)
    return SimdLevel::NEON;
#else
    return SimdLevel::Scalar;
#endif
}

using AddFn = void (*)(const float*, const float*, float*, size_t) noexcept;
using DotFn = float (*)(const float*, const float*, size_t) noexcept;

struct DispatchTable {
    AddFn add{vector_add_scalar};
    DotFn dot{dot_product_scalar};

    DispatchTable() noexcept {
        const SimdLevel level = detect_cpu_simd_level();
#if defined(OBSIDIAN_HAS_AVX512)
        if (level == SimdLevel::AVX512) {
            add = vector_add_avx512;
            dot = dot_product_avx512;
            return;
        }
#endif
        if (level >= SimdLevel::AVX2) {
            add = vector_add_avx2;
            dot = dot_product_avx2;
            return;
        }
        if (level >= SimdLevel::SSE42) {
            add = vector_add_sse42;
            dot = dot_product_sse42;
            return;
        }
    }
};

static const DispatchTable g_dispatch;

void vector_add(const float* a, const float* b, float* out, size_t count) noexcept {
    g_dispatch.add(a, b, out, count);
}

float dot_product(const float* a, const float* b, size_t count) noexcept {
    return g_dispatch.dot(a, b, count);
}

} // namespace simd
} // namespace compute
} // namespace obsidian

#endif
