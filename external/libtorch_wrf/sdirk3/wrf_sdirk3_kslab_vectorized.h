/**
 * @file wrf_sdirk3_kslab_vectorized.h
 * @brief SIMD vectorization utilities for k-slab operations
 * @date 2025-08-05
 * 
 * Provides optimized SIMD implementations for common operations:
 * - Vectorized interpolation
 * - Aligned memory management
 * - Performance measurement utilities
 * - Platform-specific optimizations
 */

#ifndef WRF_SDIRK3_KSLAB_VECTORIZED_H
#define WRF_SDIRK3_KSLAB_VECTORIZED_H

// Platform-specific SIMD headers
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
    #include <immintrin.h>  // For x86 SIMD intrinsics
    #define HAS_X86_SIMD 1
#elif defined(__ARM_NEON) || defined(__aarch64__)
    #include <arm_neon.h>   // For ARM NEON intrinsics
    #define HAS_ARM_SIMD 1
#else
    #define NO_SIMD 1
#endif

#include <memory>
#include <cstring>
#include <algorithm>
#include <iostream>             // FIX Round191: For std::cout
#include "wrf_sdirk3_config.h"  // FIX Round191: For debug_level gating

namespace WRF_SDIRK3 {
namespace AdvancedKSlab {

/**
 * @brief Platform SIMD capabilities detection
 *
 * FIX Round179: print() outputs unconditionally to std::cout.
 * Caller is responsible for gating calls to print() based on debug_level
 * or other configuration. This header intentionally does not depend on
 * wrf_sdirk3_config.h to keep it lightweight for SIMD utility purposes.
 *
 * Recommended usage pattern:
 *   if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
 *       simd_caps.print();
 *   }
 */
struct SIMDCapabilities {
    bool has_sse2 = false;
    bool has_avx = false;
    bool has_avx2 = false;
    bool has_avx512 = false;
    bool has_fma = false;
    int vector_width = 1;  // Number of floats per vector
    
    SIMDCapabilities() {
        detect_capabilities();
    }
    
    void detect_capabilities() {
        #ifdef __SSE2__
        has_sse2 = true;
        vector_width = 4;
        #endif
        
        #ifdef __AVX__
        has_avx = true;
        vector_width = 8;
        #endif
        
        #ifdef __AVX2__
        has_avx2 = true;
        has_fma = true;
        vector_width = 8;
        #endif
        
        #ifdef __AVX512F__
        has_avx512 = true;
        vector_width = 16;
        #endif
    }
    
    // FIX Round191: Add internal debug_level guard to prevent log flood on accidental calls
    void print() const {
        if (wrf::sdirk3::g_sdirk3_config.debug_level < 1) {
            return;  // Silent in production (debug_level=0)
        }
        std::cout << "SIMD Capabilities:" << std::endl;
        std::cout << "  SSE2:    " << (has_sse2 ? "YES" : "NO") << std::endl;
        std::cout << "  AVX:     " << (has_avx ? "YES" : "NO") << std::endl;
        std::cout << "  AVX2:    " << (has_avx2 ? "YES" : "NO") << std::endl;
        std::cout << "  AVX512:  " << (has_avx512 ? "YES" : "NO") << std::endl;
        std::cout << "  FMA:     " << (has_fma ? "YES" : "NO") << std::endl;
        std::cout << "  Width:   " << vector_width << " floats" << std::endl;
    }
};

/**
 * @brief Aligned memory allocator for SIMD operations
 */
template<typename T, std::size_t Alignment = 32>
class AlignedAllocator {
public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    
    template<typename U>
    struct rebind {
        using other = AlignedAllocator<U, Alignment>;
    };
    
    AlignedAllocator() noexcept = default;
    
    template<typename U>
    AlignedAllocator(const AlignedAllocator<U, Alignment>&) noexcept {}
    
    pointer allocate(size_type n) {
        if (n > std::size_t(-1) / sizeof(T)) {
            throw std::bad_alloc();
        }
        
        void* ptr = nullptr;
        #ifdef _WIN32
        ptr = _aligned_malloc(n * sizeof(T), Alignment);
        if (!ptr) throw std::bad_alloc();
        #else
        if (posix_memalign(&ptr, Alignment, n * sizeof(T)) != 0) {
            throw std::bad_alloc();
        }
        #endif
        
        return static_cast<pointer>(ptr);
    }
    
    void deallocate(pointer p, size_type) noexcept {
        #ifdef _WIN32
        _aligned_free(p);
        #else
        free(p);
        #endif
    }
};

/**
 * @brief Vectorized interpolation routines
 */
class VectorizedInterpolation {
public:
    /**
     * @brief Linear interpolation between two levels (AVX2)
     */
    static void linear_interp_avx2(
        const float* lower, const float* upper,
        float* result, float weight, int n) {
        
        #ifdef __AVX2__
        __m256 w_lower = _mm256_set1_ps(1.0f - weight);
        __m256 w_upper = _mm256_set1_ps(weight);
        
        int i = 0;
        for (; i <= n - 8; i += 8) {
            __m256 l = _mm256_loadu_ps(&lower[i]);
            __m256 u = _mm256_loadu_ps(&upper[i]);
            __m256 r = _mm256_add_ps(
                _mm256_mul_ps(l, w_lower),
                _mm256_mul_ps(u, w_upper)
            );
            _mm256_storeu_ps(&result[i], r);
        }
        
        // Scalar cleanup
        for (; i < n; ++i) {
            result[i] = (1.0f - weight) * lower[i] + weight * upper[i];
        }
        #else
        linear_interp_scalar(lower, upper, result, weight, n);
        #endif
    }
    
    /**
     * @brief Linear interpolation (scalar fallback)
     */
    static void linear_interp_scalar(
        const float* lower, const float* upper,
        float* result, float weight, int n) {
        
        float w_lower = 1.0f - weight;
        for (int i = 0; i < n; ++i) {
            result[i] = w_lower * lower[i] + weight * upper[i];
        }
    }
    
    /**
     * @brief Cubic interpolation (4-point stencil) with AVX2
     */
    static void cubic_interp_avx2(
        const float* data, float* result,
        int k, float weight, int nx, int ny, int nz) {
        
        #ifdef __AVX2__
        // Cubic weights
        float w0 = -weight * (1.0f - weight) * (2.0f - weight) / 6.0f;
        float w1 = (1.0f + weight) * (1.0f - weight) * (2.0f - weight) / 2.0f;
        float w2 = (1.0f + weight) * weight * (2.0f - weight) / 2.0f;
        float w3 = -(1.0f + weight) * weight * (1.0f - weight) / 6.0f;
        
        __m256 vw0 = _mm256_set1_ps(w0);
        __m256 vw1 = _mm256_set1_ps(w1);
        __m256 vw2 = _mm256_set1_ps(w2);
        __m256 vw3 = _mm256_set1_ps(w3);
        
        for (int j = 0; j < ny; ++j) {
            int i = 0;
            for (; i <= nx - 8; i += 8) {
                // Load 4 levels
                __m256 v0 = (k >= 1) ? 
                    _mm256_loadu_ps(&data[(k-1)*ny*nx + j*nx + i]) :
                    _mm256_loadu_ps(&data[k*ny*nx + j*nx + i]);
                __m256 v1 = _mm256_loadu_ps(&data[k*ny*nx + j*nx + i]);
                __m256 v2 = (k < nz-1) ?
                    _mm256_loadu_ps(&data[(k+1)*ny*nx + j*nx + i]) :
                    _mm256_loadu_ps(&data[k*ny*nx + j*nx + i]);
                __m256 v3 = (k < nz-2) ?
                    _mm256_loadu_ps(&data[(k+2)*ny*nx + j*nx + i]) :
                    v2;
                
                // Interpolate
                __m256 r = _mm256_add_ps(
                    _mm256_add_ps(
                        _mm256_mul_ps(v0, vw0),
                        _mm256_mul_ps(v1, vw1)
                    ),
                    _mm256_add_ps(
                        _mm256_mul_ps(v2, vw2),
                        _mm256_mul_ps(v3, vw3)
                    )
                );
                
                _mm256_storeu_ps(&result[j*nx + i], r);
            }
            
            // Scalar cleanup
            for (; i < nx; ++i) {
                float v0 = (k >= 1) ? data[(k-1)*ny*nx + j*nx + i] : data[k*ny*nx + j*nx + i];
                float v1 = data[k*ny*nx + j*nx + i];
                float v2 = (k < nz-1) ? data[(k+1)*ny*nx + j*nx + i] : data[k*ny*nx + j*nx + i];
                float v3 = (k < nz-2) ? data[(k+2)*ny*nx + j*nx + i] : v2;
                
                result[j*nx + i] = w0*v0 + w1*v1 + w2*v2 + w3*v3;
            }
        }
        #else
        cubic_interp_scalar(data, result, k, weight, nx, ny, nz);
        #endif
    }
    
    static void cubic_interp_scalar(
        const float* data, float* result,
        int k, float weight, int nx, int ny, int nz) {
        
        // Cubic weights
        float w0 = -weight * (1.0f - weight) * (2.0f - weight) / 6.0f;
        float w1 = (1.0f + weight) * (1.0f - weight) * (2.0f - weight) / 2.0f;
        float w2 = (1.0f + weight) * weight * (2.0f - weight) / 2.0f;
        float w3 = -(1.0f + weight) * weight * (1.0f - weight) / 6.0f;
        
        for (int j = 0; j < ny; ++j) {
            for (int i = 0; i < nx; ++i) {
                float v0 = (k >= 1) ? data[(k-1)*ny*nx + j*nx + i] : data[k*ny*nx + j*nx + i];
                float v1 = data[k*ny*nx + j*nx + i];
                float v2 = (k < nz-1) ? data[(k+1)*ny*nx + j*nx + i] : data[k*ny*nx + j*nx + i];
                float v3 = (k < nz-2) ? data[(k+2)*ny*nx + j*nx + i] : v2;
                
                result[j*nx + i] = w0*v0 + w1*v1 + w2*v2 + w3*v3;
            }
        }
    }
};

/**
 * @brief Vectorized math operations
 */
class VectorizedMath {
public:
    /**
     * @brief Vectorized exponential function (AVX2)
     */
    static void exp_avx2(const float* input, float* output, int n) {
        #ifdef __AVX2__
        // Use fast approximation for exponential
        // This is based on the identity: exp(x) = 2^(x/ln(2))
        const __m256 ln2_inv = _mm256_set1_ps(1.44269504088896341f);
        const __m256 c1 = _mm256_set1_ps(0.693147180559945309f);
        const __m256 c2 = _mm256_set1_ps(0.240226506959100712f);
        
        int i = 0;
        for (; i <= n - 8; i += 8) {
            __m256 x = _mm256_loadu_ps(&input[i]);
            
            // Range reduction
            __m256 fx = _mm256_mul_ps(x, ln2_inv);
            __m256 ix = _mm256_round_ps(fx, _MM_FROUND_TO_NEAREST_INT);
            __m256 f = _mm256_sub_ps(x, _mm256_mul_ps(ix, c1));
            f = _mm256_sub_ps(f, _mm256_mul_ps(ix, c2));
            
            // Polynomial approximation
            __m256 y = _mm256_set1_ps(1.0f);
            y = _mm256_add_ps(y, _mm256_mul_ps(f, _mm256_set1_ps(1.0f)));
            y = _mm256_add_ps(y, _mm256_mul_ps(f, _mm256_mul_ps(f, _mm256_set1_ps(0.5f))));
            
            // Scale by 2^ix
            __m256i ixi = _mm256_cvtps_epi32(ix);
            ixi = _mm256_add_epi32(ixi, _mm256_set1_epi32(127));
            ixi = _mm256_slli_epi32(ixi, 23);
            __m256 scale = _mm256_castsi256_ps(ixi);
            
            __m256 result = _mm256_mul_ps(y, scale);
            _mm256_storeu_ps(&output[i], result);
        }
        
        // Scalar cleanup with standard exp
        for (; i < n; ++i) {
            output[i] = std::exp(input[i]);
        }
        #else
        for (int i = 0; i < n; ++i) {
            output[i] = std::exp(input[i]);
        }
        #endif
    }
    
    /**
     * @brief Vectorized reciprocal (AVX2)
     */
    static void reciprocal_avx2(const float* input, float* output, int n) {
        #ifdef __AVX2__
        const __m256 one = _mm256_set1_ps(1.0f);
        
        int i = 0;
        for (; i <= n - 8; i += 8) {
            __m256 x = _mm256_loadu_ps(&input[i]);
            __m256 r = _mm256_div_ps(one, x);
            _mm256_storeu_ps(&output[i], r);
        }
        
        for (; i < n; ++i) {
            output[i] = 1.0f / input[i];
        }
        #else
        for (int i = 0; i < n; ++i) {
            output[i] = 1.0f / input[i];
        }
        #endif
    }
};

/**
 * @brief Performance measurement utilities
 */
class VectorPerformance {
private:
    static constexpr int CACHE_LINE_SIZE = 64;
    
public:
    /**
     * @brief Measure effective memory bandwidth
     */
    static double measure_bandwidth(size_t bytes, double time_seconds) {
        return (bytes / 1e9) / time_seconds;  // GB/s
    }
    
    /**
     * @brief Estimate cache efficiency
     */
    static double estimate_cache_efficiency(size_t accessed_bytes, 
                                          size_t unique_bytes) {
        return static_cast<double>(unique_bytes) / accessed_bytes;
    }
    
    /**
     * @brief Check memory alignment
     */
    template<typename T>
    static bool is_aligned(const T* ptr, size_t alignment = 32) {
        return (reinterpret_cast<uintptr_t>(ptr) % alignment) == 0;
    }
    
    /**
     * @brief Prefetch data for next iteration
     */
    static void prefetch_data(const void* addr, int hint = 0) {
        #if defined(HAS_X86_SIMD) && defined(__AVX2__)
        _mm_prefetch(static_cast<const char*>(addr), _MM_HINT_T0);
        _mm_prefetch(static_cast<const char*>(addr) + CACHE_LINE_SIZE, _MM_HINT_T0);
        #elif defined(HAS_ARM_SIMD)
        __builtin_prefetch(addr, 0, 3);
        __builtin_prefetch(static_cast<const char*>(addr) + CACHE_LINE_SIZE, 0, 3);
        #endif
        (void)hint; // Suppress unused parameter warning
    }
};

/**
 * @brief K-slab memory layout optimizer
 */
class KSlabLayout {
public:
    /**
     * @brief Reorder data for k-slab processing
     * 
     * Converts from [nz, ny, nx] to [num_slabs, slab_size, ny, nx]
     */
    static torch::Tensor reorder_for_kslab(
        const torch::Tensor& input,
        int slab_size) {
        
        int nz = input.size(0);
        int ny = input.size(1);
        int nx = input.size(2);
        
        int num_slabs = (nz + slab_size - 1) / slab_size;
        int padded_nz = num_slabs * slab_size;
        
        // Create output with padding
        auto output = torch::zeros({num_slabs, slab_size, ny, nx}, input.options());
        
        // Copy data in slab order
        // FIX 2025-12-27: Use index_put_ instead of index()= (no-op in C++ PyTorch)
        for (int slab = 0; slab < num_slabs; ++slab) {
            int k_start = slab * slab_size;
            int k_end = std::min(k_start + slab_size, nz);
            int actual_size = k_end - k_start;

            output.index_put_({slab, torch::indexing::Slice(0, actual_size)},
                input.index({torch::indexing::Slice(k_start, k_end)}));
        }
        
        return output;
    }
    
    /**
     * @brief Restore original layout from k-slab order
     */
    static torch::Tensor restore_from_kslab(
        const torch::Tensor& slabbed,
        int original_nz) {
        
        int num_slabs = slabbed.size(0);
        int slab_size = slabbed.size(1);
        int ny = slabbed.size(2);
        int nx = slabbed.size(3);
        
        auto output = torch::zeros({original_nz, ny, nx}, slabbed.options());
        
        for (int slab = 0; slab < num_slabs; ++slab) {
            int k_start = slab * slab_size;
            int k_end = std::min(k_start + slab_size, original_nz);
            int actual_size = k_end - k_start;
            
            // FIX 2025-12-27: Use index_put_ instead of index()= (no-op in C++ PyTorch)
            output.index_put_({torch::indexing::Slice(k_start, k_end)},
                slabbed.index({slab, torch::indexing::Slice(0, actual_size)}));
        }
        
        return output;
    }
};

} // namespace AdvancedKSlab
} // namespace WRF_SDIRK3

#endif // WRF_SDIRK3_KSLAB_VECTORIZED_H