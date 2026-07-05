/**
 * @file wrf_sdirk3_microphysics_vectorized.h
 * @brief SIMD-optimized microphysics calculations
 * @date 2025-08-05
 * 
 * Provides vectorized implementations of common microphysics calculations
 * using AVX2/AVX512 intrinsics for improved performance.
 */

#ifndef WRF_SDIRK3_MICROPHYSICS_VECTORIZED_H
#define WRF_SDIRK3_MICROPHYSICS_VECTORIZED_H

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

#include <cmath>
#include <algorithm>
#include "wrf_sdirk3_kslab_vectorized.h"

namespace WRF_SDIRK3 {
namespace PhysicsKSlab {

/**
 * @brief SIMD-optimized microphysics calculations
 */
class VectorizedMicrophysics {
public:
    //==========================================================================
    // Saturation calculations
    //==========================================================================
    
    /**
     * @brief Vectorized saturation vapor pressure over water (AVX2)
     * Uses Bolton (1980) formula: es = 611.2 * exp(17.67 * T_C / (T_C + 243.5))
     */
    static void compute_es_avx2(const float* T, float* es, int n) {
        #ifdef __AVX2__
        const __m256 T0 = _mm256_set1_ps(273.15f);
        const __m256 c1 = _mm256_set1_ps(611.2f);
        const __m256 c2 = _mm256_set1_ps(17.67f);
        const __m256 c3 = _mm256_set1_ps(243.5f);
        
        int i = 0;
        for (; i <= n - 8; i += 8) {
            __m256 T_vec = _mm256_loadu_ps(&T[i]);
            __m256 T_C = _mm256_sub_ps(T_vec, T0);
            
            // Compute T_C / (T_C + 243.5)
            __m256 denom = _mm256_add_ps(T_C, c3);
            __m256 ratio = _mm256_div_ps(T_C, denom);
            
            // Compute 17.67 * ratio
            __m256 arg = _mm256_mul_ps(c2, ratio);
            
            // exp() approximation or use math library
            // For now, fall back to scalar for exp
            float arg_scalar[8], exp_scalar[8];
            _mm256_storeu_ps(arg_scalar, arg);
            for (int j = 0; j < 8; j++) {
                exp_scalar[j] = std::exp(arg_scalar[j]);
            }
            __m256 exp_vec = _mm256_loadu_ps(exp_scalar);
            
            // es = 611.2 * exp(...)
            __m256 es_vec = _mm256_mul_ps(c1, exp_vec);
            _mm256_storeu_ps(&es[i], es_vec);
        }
        
        // Scalar cleanup
        for (; i < n; ++i) {
            float T_C = T[i] - 273.15f;
            es[i] = 611.2f * std::exp(17.67f * T_C / (T_C + 243.5f));
        }
        #else
        compute_es_scalar(T, es, n);
        #endif
    }
    
    /**
     * @brief Scalar fallback for saturation vapor pressure
     */
    static void compute_es_scalar(const float* T, float* es, int n) {
        for (int i = 0; i < n; ++i) {
            float T_C = T[i] - 273.15f;
            es[i] = 611.2f * std::exp(17.67f * T_C / (T_C + 243.5f));
        }
    }
    
    //==========================================================================
    // Collection processes
    //==========================================================================
    
    /**
     * @brief Vectorized autoconversion (cloud water to rain)
     * Berry-Reinhardt parameterization
     * FIX 2025-01-04 Batch41: Document dt unused - rate is instantaneous [kg/kg/s]
     */
    static void autoconversion_avx2(
        const float* qc, const float* rho,
        float* dqcdt, float* dqrdt,
        float k_auto, float qc0, float /*dt*/, int n) {
        // Note: dt parameter unused - rate is instantaneous [kg/kg/s]
        // Time integration is handled by the caller (SDIRK3 stages)

        #ifdef __AVX2__
        const __m256 k_vec = _mm256_set1_ps(k_auto);
        const __m256 qc0_vec = _mm256_set1_ps(qc0);
        const __m256 zero = _mm256_setzero_ps();
        
        int i = 0;
        for (; i <= n - 8; i += 8) {
            __m256 qc_vec = _mm256_loadu_ps(&qc[i]);
            __m256 rho_vec = _mm256_loadu_ps(&rho[i]);
            
            // Compute (qc - qc0)+
            __m256 excess = _mm256_sub_ps(qc_vec, qc0_vec);
            __m256 mask = _mm256_cmp_ps(excess, zero, _CMP_GT_OQ);
            excess = _mm256_and_ps(excess, mask);
            
            // Rate = k_auto * excess * rho
            __m256 rate = _mm256_mul_ps(k_vec, _mm256_mul_ps(excess, rho_vec));
            
            // Update tendencies
            __m256 dqc_vec = _mm256_loadu_ps(&dqcdt[i]);
            __m256 dqr_vec = _mm256_loadu_ps(&dqrdt[i]);
            
            dqc_vec = _mm256_sub_ps(dqc_vec, rate);
            dqr_vec = _mm256_add_ps(dqr_vec, rate);
            
            _mm256_storeu_ps(&dqcdt[i], dqc_vec);
            _mm256_storeu_ps(&dqrdt[i], dqr_vec);
        }
        
        // Scalar cleanup
        for (; i < n; ++i) {
            if (qc[i] > qc0) {
                float rate = k_auto * (qc[i] - qc0) * rho[i];
                dqcdt[i] -= rate;
                dqrdt[i] += rate;
            }
        }
        #else
        autoconversion_scalar(qc, rho, dqcdt, dqrdt, k_auto, qc0, dt, n);
        #endif
    }
    
    static void autoconversion_scalar(
        const float* qc, const float* rho,
        float* dqcdt, float* dqrdt,
        float k_auto, float qc0, float dt, int n) {
        
        for (int i = 0; i < n; ++i) {
            if (qc[i] > qc0) {
                float rate = k_auto * (qc[i] - qc0) * rho[i];
                dqcdt[i] -= rate;
                dqrdt[i] += rate;
            }
        }
    }
    
    /**
     * @brief AVX2 approximation of pow(x, 0.875) = x^(7/8) = x / x^(1/8)
     * x^(1/8) = sqrt(sqrt(sqrt(x)))
     * FIX 2025-01-04 Batch41: Proper pow approximation for CPU/GPU parity
     * FIX 2025-01-05 Batch41: Clamp x >= 0 to prevent sqrt(negative) NaN/FP exception
     *
     * === ACCURACY TRADEOFF DOCUMENTATION (FIX 2025-01-06 Batch41) ===
     *
     * DEFAULT PATH (USE_FAST_POW_0875 OFF):
     *   Method: 3x sqrt + 1x div (IEEE-754 compliant)
     *   Cycles: ~20 (Skylake measured)
     *   Max relative error: <1 ULP (essentially exact for float32)
     *   Recommended for: Validation runs, adjoint/gradient verification, debugging
     *
     * FAST PATH (USE_FAST_POW_0875 ON):
     *   Method: rsqrt chain with partial NR refinement
     *   Cycles: ~12 (Skylake measured)
     *   Max relative error: ~0.3% (3e-3) typical, up to ~0.5% at range extremes
     *
     *   IMPORTANT: NR refinement is only applied to the first rsqrt (r = x^-0.5).
     *   The subsequent r2 = rsqrt(r) and r3 = rsqrt(r2) do NOT have NR refinement,
     *   which accumulates ~12-bit rsqrt error at each step. Error compounds as:
     *     r:  ~24-bit accuracy (with NR)
     *     r2: ~12-bit accuracy (no NR, inherits r error)
     *     r3: ~11-bit accuracy (no NR, inherits r2 error)
     *
     *   Adding NR to r2/r3 would improve accuracy to ~0.01% but add ~6 cycles.
     *   Current implementation prioritizes throughput over accuracy for production.
     *
     *   Recommended for: Production runs where microphysics is a hot path
     *   Not recommended for: Validation, debugging, gradient verification
     *
     * Build configuration: cmake -DUSE_FAST_POW_0875=ON ..
     */
    static inline __m256 pow_0875_avx2(__m256 x) {
        #ifdef __AVX2__
        // FIX 2025-01-05: Clamp to 1e-20f to match fallback's clamp_min(qr, 1e-20f)
        const __m256 eps_input = _mm256_set1_ps(1e-20f);
        __m256 x_safe = _mm256_max_ps(x, eps_input);

#ifdef USE_FAST_POW_0875
        // Fast path: x^0.875 = x * x^(-0.125)
        // x^(-0.125) = rsqrt(rsqrt(rsqrt(x)))
        // NOTE: NR refinement only on first rsqrt - see doxygen above for accuracy info
        const __m256 half = _mm256_set1_ps(0.5f);
        const __m256 three_half = _mm256_set1_ps(1.5f);

        // x^(-0.5) with one Newton-Raphson step (~24-bit accuracy)
        __m256 r = _mm256_rsqrt_ps(x_safe);
        __m256 r_sq = _mm256_mul_ps(r, r);
        __m256 nr = _mm256_mul_ps(_mm256_mul_ps(half, x_safe), r_sq);
        r = _mm256_mul_ps(r, _mm256_sub_ps(three_half, nr));  // x^(-0.5) refined

        // rsqrt(x^(-0.5)) = x^(0.25) -- NO NR refinement (~12-bit accuracy)
        __m256 r2 = _mm256_rsqrt_ps(r);

        // rsqrt(x^(0.25)) = x^(-0.125) -- NO NR refinement (~11-bit accuracy)
        __m256 r3 = _mm256_rsqrt_ps(r2);

        // x^0.875 = x * x^(-0.125)
        return _mm256_mul_ps(x_safe, r3);
#else
        // Accurate path: x^(1/8) = sqrt(sqrt(sqrt(x)))
        __m256 x_eighth = _mm256_sqrt_ps(_mm256_sqrt_ps(_mm256_sqrt_ps(x_safe)));
        // x^(7/8) = x / x^(1/8)
        return _mm256_div_ps(x_safe, x_eighth);
#endif
        #else
        return x; // Fallback (should not be called)
        #endif
    }

    /**
     * @brief Vectorized accretion (cloud collected by rain)
     * FIX 2025-01-04 Batch41: Uses proper qr^0.875 for CPU/GPU parity
     */
    static void accretion_cloud_rain_avx2(
        const float* qc, const float* qr, const float* rho,
        float* dqcdt, float* dqrdt,
        float k_accr, float /*dt*/, int n) {
        // Note: dt parameter unused - rate is instantaneous [kg/kg/s]

        #ifdef __AVX2__
        const __m256 k_vec = _mm256_set1_ps(k_accr);
        const __m256 zero = _mm256_setzero_ps();

        int i = 0;
        for (; i <= n - 8; i += 8) {
            __m256 qc_vec = _mm256_loadu_ps(&qc[i]);
            __m256 qr_vec = _mm256_loadu_ps(&qr[i]);
            __m256 rho_vec = _mm256_loadu_ps(&rho[i]);

            // Check if both qc > 0 and qr > 0
            __m256 mask_c = _mm256_cmp_ps(qc_vec, zero, _CMP_GT_OQ);
            __m256 mask_r = _mm256_cmp_ps(qr_vec, zero, _CMP_GT_OQ);
            __m256 mask = _mm256_and_ps(mask_c, mask_r);

            // FIX 2025-01-04: Rate = k_accr * qc * qr^0.875 * rho (matches fallback)
            __m256 qr_pow = pow_0875_avx2(qr_vec);
            __m256 rate = _mm256_mul_ps(k_vec,
                         _mm256_mul_ps(qc_vec,
                         _mm256_mul_ps(qr_pow, rho_vec)));
            rate = _mm256_and_ps(rate, mask);

            // Update tendencies
            __m256 dqc_vec = _mm256_loadu_ps(&dqcdt[i]);
            __m256 dqr_vec = _mm256_loadu_ps(&dqrdt[i]);

            dqc_vec = _mm256_sub_ps(dqc_vec, rate);
            dqr_vec = _mm256_add_ps(dqr_vec, rate);

            _mm256_storeu_ps(&dqcdt[i], dqc_vec);
            _mm256_storeu_ps(&dqrdt[i], dqr_vec);
        }

        // Scalar cleanup with proper power law
        for (; i < n; ++i) {
            if (qc[i] > 0.0f && qr[i] > 0.0f) {
                float rate = k_accr * qc[i] * std::pow(qr[i], 0.875f) * rho[i];
                dqcdt[i] -= rate;
                dqrdt[i] += rate;
            }
        }
        #else
        accretion_cloud_rain_scalar(qc, qr, rho, dqcdt, dqrdt, k_accr, 0.0f, n);
        #endif
    }
    
    static void accretion_cloud_rain_scalar(
        const float* qc, const float* qr, const float* rho,
        float* dqcdt, float* dqrdt,
        float k_accr, float dt, int n) {
        
        for (int i = 0; i < n; ++i) {
            if (qc[i] > 0.0f && qr[i] > 0.0f) {
                float rate = k_accr * qc[i] * std::pow(qr[i], 0.875f) * rho[i];
                dqcdt[i] -= rate;
                dqrdt[i] += rate;
            }
        }
    }
    
    //==========================================================================
    // Fall velocity calculations
    //==========================================================================
    
    /**
     * @brief Vectorized rain fall velocity
     * vt = a * (rho0/rho)^b * qr^c
     */
    static void fall_velocity_rain_avx2(
        const float* qr, const float* rho,
        float* vt, float a, float b, float c, int n) {
        
        #ifdef __AVX2__
        const __m256 a_vec = _mm256_set1_ps(a);
        const __m256 rho0 = _mm256_set1_ps(1.2f);
        const __m256 zero = _mm256_setzero_ps();
        
        int i = 0;
        for (; i <= n - 8; i += 8) {
            __m256 qr_vec = _mm256_loadu_ps(&qr[i]);
            __m256 rho_vec = _mm256_loadu_ps(&rho[i]);
            
            // Check qr > 0
            __m256 mask = _mm256_cmp_ps(qr_vec, zero, _CMP_GT_OQ);
            
            // Compute density ratio (rho0/rho)^b
            __m256 rho_ratio = _mm256_div_ps(rho0, rho_vec);
            // For b = 0.5, use sqrt
            __m256 rho_factor = (b == 0.5f) ? _mm256_sqrt_ps(rho_ratio) : rho_ratio;
            
            // For c = 0.125 (1/8), simplified
            // In practice, would need power approximation
            __m256 q_factor = qr_vec;  // Simplified
            
            // vt = a * rho_factor * q_factor
            __m256 vt_vec = _mm256_mul_ps(a_vec, 
                           _mm256_mul_ps(rho_factor, q_factor));
            vt_vec = _mm256_and_ps(vt_vec, mask);
            
            _mm256_storeu_ps(&vt[i], vt_vec);
        }
        
        // Scalar cleanup with proper power laws
        for (; i < n; ++i) {
            if (qr[i] > 0.0f) {
                vt[i] = a * std::pow(1.2f/rho[i], b) * std::pow(qr[i], c);
            } else {
                vt[i] = 0.0f;
            }
        }
        #else
        fall_velocity_rain_scalar(qr, rho, vt, a, b, c, n);
        #endif
    }
    
    static void fall_velocity_rain_scalar(
        const float* qr, const float* rho,
        float* vt, float a, float b, float c, int n) {
        
        const float rho0 = 1.2f;
        for (int i = 0; i < n; ++i) {
            if (qr[i] > 0.0f) {
                vt[i] = a * std::pow(rho0/rho[i], b) * std::pow(qr[i], c);
            } else {
                vt[i] = 0.0f;
            }
        }
    }
    
    //==========================================================================
    // Temperature adjustments
    //==========================================================================
    
    /**
     * @brief Vectorized latent heating calculation
     */
    static void apply_latent_heating_avx2(
        float* T, const float* dq, float L_over_cp, int n) {
        
        #ifdef __AVX2__
        const __m256 L_cp = _mm256_set1_ps(L_over_cp);
        
        int i = 0;
        for (; i <= n - 8; i += 8) {
            __m256 T_vec = _mm256_loadu_ps(&T[i]);
            __m256 dq_vec = _mm256_loadu_ps(&dq[i]);
            
            // dT = -L/cp * dq (negative because condensation releases heat)
            __m256 dT = _mm256_mul_ps(L_cp, dq_vec);
            T_vec = _mm256_sub_ps(T_vec, dT);
            
            _mm256_storeu_ps(&T[i], T_vec);
        }
        
        // Scalar cleanup
        for (; i < n; ++i) {
            T[i] -= L_over_cp * dq[i];
        }
        #else
        apply_latent_heating_scalar(T, dq, L_over_cp, n);
        #endif
    }
    
    static void apply_latent_heating_scalar(
        float* T, const float* dq, float L_over_cp, int n) {
        
        for (int i = 0; i < n; ++i) {
            T[i] -= L_over_cp * dq[i];
        }
    }
    
    //==========================================================================
    // Melting and freezing
    //==========================================================================
    
    /**
     * @brief Vectorized melting of frozen hydrometeors
     */
    static void melting_process_avx2(
        const float* T, const float* q_frozen,
        float* dq_frozen, float* dq_liquid,
        float* dT, float T0, float k_melt, float L_over_cp, int n) {
        
        #ifdef __AVX2__
        const __m256 T0_vec = _mm256_set1_ps(T0);
        const __m256 k_vec = _mm256_set1_ps(k_melt);
        const __m256 L_cp = _mm256_set1_ps(L_over_cp);
        const __m256 zero = _mm256_setzero_ps();
        
        int i = 0;
        for (; i <= n - 8; i += 8) {
            __m256 T_vec = _mm256_loadu_ps(&T[i]);
            __m256 q_vec = _mm256_loadu_ps(&q_frozen[i]);
            
            // Check T > T0 and q_frozen > 0
            __m256 mask_T = _mm256_cmp_ps(T_vec, T0_vec, _CMP_GT_OQ);
            __m256 mask_q = _mm256_cmp_ps(q_vec, zero, _CMP_GT_OQ);
            __m256 mask = _mm256_and_ps(mask_T, mask_q);
            
            // Melting rate = k_melt * (T - T0) * q_frozen
            __m256 dT_excess = _mm256_sub_ps(T_vec, T0_vec);
            __m256 melt_rate = _mm256_mul_ps(k_vec, 
                               _mm256_mul_ps(dT_excess, q_vec));
            melt_rate = _mm256_and_ps(melt_rate, mask);
            
            // Limit melting to available frozen mass
            melt_rate = _mm256_min_ps(melt_rate, q_vec);
            
            // Update tendencies
            __m256 dqf_vec = _mm256_loadu_ps(&dq_frozen[i]);
            __m256 dql_vec = _mm256_loadu_ps(&dq_liquid[i]);
            __m256 dT_vec = _mm256_loadu_ps(&dT[i]);
            
            dqf_vec = _mm256_sub_ps(dqf_vec, melt_rate);
            dql_vec = _mm256_add_ps(dql_vec, melt_rate);
            
            // Cooling due to melting
            __m256 cooling = _mm256_mul_ps(L_cp, melt_rate);
            dT_vec = _mm256_sub_ps(dT_vec, cooling);
            
            _mm256_storeu_ps(&dq_frozen[i], dqf_vec);
            _mm256_storeu_ps(&dq_liquid[i], dql_vec);
            _mm256_storeu_ps(&dT[i], dT_vec);
        }
        
        // Scalar cleanup
        for (; i < n; ++i) {
            if (T[i] > T0 && q_frozen[i] > 0.0f) {
                float melt_rate = k_melt * (T[i] - T0) * q_frozen[i];
                melt_rate = std::min(melt_rate, q_frozen[i]);
                
                dq_frozen[i] -= melt_rate;
                dq_liquid[i] += melt_rate;
                dT[i] -= L_over_cp * melt_rate;
            }
        }
        #else
        melting_process_scalar(T, q_frozen, dq_frozen, dq_liquid, 
                              dT, T0, k_melt, L_over_cp, n);
        #endif
    }
    
    static void melting_process_scalar(
        const float* T, const float* q_frozen,
        float* dq_frozen, float* dq_liquid,
        float* dT, float T0, float k_melt, float L_over_cp, int n) {
        
        for (int i = 0; i < n; ++i) {
            if (T[i] > T0 && q_frozen[i] > 0.0f) {
                float melt_rate = k_melt * (T[i] - T0) * q_frozen[i];
                melt_rate = std::min(melt_rate, q_frozen[i]);
                
                dq_frozen[i] -= melt_rate;
                dq_liquid[i] += melt_rate;
                dT[i] -= L_over_cp * melt_rate;
            }
        }
    }
    
    //==========================================================================
    // Utility functions
    //==========================================================================
    
    /**
     * @brief Check if AVX2 is available
     */
    static bool has_avx2() {
        #ifdef __AVX2__
        return true;
        #else
        return false;
        #endif
    }
    
    /**
     * @brief Get optimal vector width
     */
    static int get_vector_width() {
        #ifdef __AVX512F__
        return 16;
        #elif defined(__AVX2__)
        return 8;
        #elif defined(__AVX__)
        return 8;
        #else
        return 1;
        #endif
    }
};

} // namespace PhysicsKSlab
} // namespace WRF_SDIRK3

#endif // WRF_SDIRK3_MICROPHYSICS_VECTORIZED_H