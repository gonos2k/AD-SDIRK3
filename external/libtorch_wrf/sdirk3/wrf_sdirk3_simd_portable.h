/**
 * @file wrf_sdirk3_simd_portable.h
 * @brief Portable SIMD wrapper supporting both x86 (AVX2) and ARM (NEON)
 * @date 2025-08-05
 * 
 * Provides architecture-agnostic SIMD operations for k-slab optimization
 * with automatic detection and fallback to scalar operations.
 */

#ifndef WRF_SDIRK3_SIMD_PORTABLE_H
#define WRF_SDIRK3_SIMD_PORTABLE_H

#include <cstddef>
#include <cmath>
#include <algorithm>

// Architecture detection
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #define SDIRK3_X86_ARCH
    #include <immintrin.h>
#elif defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    #define SDIRK3_ARM_ARCH
    #include <arm_neon.h>
#else
    #define SDIRK3_NO_SIMD
#endif

namespace WRF_SDIRK3 {
namespace SIMD {

/**
 * @brief SIMD vector type abstraction
 */
#ifdef SDIRK3_X86_ARCH
    #ifdef __AVX2__
        using vec_f32 = __m256;
        static constexpr int VEC_WIDTH = 8;
        #define SDIRK3_HAS_SIMD
    #elif defined(__SSE2__)
        using vec_f32 = __m128;
        static constexpr int VEC_WIDTH = 4;
        #define SDIRK3_HAS_SIMD
    #endif
#elif defined(SDIRK3_ARM_ARCH)
    using vec_f32 = float32x4_t;
    static constexpr int VEC_WIDTH = 4;
    #define SDIRK3_HAS_SIMD
#else
    struct vec_f32 { float data[1]; };
    static constexpr int VEC_WIDTH = 1;
#endif

/**
 * @brief Load functions
 */
inline vec_f32 vec_load(const float* ptr) {
    #ifdef SDIRK3_X86_ARCH
        #ifdef __AVX2__
            return _mm256_loadu_ps(ptr);
        #elif defined(__SSE2__)
            return _mm_loadu_ps(ptr);
        #endif
    #elif defined(SDIRK3_ARM_ARCH)
        return vld1q_f32(ptr);
    #else
        vec_f32 result;
        result.data[0] = *ptr;
        return result;
    #endif
}

inline vec_f32 vec_load_aligned(const float* ptr) {
    #ifdef SDIRK3_X86_ARCH
        #ifdef __AVX2__
            return _mm256_load_ps(ptr);
        #elif defined(__SSE2__)
            return _mm_load_ps(ptr);
        #endif
    #elif defined(SDIRK3_ARM_ARCH)
        // ARM NEON doesn't require explicit aligned loads
        return vld1q_f32(ptr);
    #else
        return vec_load(ptr);
    #endif
}

/**
 * @brief Store functions
 */
inline void vec_store(float* ptr, vec_f32 v) {
    #ifdef SDIRK3_X86_ARCH
        #ifdef __AVX2__
            _mm256_storeu_ps(ptr, v);
        #elif defined(__SSE2__)
            _mm_storeu_ps(ptr, v);
        #endif
    #elif defined(SDIRK3_ARM_ARCH)
        vst1q_f32(ptr, v);
    #else
        *ptr = v.data[0];
    #endif
}

/**
 * @brief Arithmetic operations
 */
inline vec_f32 vec_add(vec_f32 a, vec_f32 b) {
    #ifdef SDIRK3_X86_ARCH
        #ifdef __AVX2__
            return _mm256_add_ps(a, b);
        #elif defined(__SSE2__)
            return _mm_add_ps(a, b);
        #endif
    #elif defined(SDIRK3_ARM_ARCH)
        return vaddq_f32(a, b);
    #else
        vec_f32 result;
        result.data[0] = a.data[0] + b.data[0];
        return result;
    #endif
}

inline vec_f32 vec_sub(vec_f32 a, vec_f32 b) {
    #ifdef SDIRK3_X86_ARCH
        #ifdef __AVX2__
            return _mm256_sub_ps(a, b);
        #elif defined(__SSE2__)
            return _mm_sub_ps(a, b);
        #endif
    #elif defined(SDIRK3_ARM_ARCH)
        return vsubq_f32(a, b);
    #else
        vec_f32 result;
        result.data[0] = a.data[0] - b.data[0];
        return result;
    #endif
}

inline vec_f32 vec_mul(vec_f32 a, vec_f32 b) {
    #ifdef SDIRK3_X86_ARCH
        #ifdef __AVX2__
            return _mm256_mul_ps(a, b);
        #elif defined(__SSE2__)
            return _mm_mul_ps(a, b);
        #endif
    #elif defined(SDIRK3_ARM_ARCH)
        return vmulq_f32(a, b);
    #else
        vec_f32 result;
        result.data[0] = a.data[0] * b.data[0];
        return result;
    #endif
}

inline vec_f32 vec_div(vec_f32 a, vec_f32 b) {
    #ifdef SDIRK3_X86_ARCH
        #ifdef __AVX2__
            return _mm256_div_ps(a, b);
        #elif defined(__SSE2__)
            return _mm_div_ps(a, b);
        #endif
    #elif defined(SDIRK3_ARM_ARCH)
        // ARM NEON division (might use reciprocal approximation)
        #ifdef __ARM_FEATURE_FP16_VECTOR_ARITHMETIC
            return vdivq_f32(a, b);
        #else
            // Reciprocal approximation for older ARM
            float32x4_t reciprocal = vrecpeq_f32(b);
            reciprocal = vmulq_f32(vrecpsq_f32(b, reciprocal), reciprocal);
            return vmulq_f32(a, reciprocal);
        #endif
    #else
        vec_f32 result;
        result.data[0] = a.data[0] / b.data[0];
        return result;
    #endif
}

/**
 * @brief FMA (Fused Multiply-Add): a*b + c
 */
inline vec_f32 vec_fmadd(vec_f32 a, vec_f32 b, vec_f32 c) {
    #ifdef SDIRK3_X86_ARCH
        #ifdef __FMA__
            #ifdef __AVX2__
                return _mm256_fmadd_ps(a, b, c);
            #else
                return _mm_fmadd_ps(a, b, c);
            #endif
        #else
            return vec_add(vec_mul(a, b), c);
        #endif
    #elif defined(SDIRK3_ARM_ARCH)
        return vfmaq_f32(c, a, b);  // ARM FMA: c + a*b
    #else
        vec_f32 result;
        result.data[0] = a.data[0] * b.data[0] + c.data[0];
        return result;
    #endif
}

/**
 * @brief Comparison operations
 */
inline vec_f32 vec_cmp_gt(vec_f32 a, vec_f32 b) {
    #ifdef SDIRK3_X86_ARCH
        #ifdef __AVX2__
            return _mm256_cmp_ps(a, b, _CMP_GT_OQ);
        #elif defined(__SSE2__)
            return _mm_cmpgt_ps(a, b);
        #endif
    #elif defined(SDIRK3_ARM_ARCH)
        return vreinterpretq_f32_u32(vcgtq_f32(a, b));
    #else
        vec_f32 result;
        result.data[0] = (a.data[0] > b.data[0]) ? -1.0f : 0.0f;
        return result;
    #endif
}

/**
 * @brief Broadcast operations
 */
inline vec_f32 vec_set1(float value) {
    #ifdef SDIRK3_X86_ARCH
        #ifdef __AVX2__
            return _mm256_set1_ps(value);
        #elif defined(__SSE2__)
            return _mm_set1_ps(value);
        #endif
    #elif defined(SDIRK3_ARM_ARCH)
        return vdupq_n_f32(value);
    #else
        vec_f32 result;
        result.data[0] = value;
        return result;
    #endif
}

inline vec_f32 vec_zero() {
    #ifdef SDIRK3_X86_ARCH
        #ifdef __AVX2__
            return _mm256_setzero_ps();
        #elif defined(__SSE2__)
            return _mm_setzero_ps();
        #endif
    #elif defined(SDIRK3_ARM_ARCH)
        return vdupq_n_f32(0.0f);
    #else
        vec_f32 result;
        result.data[0] = 0.0f;
        return result;
    #endif
}

/**
 * @brief Math operations
 */
inline vec_f32 vec_sqrt(vec_f32 a) {
    #ifdef SDIRK3_X86_ARCH
        #ifdef __AVX2__
            return _mm256_sqrt_ps(a);
        #elif defined(__SSE2__)
            return _mm_sqrt_ps(a);
        #endif
    #elif defined(SDIRK3_ARM_ARCH)
        return vsqrtq_f32(a);
    #else
        vec_f32 result;
        result.data[0] = std::sqrt(a.data[0]);
        return result;
    #endif
}

/**
 * @brief Min/Max operations
 */
inline vec_f32 vec_min(vec_f32 a, vec_f32 b) {
    #ifdef SDIRK3_X86_ARCH
        #ifdef __AVX2__
            return _mm256_min_ps(a, b);
        #elif defined(__SSE2__)
            return _mm_min_ps(a, b);
        #endif
    #elif defined(SDIRK3_ARM_ARCH)
        return vminq_f32(a, b);
    #else
        vec_f32 result;
        result.data[0] = std::min(a.data[0], b.data[0]);
        return result;
    #endif
}

inline vec_f32 vec_max(vec_f32 a, vec_f32 b) {
    #ifdef SDIRK3_X86_ARCH
        #ifdef __AVX2__
            return _mm256_max_ps(a, b);
        #elif defined(__SSE2__)
            return _mm_max_ps(a, b);
        #endif
    #elif defined(SDIRK3_ARM_ARCH)
        return vmaxq_f32(a, b);
    #else
        vec_f32 result;
        result.data[0] = std::max(a.data[0], b.data[0]);
        return result;
    #endif
}

/**
 * @brief Platform detection helpers
 */
inline bool has_simd() {
    #ifdef SDIRK3_HAS_SIMD
        return true;
    #else
        return false;
    #endif
}

inline int get_vector_width() {
    return VEC_WIDTH;
}

inline const char* get_simd_type() {
    #ifdef SDIRK3_X86_ARCH
        #ifdef __AVX2__
            return "AVX2";
        #elif defined(__SSE2__)
            return "SSE2";
        #else
            return "x86_scalar";
        #endif
    #elif defined(SDIRK3_ARM_ARCH)
        return "ARM_NEON";
    #else
        return "scalar";
    #endif
}

/**
 * @brief Vectorized loop helper
 */
template<typename Func>
inline void vectorized_loop(float* out, const float* in, int n, Func func) {
    int i = 0;
    
    #ifdef SDIRK3_HAS_SIMD
    // Process vectorized part
    for (; i <= n - VEC_WIDTH; i += VEC_WIDTH) {
        vec_f32 v = vec_load(&in[i]);
        vec_f32 result = func(v);
        vec_store(&out[i], result);
    }
    #endif
    
    // Process remainder
    for (; i < n; ++i) {
        out[i] = func(in[i]);
    }
}

} // namespace SIMD
} // namespace WRF_SDIRK3

#endif // WRF_SDIRK3_SIMD_PORTABLE_H