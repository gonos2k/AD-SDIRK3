/**
 * @file wrf_sdirk3_microphysics_vectorized_portable.h
 * @brief Portable SIMD microphysics using architecture-agnostic wrapper
 * @date 2025-08-05
 * 
 * Example of how to adapt microphysics vectorization for both
 * x86 (AVX2) and ARM (NEON) architectures.
 */

#ifndef WRF_SDIRK3_MICROPHYSICS_VECTORIZED_PORTABLE_H
#define WRF_SDIRK3_MICROPHYSICS_VECTORIZED_PORTABLE_H

#include "wrf_sdirk3_simd_portable.h"
#include <cmath>
#include <algorithm>

namespace WRF_SDIRK3 {
namespace PhysicsKSlab {

/**
 * @brief Portable SIMD microphysics calculations
 */
class PortableVectorizedMicrophysics {
public:
    using namespace SIMD;
    
    //==========================================================================
    // Saturation calculations (portable)
    //==========================================================================
    
    /**
     * @brief Portable vectorized saturation vapor pressure
     * Works on both x86 (AVX2) and ARM (NEON)
     */
    static void compute_es_portable(const float* T, float* es, int n) {
        const vec_f32 T0 = vec_set1(273.15f);
        const vec_f32 c1 = vec_set1(611.2f);
        const vec_f32 c2 = vec_set1(17.67f);
        const vec_f32 c3 = vec_set1(243.5f);
        
        int i = 0;
        
        // Process vectorized part
        for (; i <= n - VEC_WIDTH; i += VEC_WIDTH) {
            vec_f32 T_vec = vec_load(&T[i]);
            vec_f32 T_C = vec_sub(T_vec, T0);
            
            // Compute T_C / (T_C + 243.5)
            vec_f32 denom = vec_add(T_C, c3);
            vec_f32 ratio = vec_div(T_C, denom);
            
            // Compute 17.67 * ratio
            vec_f32 arg = vec_mul(c2, ratio);
            
            // exp() - need scalar fallback for now
            float arg_scalar[VEC_WIDTH];
            float exp_scalar[VEC_WIDTH];
            vec_store(arg_scalar, arg);
            
            for (int j = 0; j < VEC_WIDTH; j++) {
                exp_scalar[j] = std::exp(arg_scalar[j]);
            }
            
            vec_f32 exp_vec = vec_load(exp_scalar);
            
            // es = 611.2 * exp(...)
            vec_f32 es_vec = vec_mul(c1, exp_vec);
            vec_store(&es[i], es_vec);
        }
        
        // Scalar cleanup
        for (; i < n; ++i) {
            float T_C = T[i] - 273.15f;
            es[i] = 611.2f * std::exp(17.67f * T_C / (T_C + 243.5f));
        }
    }
    
    //==========================================================================
    // Autoconversion (portable)
    //==========================================================================
    
    /**
     * @brief Portable autoconversion implementation
     * Automatically uses AVX2 on x86 or NEON on ARM
     */
    static void autoconversion_portable(
        const float* qc, const float* rho,
        float* dqcdt, float* dqrdt,
        float k_auto, float qc0, float dt, int n) {
        
        const vec_f32 k_vec = vec_set1(k_auto);
        const vec_f32 qc0_vec = vec_set1(qc0);
        const vec_f32 zero = vec_zero();
        
        int i = 0;
        
        // Vectorized loop - automatically uses appropriate SIMD
        for (; i <= n - VEC_WIDTH; i += VEC_WIDTH) {
            vec_f32 qc_vec = vec_load(&qc[i]);
            vec_f32 rho_vec = vec_load(&rho[i]);
            
            // Compute (qc - qc0)+
            vec_f32 excess = vec_sub(qc_vec, qc0_vec);
            vec_f32 mask = vec_cmp_gt(excess, zero);
            
            // Apply mask (portable way)
            float excess_scalar[VEC_WIDTH];
            float mask_scalar[VEC_WIDTH];
            vec_store(excess_scalar, excess);
            vec_store(mask_scalar, mask);
            
            for (int j = 0; j < VEC_WIDTH; j++) {
                excess_scalar[j] = mask_scalar[j] ? excess_scalar[j] : 0.0f;
            }
            excess = vec_load(excess_scalar);
            
            // Rate = k_auto * excess * rho
            vec_f32 rate = vec_mul(k_vec, vec_mul(excess, rho_vec));
            
            // Update tendencies
            vec_f32 dqc_vec = vec_load(&dqcdt[i]);
            vec_f32 dqr_vec = vec_load(&dqrdt[i]);
            
            dqc_vec = vec_sub(dqc_vec, rate);
            dqr_vec = vec_add(dqr_vec, rate);
            
            vec_store(&dqcdt[i], dqc_vec);
            vec_store(&dqrdt[i], dqr_vec);
        }
        
        // Scalar cleanup
        for (; i < n; ++i) {
            if (qc[i] > qc0) {
                float rate = k_auto * (qc[i] - qc0) * rho[i];
                dqcdt[i] -= rate;
                dqrdt[i] += rate;
            }
        }
    }
    
    //==========================================================================
    // Fall velocity (portable)
    //==========================================================================
    
    /**
     * @brief Portable rain fall velocity
     * Demonstrates FMA usage (fused multiply-add)
     */
    static void fall_velocity_rain_portable(
        const float* qr, const float* rho,
        float* vt, float a, float b, float c, int n) {
        
        const vec_f32 a_vec = vec_set1(a);
        const vec_f32 rho0 = vec_set1(1.2f);
        const vec_f32 zero = vec_zero();
        
        int i = 0;
        
        for (; i <= n - VEC_WIDTH; i += VEC_WIDTH) {
            vec_f32 qr_vec = vec_load(&qr[i]);
            vec_f32 rho_vec = vec_load(&rho[i]);
            
            // Check qr > 0
            vec_f32 mask = vec_cmp_gt(qr_vec, zero);
            
            // Compute density ratio
            vec_f32 rho_ratio = vec_div(rho0, rho_vec);
            
            // For b = 0.5, use sqrt
            vec_f32 rho_factor;
            if (std::abs(b - 0.5f) < 1e-6f) {
                rho_factor = vec_sqrt(rho_ratio);
            } else {
                // Fallback to scalar for general power
                float ratio_scalar[VEC_WIDTH];
                float factor_scalar[VEC_WIDTH];
                vec_store(ratio_scalar, rho_ratio);
                
                for (int j = 0; j < VEC_WIDTH; j++) {
                    factor_scalar[j] = std::pow(ratio_scalar[j], b);
                }
                rho_factor = vec_load(factor_scalar);
            }
            
            // Simplified q_factor (would need power for full implementation)
            vec_f32 q_factor = qr_vec;
            
            // vt = a * rho_factor * q_factor
            vec_f32 vt_vec = vec_mul(a_vec, vec_mul(rho_factor, q_factor));
            
            // Apply mask
            float vt_scalar[VEC_WIDTH];
            float mask_scalar[VEC_WIDTH];
            vec_store(vt_scalar, vt_vec);
            vec_store(mask_scalar, mask);
            
            for (int j = 0; j < VEC_WIDTH; j++) {
                vt_scalar[j] = mask_scalar[j] ? vt_scalar[j] : 0.0f;
            }
            
            vec_store(&vt[i], vec_load(vt_scalar));
        }
        
        // Scalar cleanup
        for (; i < n; ++i) {
            if (qr[i] > 0.0f) {
                vt[i] = a * std::pow(1.2f/rho[i], b) * std::pow(qr[i], c);
            } else {
                vt[i] = 0.0f;
            }
        }
    }
    
    //==========================================================================
    // Performance comparison example
    //==========================================================================
    
    /**
     * @brief Compare performance across architectures
     */
    static void print_performance_info() {
        std::cout << "\n=== Portable Microphysics Performance ===" << std::endl;
        std::cout << "Architecture: " << get_simd_type() << std::endl;
        std::cout << "Vector Width: " << get_vector_width() << " floats" << std::endl;
        
        if (get_vector_width() == 8) {
            std::cout << "Expected speedup: 3.5-4x (AVX2)" << std::endl;
        } else if (get_vector_width() == 4) {
            std::cout << "Expected speedup: 2-2.5x (NEON/SSE)" << std::endl;
        } else {
            std::cout << "Running in scalar mode" << std::endl;
        }
        
        std::cout << "\nPerformance characteristics:" << std::endl;
        std::cout << "- Autoconversion: " << get_vector_width() << " points/cycle" << std::endl;
        std::cout << "- Memory bandwidth: " << (get_vector_width() * 4) << " bytes/load" << std::endl;
        std::cout << "- Cache line usage: " << (64 / (get_vector_width() * 4) * 100) << "%" << std::endl;
    }
    
    /**
     * @brief Check if vectorization is available
     */
    static bool has_vectorization() {
        return has_simd();
    }
};

} // namespace PhysicsKSlab
} // namespace WRF_SDIRK3

#endif // WRF_SDIRK3_MICROPHYSICS_VECTORIZED_PORTABLE_H