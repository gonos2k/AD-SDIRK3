#ifndef WRF_SPECIALIZED_PRECONDITIONERS_H
#define WRF_SPECIALIZED_PRECONDITIONERS_H

#include "wrf_sdirk3_preconditioner_advanced.h"
#include <torch/torch.h>

namespace wrf {
namespace sdirk3 {

/**
 * Specialized Preconditioners for WRF Physics
 */

// =============================================================================
// Acoustic Mode Preconditioner
// =============================================================================

class AcousticModePreconditioner : public AdvancedPreconditioner {
private:
    // Grid parameters
    int nx_, ny_, nz_;
    float dx_, dy_;
    torch::Tensor dz_;
    
    // Physical parameters
    float c_sound_ = 340.0f;
    float rho_ref_ = 1.225f;
    
    // Vertical structure
    torch::Tensor N2_profile_;  // Brunt-Väisälä frequency squared
    torch::Tensor rho_profile_;  // Reference density profile
    torch::Tensor p_profile_;    // Reference pressure profile
    
    // Modal decomposition
    torch::Tensor vertical_modes_;
    torch::Tensor modal_eigenvalues_;
    int n_modes_ = 10;
    
    // Implicit acoustic parameters
    float dt_;
    float gamma_;
    
public:
    AcousticModePreconditioner(int nx, int ny, int nz,
                              float dx, float dy, const torch::Tensor& dz,
                              const torch::Tensor& base_state);
    
    torch::Tensor apply(const torch::Tensor& r) override;
    void update(const torch::Tensor& state, float dt, float gamma) override;
    size_t get_memory_usage() const override;
    
private:
    void compute_vertical_modes();
    void setup_reference_profiles(const torch::Tensor& base_state);
    torch::Tensor apply_modal_preconditioner(const torch::Tensor& r);
    torch::Tensor transform_to_modal_space(const torch::Tensor& field);
    torch::Tensor transform_from_modal_space(const torch::Tensor& modal_field);
};

// =============================================================================
// Orographic Preconditioner (for terrain-following coordinates)
// =============================================================================

class OrographicPreconditioner : public AdvancedPreconditioner {
private:
    // Terrain information
    torch::Tensor terrain_height_;
    torch::Tensor terrain_gradient_x_;
    torch::Tensor terrain_gradient_y_;
    torch::Tensor metric_terms_;
    
    // Coordinate transformation
    torch::Tensor jacobian_;
    torch::Tensor jacobian_inv_;
    
    // Terrain-following coordinate parameters
    float eta_top_ = 0.0f;
    float p_top_ = 5000.0f;  // Pa
    
    // Base preconditioner for transformed system
    std::unique_ptr<AdvancedPreconditioner> base_preconditioner_;
    
public:
    OrographicPreconditioner(int nx, int ny, int nz,
                            float dx, float dy,
                            const torch::Tensor& terrain,
                            std::unique_ptr<AdvancedPreconditioner> base_precond);
    
    torch::Tensor apply(const torch::Tensor& r) override;
    void update(const torch::Tensor& state, float dt, float gamma) override;
    size_t get_memory_usage() const override;
    
private:
    void setup_terrain_metrics(const torch::Tensor& terrain);
    torch::Tensor transform_to_computational_space(const torch::Tensor& physical);
    torch::Tensor transform_to_physical_space(const torch::Tensor& computational);
    void compute_metric_terms();
};

// =============================================================================
// Boundary Layer Preconditioner
// =============================================================================

class BoundaryLayerPreconditioner : public AdvancedPreconditioner {
private:
    // PBL parameters
    float pbl_height_ = 1000.0f;  // m
    torch::Tensor mixing_length_;
    torch::Tensor eddy_diffusivity_;
    torch::Tensor richardson_number_;
    
    // Surface layer parameters
    float z0_ = 0.1f;  // Roughness length
    float u_star_ = 0.3f;  // Friction velocity
    float L_obukhov_ = -100.0f;  // Obukhov length
    
    // Vertical structure
    enum PBLType {
        STABLE,
        NEUTRAL,
        UNSTABLE,
        TRANSITION
    } pbl_type_;
    
    // Special treatment zones
    int n_surface_layers_ = 5;
    int n_entrainment_layers_ = 3;
    
public:
    BoundaryLayerPreconditioner(int nx, int ny, int nz,
                               float dx, float dy, const torch::Tensor& dz,
                               const torch::Tensor& surface_fluxes);
    
    torch::Tensor apply(const torch::Tensor& r) override;
    void update(const torch::Tensor& state, float dt, float gamma) override;
    size_t get_memory_usage() const override;
    
private:
    void diagnose_pbl_type(const torch::Tensor& state);
    void compute_mixing_length_profile();
    void compute_eddy_diffusivity(const torch::Tensor& state);
    torch::Tensor apply_surface_layer_correction(const torch::Tensor& r);
    torch::Tensor apply_entrainment_correction(const torch::Tensor& r);
};

// =============================================================================
// Moist Physics Preconditioner
// =============================================================================

class MoistPhysicsPreconditioner : public AdvancedPreconditioner {
private:
    // Thermodynamic parameters
    float Lv_ = 2.5e6f;  // Latent heat of vaporization
    float Rv_ = 461.5f;  // Gas constant for water vapor
    float Rd_ = 287.0f;  // Gas constant for dry air
    float cp_ = 1004.0f; // Specific heat at constant pressure
    
    // Saturation properties
    torch::Tensor qv_sat_;
    torch::Tensor dqv_sat_dT_;
    torch::Tensor relative_humidity_;
    
    // Cloud fraction and phase
    torch::Tensor cloud_fraction_;
    torch::Tensor liquid_fraction_;
    torch::Tensor ice_fraction_;
    
    // Microphysics coupling
    bool has_cloud_water_ = true;
    bool has_rain_ = true;
    bool has_ice_ = true;
    bool has_snow_ = true;
    bool has_graupel_ = false;
    
    // Phase change time scales
    torch::Tensor condensation_rate_;
    torch::Tensor evaporation_rate_;
    torch::Tensor freezing_rate_;
    
public:
    MoistPhysicsPreconditioner(int nx, int ny, int nz,
                              const torch::Tensor& temperature,
                              const torch::Tensor& pressure,
                              const torch::Tensor& qv);
    
    torch::Tensor apply(const torch::Tensor& r) override;
    void update(const torch::Tensor& state, float dt, float gamma) override;
    size_t get_memory_usage() const override;
    
private:
    void compute_saturation_properties(const torch::Tensor& T, const torch::Tensor& p);
    void diagnose_cloud_properties(const torch::Tensor& state);
    torch::Tensor apply_phase_change_coupling(const torch::Tensor& r);
    torch::Tensor clausius_clapeyron_correction(const torch::Tensor& r_theta, 
                                               const torch::Tensor& r_qv);
};

// =============================================================================
// Radiation Preconditioner
// =============================================================================

class RadiationPreconditioner : public AdvancedPreconditioner {
private:
    // Radiation parameters
    float solar_constant_ = 1370.0f;  // W/m²
    float albedo_surface_ = 0.2f;
    torch::Tensor optical_depth_;
    torch::Tensor heating_rate_;
    
    // Time scales
    float radiation_timestep_ = 1800.0f;  // 30 minutes
    float thermal_relaxation_time_ = 86400.0f;  // 1 day
    
    // Spectral bands (simplified)
    int n_sw_bands_ = 6;  // Shortwave
    int n_lw_bands_ = 16; // Longwave
    
    // Cloud-radiation interaction
    torch::Tensor cloud_optical_properties_;
    
public:
    RadiationPreconditioner(int nx, int ny, int nz,
                           const torch::Tensor& temperature_profile,
                           const torch::Tensor& cloud_fraction);
    
    torch::Tensor apply(const torch::Tensor& r) override;
    void update(const torch::Tensor& state, float dt, float gamma) override;
    size_t get_memory_usage() const override;
    
private:
    void compute_optical_properties(const torch::Tensor& state);
    torch::Tensor apply_radiative_relaxation(const torch::Tensor& r);
    float compute_radiative_timescale(int k_level);
};

// =============================================================================
// Nested Domain Preconditioner
// =============================================================================

class NestedDomainPreconditioner : public AdvancedPreconditioner {
private:
    // Nesting configuration
    struct NestInfo {
        int parent_ratio;
        int i_parent_start, j_parent_start;
        int nest_i_start, nest_i_end;
        int nest_j_start, nest_j_end;
        torch::Tensor interpolation_weights;
    };
    
    std::vector<NestInfo> nests_;
    int current_nest_level_;
    
    // Boundary zone treatment
    int n_boundary_zones_ = 5;
    torch::Tensor boundary_weights_;
    
    // Inter-grid transfer operators
    torch::Tensor restriction_op_;
    torch::Tensor prolongation_op_;
    
    // Preconditioners for each nest level
    std::vector<std::unique_ptr<AdvancedPreconditioner>> nest_preconditioners_;
    
public:
    NestedDomainPreconditioner(const std::vector<NestInfo>& nest_config);
    
    torch::Tensor apply(const torch::Tensor& r) override;
    void update(const torch::Tensor& state, float dt, float gamma) override;
    size_t get_memory_usage() const override;
    
private:
    void setup_inter_grid_operators();
    torch::Tensor apply_boundary_zone_treatment(const torch::Tensor& r, int nest_level);
    torch::Tensor transfer_to_parent(const torch::Tensor& nest_field, int nest_level);
    torch::Tensor transfer_to_nest(const torch::Tensor& parent_field, int nest_level);
};

// =============================================================================
// Convection-Permitting Preconditioner
// =============================================================================

class ConvectionPermittingPreconditioner : public AdvancedPreconditioner {
private:
    // Convective parameters
    float CAPE_threshold_ = 1000.0f;  // J/kg
    float CIN_threshold_ = 50.0f;     // J/kg
    torch::Tensor vertical_velocity_scale_;
    torch::Tensor convective_timescale_;
    
    // Convective structure
    torch::Tensor updraft_mask_;
    torch::Tensor downdraft_mask_;
    torch::Tensor cloud_top_height_;
    torch::Tensor cloud_base_height_;
    
    // Microphysics coupling strength
    torch::Tensor condensation_loading_;
    torch::Tensor precipitation_loading_;
    
    // Turbulence parameters
    torch::Tensor tke_;  // Turbulent kinetic energy
    torch::Tensor mixing_length_;
    
public:
    ConvectionPermittingPreconditioner(int nx, int ny, int nz,
                                      float dx, float dy, const torch::Tensor& dz);
    
    torch::Tensor apply(const torch::Tensor& r) override;
    void update(const torch::Tensor& state, float dt, float gamma) override;
    size_t get_memory_usage() const override;
    
private:
    void diagnose_convective_structure(const torch::Tensor& state);
    void compute_convective_timescales(const torch::Tensor& state);
    torch::Tensor apply_convective_scale_separation(const torch::Tensor& r);
    torch::Tensor apply_updraft_downdraft_coupling(const torch::Tensor& r);
};

// =============================================================================
// Combined Physics-Aware Preconditioner
// =============================================================================

class PhysicsAwarePreconditioner : public AdvancedPreconditioner {
private:
    // Component preconditioners
    std::unique_ptr<AcousticModePreconditioner> acoustic_;
    std::unique_ptr<BoundaryLayerPreconditioner> pbl_;
    std::unique_ptr<MoistPhysicsPreconditioner> moist_;
    std::unique_ptr<OrographicPreconditioner> orographic_;
    
    // Coupling weights
    torch::Tensor acoustic_weight_;
    torch::Tensor pbl_weight_;
    torch::Tensor moist_weight_;
    
    // Problem diagnosis
    struct PhysicsRegime {
        bool is_stably_stratified;
        bool has_active_convection;
        bool has_strong_shear;
        bool has_complex_terrain;
        bool is_moist_saturated;
        float dominant_timescale;
    } physics_regime_;
    
public:
    PhysicsAwarePreconditioner(int nx, int ny, int nz,
                              float dx, float dy, const torch::Tensor& dz,
                              const torch::Tensor& terrain,
                              const torch::Tensor& base_state);
    
    torch::Tensor apply(const torch::Tensor& r) override;
    void update(const torch::Tensor& state, float dt, float gamma) override;
    size_t get_memory_usage() const override;
    
private:
    void diagnose_physics_regime(const torch::Tensor& state);
    void compute_coupling_weights();
    torch::Tensor apply_weighted_combination(const torch::Tensor& r);
};

// =============================================================================
// Factory for WRF-specific Preconditioners
// =============================================================================

class WRFPreconditionerFactory {
public:
    enum WRFPreconditionerType {
        ACOUSTIC_OPTIMIZED,
        TERRAIN_FOLLOWING,
        BOUNDARY_LAYER,
        MOIST_PHYSICS,
        RADIATION_AWARE,
        NESTED_DOMAIN,
        CONVECTION_PERMITTING,
        PHYSICS_AWARE_COMBINED,
        AUTO_SELECT
    };
    
    static std::unique_ptr<AdvancedPreconditioner> create(
        WRFPreconditionerType type,
        int nx, int ny, int nz,
        float dx, float dy, const torch::Tensor& dz,
        const std::unordered_map<std::string, torch::Tensor>& config_data);
    
    /**
     * Auto-select best preconditioner based on problem configuration
     */
    static WRFPreconditionerType auto_select_type(
        const std::unordered_map<std::string, torch::Tensor>& config_data);
    
private:
    static bool has_complex_terrain(const torch::Tensor& terrain);
    static bool has_active_moist_physics(const torch::Tensor& qv);
    static float estimate_convective_activity(const torch::Tensor& state);
};

} // namespace sdirk3
} // namespace wrf

#endif // WRF_SPECIALIZED_PRECONDITIONERS_H