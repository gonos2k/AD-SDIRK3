/**
 * @file wrf_config_flags.h
 * @brief WRF namelist configuration flags for SDIRK3 implementation
 * 
 * This file defines the configuration flags structure that matches
 * WRF's grid_config_rec_type for proper namelist option handling.
 * Critical for maintaining exact WRF 1:1 consistency.
 */

#ifndef WRF_CONFIG_FLAGS_H
#define WRF_CONFIG_FLAGS_H

namespace wrf {
namespace sdirk3 {

/**
 * @struct ConfigFlags
 * @brief WRF namelist configuration options
 * 
 * Matches WRF's grid_config_rec_type structure from Registry.EM_COMMON
 * and module_configure.F to ensure exact consistency with WRF behavior.
 */
struct ConfigFlags {
    // ===== THERMODYNAMICS OPTIONS =====
    /**
     * @brief Use moist or dry potential temperature
     * 0 = dry theta (default), 1 = moist theta_m
     * CRITICAL: Affects equation of state and thermodynamic calculations
     */
    int use_theta_m = 0;
    
    // ===== ADVECTION OPTIONS =====
    /**
     * @brief Momentum advection option
     * 1 = standard, 2 = WENO, 3 = monotonic
     */
    int momentum_adv_opt = 1;
    
    /**
     * @brief Scalar advection option
     * 0 = standard, 1 = positive definite, 2 = monotonic
     */
    int scalar_adv_opt = 1;
    
    /**
     * @brief Moisture advection option
     * 0 = standard, 1 = positive definite, 2 = monotonic
     */
    int moist_adv_opt = 1;
    
    /**
     * @brief TKE advection option
     * 0 = standard, 1 = positive definite, 2 = monotonic
     */
    int tke_adv_opt = 1;
    
    /**
     * @brief Horizontal advection order for scalars
     * Typically 3, 4, 5, or 6
     */
    int h_sca_adv_order = 5;
    
    /**
     * @brief Vertical advection order for scalars
     * Typically 2, 3, 4, or 5
     */
    int v_sca_adv_order = 3;
    
    /**
     * @brief Horizontal advection order for momentum
     * Typically 3, 4, 5, or 6
     */
    int h_mom_adv_order = 5;
    
    /**
     * @brief Vertical advection order for momentum
     * Typically 2, 3, 4, or 5
     */
    int v_mom_adv_order = 3;
    
    // ===== PHYSICS SCHEMES =====
    /**
     * @brief Microphysics scheme
     * 0 = none, 1 = Kessler, 2 = Lin, 3 = WSM3, 4 = WSM5, etc.
     */
    int mp_physics = 0;
    
    /**
     * @brief Cumulus parameterization scheme
     * 0 = none, 1 = Kain-Fritsch, 2 = BMJ, 3 = GD, etc.
     */
    int cu_physics = 0;
    
    /**
     * @brief Longwave radiation scheme
     * 0 = none, 1 = RRTM, 4 = RRTMG, etc.
     */
    int ra_lw_physics = 0;
    
    /**
     * @brief Shortwave radiation scheme
     * 0 = none, 1 = Dudhia, 2 = Goddard, 4 = RRTMG, etc.
     */
    int ra_sw_physics = 0;
    
    /**
     * @brief PBL scheme
     * 0 = none, 1 = YSU, 2 = MYJ, 5 = MYNN, etc.
     */
    int bl_pbl_physics = 0;
    
    /**
     * @brief Surface layer scheme
     * 0 = none, 1 = MM5, 2 = Eta, 5 = MYNN, etc.
     */
    int sf_sfclay_physics = 0;
    
    /**
     * @brief Land surface scheme
     * 0 = none, 1 = thermal diffusion, 2 = Noah, 3 = RUC, etc.
     */
    int sf_surface_physics = 0;
    
    // ===== DAMPING OPTIONS =====
    /**
     * @brief Divergence damping option
     * 0 = off, 1 = constant coefficient, 2 = proportional to deformation
     */
    int damp_opt = 0;
    
    /**
     * @brief Vertical velocity damping flag
     * 0 = off, 1 = on
     */
    int w_damping = 0;
    
    /**
     * @brief Upper level damping depth (m)
     * Default: 5000.0
     */
    float zdamp = 5000.0f;
    
    /**
     * @brief Upper level damping coefficient
     * Default: 0.01 to 0.2
     */
    float dampcoef = 0.01f;
    
    /**
     * @brief Divergence damping coefficient (smdiv)
     * Range: 0.0 to 0.2, default: 0.1
     */
    float smdiv = 0.1f;
    
    /**
     * @brief External mode divergence damping (emdiv)
     * Range: 0.0 to 0.2, default: 0.01
     */
    float emdiv = 0.01f;
    
    /**
     * @brief Use max height instead of mean for Rayleigh damping profile
     *
     * When grid.dn is 3D and this flag is true, extract the maximum column
     * height (max over horizontal dims) for the damping profile. This prevents
     * underestimating the model top over high terrain.
     *
     * false = Use horizontal mean of dn (may underestimate over terrain)
     * true  = Use max over horizontal dimensions (consistent with tallest column)
     *
     * Default: false (use mean for backward compatibility)
     */
    bool use_max_height_for_damping = false;

    /**
     * @brief 6th order diffusion option
     * 0 = none, 1 = 6th order on all variables, 2 = 6th order on theta only
     */
    int diff_6th_opt = 0;
    
    /**
     * @brief 6th order diffusion coefficient
     * Typical range: 0.06 to 0.25
     */
    float diff_6th_factor = 0.12f;
    
    // ===== TURBULENCE OPTIONS =====
    /**
     * @brief Eddy coefficient option
     * 0 = constant, 1 = 1.5 order TKE closure, 2 = full TKE, 3 = Smagorinsky
     */
    int km_opt = 1;
    
    /**
     * @brief Vertical diffusion option
     * 0 = off, 1 = on
     */
    int diff_opt = 1;
    
    /**
     * @brief Horizontal diffusion coefficient (m^2/s)
     * Default: 0.0
     */
    float khdif = 0.0f;
    
    /**
     * @brief Vertical diffusion coefficient (m^2/s)
     * Default: 0.0
     */
    float kvdif = 0.0f;
    
    /**
     * @brief Smagorinsky constant for turbulence closure
     * Default: 0.25 (typical for WRF)
     * Range: 0.1-0.3
     */
    float c_s = 0.25f;
    
    /**
     * @brief TKE closure constant
     * Default: 0.2 (typical for WRF)
     * Range: 0.1-0.3
     */
    float c_k = 0.2f;
    
    /**
     * @brief Upper bound for mixing length (meters)
     * Default: 250.0 (typical for WRF)
     * Used in Blackadar mixing length formula
     */
    float mix_upper_bound = 250.0f;
    
    /**
     * @brief Mixing isotropy flag
     * 0 = anisotropic (stability-dependent)
     * 1 = isotropic (no stability correction)
     * Default: 0
     */
    int mix_isotropic = 0;
    
    /**
     * @brief Mix full fields (1) or perturbation (0)
     * Default: 1
     */
    int mix_full_fields = 1;
    
    /**
     * @brief FFT filter latitude for polar filtering
     * Default: 45.0 degrees
     */
    float fft_filter_lat = 45.0f;
    
    // ===== TIME INTEGRATION =====
    /**
     * @brief Acoustic time step mode
     * 1 = constant dt_sound, 2 = variable dt_sound
     */
    int time_step_sound = 1;
    
    /**
     * @brief RK3 time integration option
     * 2 = RK2, 3 = RK3 (default)
     */
    int rk_ord = 3;
    
    // ===== MOISTURE OPTIONS =====
    /**
     * @brief Positive definite advection for moisture
     * 0 = off, 1 = on for qv only, 2 = on for all moisture
     */
    int pd_moist = 0;
    
    /**
     * @brief Positive definite advection for scalars
     * 0 = off, 1 = on
     */
    int pd_scalar = 0;
    
    /**
     * @brief Positive definite advection for TKE
     * 0 = off, 1 = on
     */
    int pd_tke = 0;
    
    // ===== NUMERICAL OPTIONS =====
    /**
     * @brief Base state sea level pressure (Pa)
     * Default: 100000.0
     */
    float base_pres = 100000.0f;
    
    /**
     * @brief Base state temperature (K)
     * Default: 290.0 (can be different from t0=300)
     */
    float base_temp = 290.0f;
    
    /**
     * @brief Base state lapse rate (K/m)
     * Default: 50.0
     */
    float base_lapse = 50.0f;
    
    /**
     * @brief Epssm coefficient for numerical smoothing
     * Typical: 0.1 to 0.5
     */
    float epssm = 0.1f;
    
    /**
     * @brief Non-hydrostatic option
     * 0 = hydrostatic, 1 = non-hydrostatic (default)
     */
    int non_hydrostatic = 1;
    
    // ===== BOUNDARY CONDITIONS =====
    /**
     * @brief Lateral boundary condition option
     * 1 = periodic, 2 = symmetric, 3 = open, 4 = specified
     */
    int open_xs = 0;
    int open_xe = 0;
    int open_ys = 0;
    int open_ye = 0;
    int periodic_x = 0;
    int periodic_y = 0;
    int symmetric_xs = 0;
    int symmetric_xe = 0;
    int symmetric_ys = 0;
    int symmetric_ye = 0;
    int specified = 0;
    int nested = 0;
    
    /**
     * @brief Width of specified BC relaxation zone
     * Default: 5 grid points
     * Range: 1-10
     */
    int spec_zone = 5;
    
    /**
     * @brief Polar filtering flag
     * 0 = no polar filtering
     * 1 = apply polar filtering
     * Default: 0
     */
    int polar = 0;
    
    /**
     * @brief Relaxation zone width for lateral boundaries
     * Default: 4 grid points
     * Used for blending interior with boundary values
     */
    int relax_zone = 4;
    
    // ===== GRID OPTIONS =====
    /**
     * @brief Map projection
     * 0 = ideal, 1 = Lambert, 2 = polar stereographic, 3 = Mercator
     */
    int map_proj = 0;
    
    /**
     * @brief Grid ID for this domain
     */
    int grid_id = 1;

    /**
     * @brief Parent grid ID (for nesting)
     */
    int parent_id = 0;

    // ===== AD (AUTOMATIC DIFFERENTIATION) OPTIONS =====
    /**
     * @brief AD strict mode - enforce gradient checks even in Release builds
     *
     * When enabled (1), requiresGradCheck() in metric_utils.h will throw
     * TORCH_CHECK errors instead of just warnings when requires_grad tensors
     * are used with scalar extraction functions. This prevents silent gradient
     * graph truncation.
     *
     * 0 = Normal mode (throw in Debug, warn in Release)
     * 1 = Strict mode (throw in both Debug and Release)
     *
     * Default: 0
     * Set to 1 for production AD workloads where silent graph breaks are unacceptable.
     */
    int ad_strict_mode = 0;

    // ===== HELPER FUNCTIONS =====
    
    /**
     * @brief Check if using moist potential temperature
     */
    bool uses_moist_theta() const { return use_theta_m == 1; }
    
    /**
     * @brief Check if physics is enabled
     */
    bool has_physics() const {
        return mp_physics > 0 || cu_physics > 0 || 
               ra_lw_physics > 0 || ra_sw_physics > 0 ||
               bl_pbl_physics > 0 || sf_sfclay_physics > 0;
    }
    
    /**
     * @brief Get advection order for scalars
     */
    int get_scalar_adv_order(bool horizontal) const {
        return horizontal ? h_sca_adv_order : v_sca_adv_order;
    }
    
    /**
     * @brief Get advection order for momentum
     */
    int get_momentum_adv_order(bool horizontal) const {
        return horizontal ? h_mom_adv_order : v_mom_adv_order;
    }
    
    /**
     * @brief Check if using positive definite advection
     */
    bool uses_pd_advection() const {
        return scalar_adv_opt == 1 || moist_adv_opt == 1 ||
               pd_moist > 0 || pd_scalar > 0;
    }
    
    /**
     * @brief Check if using monotonic advection
     */
    bool uses_monotonic_advection() const {
        return scalar_adv_opt == 2 || moist_adv_opt == 2;
    }

    /**
     * @brief Check if AD strict mode is enabled
     */
    bool uses_ad_strict_mode() const {
        return ad_strict_mode == 1;
    }
};

// Global config for AD enforcement (can be set at initialization)
// This allows metric_utils.h to check AD strict mode without passing ConfigFlags everywhere
inline bool& g_ad_strict_mode() {
    static bool mode = false;
    return mode;
}

inline void set_ad_strict_mode(bool enabled) {
    g_ad_strict_mode() = enabled;
}

inline bool is_ad_strict_mode() {
    return g_ad_strict_mode();
}

} // namespace sdirk3
} // namespace wrf

#endif // WRF_CONFIG_FLAGS_H