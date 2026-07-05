/**
 * @file wrf_sdirk3_wsm6_kslab.h
 * @brief WSM6 microphysics scheme with k-slab optimization
 * @date 2025-08-05
 * 
 * Implements the WRF Single-Moment 6-class (WSM6) microphysics scheme
 * with k-slab optimization for improved cache performance and vectorization.
 * 
 * WSM6 includes: vapor, cloud, rain, ice, snow, graupel
 */

#ifndef WRF_SDIRK3_WSM6_KSLAB_H
#define WRF_SDIRK3_WSM6_KSLAB_H

#include "wrf_sdirk3_physics_kslab_base.h"
#include "wrf_sdirk3_microphysics_vectorized.h"
#include <memory>
#include <optional>  // OPT Pass33+: For warm_mask_all_true cache parameter

namespace WRF_SDIRK3 {
namespace PhysicsKSlab {

/**
 * @brief WSM6 microphysics constants
 */
struct WSM6Constants {
    // Densities [kg/m³]
    static constexpr float rho_w = 1000.0f;    // Water
    static constexpr float rho_s = 100.0f;     // Snow
    static constexpr float rho_g = 500.0f;     // Graupel
    
    // Intercept parameters [m⁻⁴]
    static constexpr float n0_r = 8.0e6f;      // Rain
    static constexpr float n0_s = 2.0e6f;      // Snow
    static constexpr float n0_g = 4.0e6f;      // Graupel
    
    // Size distribution parameters
    static constexpr float lambda_r_min = 1.0e-6f;
    static constexpr float lambda_r_max = 1.0e20f;
    
    // Process rate constants
    static constexpr float k_auto = 1.0e-3f;   // Autoconversion rate
    static constexpr float qc0 = 1.0e-3f;      // Autoconversion threshold
    
    // Thermodynamic constants
    static constexpr float cp = 1005.0f;       // Specific heat [J/kg/K]
    static constexpr float cv = 718.0f;        // Specific heat [J/kg/K]
    static constexpr float Rd = 287.0f;        // Gas constant [J/kg/K]
    static constexpr float Rv = 461.5f;        // Water vapor constant
    static constexpr float Lv = 2.5e6f;        // Latent heat vaporization
    static constexpr float Lf = 3.34e5f;       // Latent heat fusion
    static constexpr float Ls = Lv + Lf;       // Latent heat sublimation
    
    // Temperature thresholds
    static constexpr float T0 = 273.15f;       // Freezing point [K]
    static constexpr float T_warm = 268.15f;   // Warm process threshold
    static constexpr float T_cold = 238.15f;   // Cold process threshold
};

/**
 * @brief WSM6 microphysics k-slab implementation
 */
class WSM6KSlab : public PhysicsKSlabOperation {
private:
    // Vectorized microphysics utilities
    std::unique_ptr<VectorizedMicrophysics> vec_micro_;
    
    // Working arrays for k-slab processing
    struct WorkArrays {
        torch::Tensor lambda_r;    // Rain size parameter
        torch::Tensor lambda_s;    // Snow size parameter
        torch::Tensor lambda_g;    // Graupel size parameter
        torch::Tensor vt_r;        // Rain fall velocity
        torch::Tensor vt_s;        // Snow fall velocity
        torch::Tensor vt_g;        // Graupel fall velocity
        
        void allocate(int k_size, int ny, int nx, 
                     const torch::TensorOptions& options);
    };
    
    // Process-specific methods
    void warm_rain_processes(
        PhysicsState& state,
        PhysicsTendency& tend,
        WorkArrays& work,
        int k_start, int k_size);
    
    void ice_processes(
        PhysicsState& state,
        PhysicsTendency& tend,
        WorkArrays& work,
        int k_start, int k_size);
    
    void snow_processes(
        PhysicsState& state,
        PhysicsTendency& tend,
        WorkArrays& work,
        int k_start, int k_size);
    
    void graupel_processes(
        PhysicsState& state,
        PhysicsTendency& tend,
        WorkArrays& work,
        int k_start, int k_size);
    
    void melting_freezing(
        PhysicsState& state,
        PhysicsTendency& tend,
        WorkArrays& work,
        int k_start, int k_size);
    
    void compute_sedimentation(
        PhysicsState& state,
        PhysicsTendency& tend,
        WorkArrays& work,
        int k_start, int k_size);
    
    // Collection processes
    // FIX 2025-01-03: Added optional warm_mask to avoid clone() in warm_rain_processes
    // OPT Pass33+: Added warm_mask_all_true cache parameter - caller can pre-compute warm_mask.all().item<bool>()
    //              and pass to all WSM6 functions to avoid redundant .item() D2H transfers
    void compute_autoconversion(
        torch::Tensor& qc, torch::Tensor& qr,
        torch::Tensor& dqcdt, torch::Tensor& dqrdt,
        const torch::Tensor& rho,
        int k_start, int k_size,
        const torch::Tensor& warm_mask = {},
        std::optional<bool> warm_mask_all_true = std::nullopt);

    void compute_accretion(
        const torch::Tensor& qc, const torch::Tensor& qr,
        torch::Tensor& dqcdt, torch::Tensor& dqrdt,
        const torch::Tensor& rho,
        int k_start, int k_size,
        const torch::Tensor& warm_mask = {},
        std::optional<bool> warm_mask_all_true = std::nullopt);
    
    void compute_collection_ice_snow(
        const torch::Tensor& qi, const torch::Tensor& qs,
        torch::Tensor& dqidt, torch::Tensor& dqsdt,
        const torch::Tensor& T, const torch::Tensor& rho,
        int k_start, int k_size);
    
    // Vectorized helper functions
    void compute_size_parameters(
        const torch::Tensor& q, const torch::Tensor& rho,
        torch::Tensor& lambda, float n0, float rho_x);
    
    void apply_latent_heating(
        torch::Tensor& T, const torch::Tensor& dq,
        float L_heat, float dt);
    
public:
    /**
     * @brief Constructor
     */
    explicit WSM6KSlab(const PhysicsKSlabConfig& config);
    
    /**
     * @brief Process a k-slab of WSM6 microphysics
     */
    void process_slab(
        PhysicsState& state,
        PhysicsTendency& tendency,
        int k_start, int k_size,
        const AdvancedKSlab::AdaptiveKSlabConfig& kslab_config) override;
    
    /**
     * @brief Compute fall velocities for a specific species
     * @param species The hydrometeor species (RAIN, SNOW, GRAUPEL)
     */
    void compute_fall_velocities(
        torch::Tensor& vt_array,
        const torch::Tensor& q_array,
        const torch::Tensor& rho,
        int k_start, int k_size,
        HydrometeorSpecies species) override;
    
    /**
     * @brief Get scheme-specific metrics
     */
    void get_metrics(std::unordered_map<std::string, double>& metrics) const override;
};

/**
 * @brief Factory function for WSM6 k-slab
 */
inline std::unique_ptr<WSM6KSlab> create_wsm6_kslab(
    const PhysicsKSlabConfig& config = PhysicsKSlabConfig()) {
    return std::make_unique<WSM6KSlab>(config);
}

} // namespace PhysicsKSlab
} // namespace WRF_SDIRK3

#endif // WRF_SDIRK3_WSM6_KSLAB_H