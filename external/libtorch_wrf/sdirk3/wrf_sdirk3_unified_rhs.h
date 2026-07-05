#ifndef UNIFIED_RHS_H
#define UNIFIED_RHS_H

#include <torch/torch.h>
#include <memory>
#include <vector>
#include <mutex>  // FIX 2025-01-10 Round2: For rdzw/rdz cache thread safety
#include "wrf_sdirk3_types.h"
#include "wrf_sdirk3_unified_rhs_extended.h"
#include "wrf_config_flags.h"

namespace wrf {
namespace sdirk3 {

// WRF grid information is now defined in wrf_sdirk3_types.h

// Physics configuration - DEPRECATED, use ConfigFlags instead
// Kept for backward compatibility only
class PhysicsConfig {
public:
    // Physics scheme flags
    int mp_physics = 0;      // Microphysics scheme
    int cu_physics = 0;      // Cumulus scheme
    int ra_lw_physics = 0;   // Longwave radiation
    int ra_sw_physics = 0;   // Shortwave radiation
    int bl_pbl_physics = 0;  // PBL scheme
    int sf_sfclay_physics = 0; // Surface layer
    
    // Configuration
    bool use_physics = false; // Master switch for physics
    bool enabled = false;     // General enable flag
    
    // Damping parameters from Registry.EM_SDIRK3_OPTIMIZATIONS namelist
    float rayleigh_damp_coef = 0.2f;      // sdirk3_rayleigh_coef from namelist
    float rayleigh_damp_depth = 5000.0f;  // sdirk3_rayleigh_depth from namelist
    float w_damp_alpha = 0.3f;            // sdirk3_w_damp_alpha from namelist  
    float w_damp_crit_cfl = 1.0f;         // sdirk3_w_crit_cfl from namelist
};

// Physics tendencies from WRF physics parameterizations
struct PhysicsTendencies {
    // Diabatic heating terms (physics coupling)
    torch::Tensor h_diabatic;   // Microphysics latent heating (3D)
    torch::Tensor qv_diabatic;  // Microphysics qv tendency (3D)
    torch::Tensor qc_diabatic;  // Microphysics qc tendency (3D)
    
    // Physics tendencies (already in coupled form: multiplied by (c1*mu+c2))
    // Radiation
    torch::Tensor rthraten;     // Radiation t tendency (3D)
    
    // PBL (Planetary Boundary Layer)
    torch::Tensor rublten;      // PBL u tendency (3D)
    torch::Tensor rvblten;      // PBL v tendency (3D)
    torch::Tensor rthblten;     // PBL t tendency (3D)
    torch::Tensor rqvblten;     // PBL qv tendency (3D)
    torch::Tensor rqcblten;     // PBL qc tendency (3D)
    torch::Tensor rqiblten;     // PBL qi tendency (3D)
    
    // Cumulus
    torch::Tensor rucuten;      // Cumulus u tendency (3D)
    torch::Tensor rvcuten;      // Cumulus v tendency (3D)
    torch::Tensor rthcuten;     // Cumulus t tendency (3D)
    torch::Tensor rqvcuten;     // Cumulus qv tendency (3D)
    torch::Tensor rqccuten;     // Cumulus qc tendency (3D)
    torch::Tensor rqrcuten;     // Cumulus qr tendency (3D)
    torch::Tensor rqicuten;     // Cumulus qi tendency (3D)
    torch::Tensor rqscuten;     // Cumulus qs tendency (3D)
    
    // Shallow cumulus
    torch::Tensor rushten;      // Shallow cumulus u tendency (3D)
    torch::Tensor rvshten;      // Shallow cumulus v tendency (3D)
    torch::Tensor rthshten;     // Shallow cumulus t tendency (3D)
    torch::Tensor rqvshten;     // Shallow cumulus qv tendency (3D)
    torch::Tensor rqcshten;     // Shallow cumulus qc tendency (3D)
    torch::Tensor rqrshten;     // Shallow cumulus qr tendency (3D)
    torch::Tensor rqishten;     // Shallow cumulus qi tendency (3D)
    torch::Tensor rqsshten;     // Shallow cumulus qs tendency (3D)
    torch::Tensor rqgshten;     // Shallow cumulus qg tendency (3D)
    
    // Surface fluxes (2D) - WRF-consistent names
    torch::Tensor hfx;          // Sensible heat flux at surface [W/m^2]
    torch::Tensor qfx;          // Moisture flux at surface [kg/m^2/s]
    torch::Tensor ust;          // Friction velocity [m/s]
    torch::Tensor tsk;          // Skin temperature [K]
    torch::Tensor lh;           // Latent heat flux [W/m^2]
    torch::Tensor grdflx;       // Ground heat flux [W/m^2]
    torch::Tensor cd;           // Drag coefficient for momentum [dimensionless]
    torch::Tensor ck;           // Exchange coefficient for heat [dimensionless]
    torch::Tensor cka;          // Exchange coefficient for moisture [dimensionless]
    torch::Tensor znt;          // Roughness length [m]
};

// Phase 4.7: 3D Batching Geometry Cache
// Purpose: Precomputed 3D geometry tensors for broadcast-compatible operations
// Design: PHASE_4.7_3D_BATCHING_DESIGN.md lines 169-267
struct Geometry3D {
    torch::Tensor dnw_3d;    // [1, nz, 1] - vertical level weights for broadcasting
    torch::Tensor mrdx_3d;   // [nj, 1, ni] - map scale * rdx for broadcasting
    torch::Tensor mrdy_3d;   // [nj, 1, ni] - map scale * rdy for broadcasting
    bool initialized = false;

    // Cache invalidation conditions (see PHASE_4.7_3D_BATCHING_DESIGN.md lines 249-266)
    // - Device migration detected
    // - Grid size changes (nx, ny, nz modified)
    // - Tile/domain reconfiguration
};

// State components for WRF variables
struct WRFStateComponents {
    torch::Tensor u, v, w;      // Velocity components (uncoupled)
    torch::Tensor t;            // Potential temperature (perturbation) - WRF uses 't'
    torch::Tensor phi;          // Geopotential (perturbation)
    torch::Tensor mu;           // Column mass (perturbation, 2D)
    torch::Tensor p;            // Pressure (perturbation)
    
    // WRF coupled momentum variables: U = μd*u/my, V = μd*v/mx, W = μd*w/my
    torch::Tensor U, V, W;      // Coupled momentum (WRF dynamics form)
    torch::Tensor Omega;        // Contravariant vertical velocity
    torch::Tensor rom;          // rom = μ/my
    
    // Moist coupling coefficients
    torch::Tensor cqu, cqv, cqw;     // 1/(1+0.5*qtot) for moist air (U, V, W momentum)
    
    // Full variables (base + perturbation)
    torch::Tensor t_full;       // t_full = t' + t_base
    torch::Tensor p_full;       // p_full = p' + p_base
    torch::Tensor ph_full;      // φ_full = φ' + φ_base
    torch::Tensor mu_full;      // μ_full = μ' + μ_base
    torch::Tensor rho;          // ρ = 1/α (density computed from specific volume)
    
    // Moisture variables
    torch::Tensor q, qc, qr, qi, qs;
    
    // Physics tendencies are now stored separately in PhysicsTendencies structure
    // to cleanly separate dynamics state from physics forcing
    
    // These fields are kept for backward compatibility but should migrate to PhysicsTendencies
    torch::Tensor h_diabatic;   // Microphysics latent heating (3D)
    torch::Tensor qv_diabatic;  // Microphysics qv tendency (3D)
    torch::Tensor qc_diabatic;  // Microphysics qc tendency (3D)
    torch::Tensor rthraten;     // Radiation t tendency (3D)
    torch::Tensor rublten;      // PBL u tendency (3D)
    torch::Tensor rvblten;      // PBL v tendency (3D)
    torch::Tensor rthblten;     // PBL t tendency (3D)
    torch::Tensor rucuten;      // Cumulus u tendency (3D)
    torch::Tensor rvcuten;      // Cumulus v tendency (3D)
    torch::Tensor rthcuten;     // Cumulus t tendency (3D)
    torch::Tensor rushten;      // Shallow cumulus u tendency (3D)
    torch::Tensor rvshten;      // Shallow cumulus v tendency (3D)
    torch::Tensor rthshten;     // Shallow cumulus t tendency (3D)
};

// Main RHS computation class - Unified implicit treatment
class UnifiedRHS : public torch::nn::Module {
protected:
    std::shared_ptr<WRFGridInfo> grid_info_;
    std::shared_ptr<WRFGridInfoExtended> grid_info_ext_;  // Extended grid info for advanced features
    std::shared_ptr<PhysicsConfig> physics_config_;       // DEPRECATED - use config_flags_
    std::shared_ptr<ConfigFlags> config_flags_;           // WRF namelist options
    PhysicsTendencies physics_tendencies_;                // Current physics tendencies including surface fluxes

#ifdef ENABLE_PHASE_4_7_3D_BATCHING
    Geometry3D geom_cache_;  // Phase 4.7: Precomputed 3D geometry tensors
#endif

    // =========================================================================
    // FIX 2025-01-10 Round2: Cache for rdzw/rdz 1D tensors in add_acoustic_terms
    // Avoids repeated CPU→GPU transfers in vectorized divergence computation
    // =========================================================================
    struct RdzwRdzDevCache {
        torch::Tensor tensor;            // [n_interior, 1] for broadcasting
        torch::Device device{torch::kCPU};
        torch::Dtype dtype{torch::kFloat32};
        int64_t cache_size{0};
        int64_t k_start{0};
        // Content signature: first/middle/last values
        double first{0.0};
        double middle{0.0};
        double last{0.0};
        bool valid{false};
    };
    mutable RdzwRdzDevCache rdzw_dev_cache_;
    mutable RdzwRdzDevCache rdz_dev_cache_;
    mutable std::mutex rdzw_rdz_cache_mutex_;

    // NOTE: Acoustic-gravity waves are handled through diagnostic pressure
    // and the enhanced preconditioner with Brunt-Väisälä frequency (N²) terms.
    
    // Boundary handling documentation:
    // The implementation uses WRF's index convention:
    // - Fortran arrays come with indices: its:ite, jts:jte (tile bounds)
    // - C++ arrays are 0-based with size nx = ite-its+1, ny = jte-jts+1
    // - Loop bounds depend on:
    //   * Variable staggering (U staggered in x, V in y, W in z)
    //   * Advection order (5th order needs 3 points, 3rd order needs 2)
    //   * Boundary conditions (periodic, symmetric, open, specified)
    // - The BoundaryHandler class manages these conversions
    
public:
    // OPT Pass33+: Explicit non-copyable/non-movable
    // Reason: mutable std::mutex rdzw_rdz_cache_mutex_ member
    UnifiedRHS(const UnifiedRHS&) = delete;
    UnifiedRHS& operator=(const UnifiedRHS&) = delete;
    UnifiedRHS(UnifiedRHS&&) = delete;
    UnifiedRHS& operator=(UnifiedRHS&&) = delete;

    // Constructor
    UnifiedRHS(std::shared_ptr<WRFGridInfo> grid_info,
               std::shared_ptr<PhysicsConfig> physics_config);
    
    // Set configuration flags (for namelist options)
    void set_config_flags(std::shared_ptr<ConfigFlags> flags) { config_flags_ = flags; }
    std::shared_ptr<ConfigFlags> get_config_flags() { return config_flags_; }
    
    // Set physics tendencies (including surface fluxes)
    void set_physics_tendencies(const PhysicsTendencies& tendencies) { physics_tendencies_ = tendencies; }
    PhysicsTendencies& get_physics_tendencies() { return physics_tendencies_; }

    /**
     * @brief Invalidate rdzw/rdz device caches for grid metric updates
     *
     * FIX 2025-01-10 Round4: Call this method when grid metrics (rdzw, rdz) change
     * at runtime (e.g., adaptive mesh refinement, moving grids). For static grids,
     * the content signature auto-validates, but explicit invalidation is cleaner.
     */
    void invalidate_acoustic_metric_cache() const {
        std::lock_guard<std::mutex> lock(rdzw_rdz_cache_mutex_);
        rdzw_dev_cache_.valid = false;
        rdz_dev_cache_.valid = false;
    }

    // Main RHS computation - unified implicit treatment
    torch::Tensor forward(const torch::Tensor& U);
    
    // Compatibility interface (returns same as forward for unified approach)
    torch::Tensor compute_fast_terms(const torch::Tensor& U);
    
    // Extract state components from packed vector
    WRFStateComponents extract_state_components(const torch::Tensor& U);
    
    // Compute derived quantities
    // CRITICAL: 't' parameter is PERTURBATION potential temperature (theta - t0)
    // These functions internally add t0 (300K) to get full potential temperature
    
    // Main versions with moisture
    torch::Tensor compute_density(const torch::Tensor& p, const torch::Tensor& t,  // t = perturbation
                                  const torch::Tensor& qv);                     // water vapor for moist equation of state
    torch::Tensor compute_alpha(const torch::Tensor& p, const torch::Tensor& t,    // t = perturbation
                                const torch::Tensor& qv);                       // water vapor for moist equation of state
    
    // Overloads for backward compatibility (without moisture)
    torch::Tensor compute_density(const torch::Tensor& p, const torch::Tensor& t); // t = perturbation
    torch::Tensor compute_alpha(const torch::Tensor& p, const torch::Tensor& t);   // t = perturbation
    
    torch::Tensor compute_pressure_from_state(const torch::Tensor& mu, const torch::Tensor& t); // t = perturbation
    
    // WRF equation of state with full moisture treatment
    torch::Tensor compute_pressure_moist(const torch::Tensor& mu, const torch::Tensor& t,  // t = perturbation
                                        const torch::Tensor& qv, const torch::Tensor& qc); // Full moisture
    torch::Tensor compute_moist_potential_temperature(const torch::Tensor& theta,          // Dry potential temp
                                                     const torch::Tensor& qv);              // Water vapor mixing ratio
    
    // Surface flux application
    void apply_surface_fluxes(torch::Tensor& F, const WRFStateComponents& state,
                             const PhysicsTendencies& physics);
    
    // Unified dynamics functions
    void add_momentum_rhs(torch::Tensor& F, const WRFStateComponents& state, 
                          const torch::Tensor& alpha);
    void add_mass_continuity(torch::Tensor& F, const WRFStateComponents& state);
    void add_thermodynamic_rhs(torch::Tensor& F, const WRFStateComponents& state,
                               const torch::Tensor& rho);
    void add_geopotential_rhs(torch::Tensor& F, const WRFStateComponents& state);
    void add_moisture_rhs(torch::Tensor& F, const WRFStateComponents& state);
    
    // Flux operators
    float flux3(float q_im2, float q_im1, float q_i, float q_ip1, float vel);
    float flux4(float q_im2, float q_im1, float q_i, float q_ip1);
    float flux5(float q_im3, float q_im2, float q_im1, float q_i, float q_ip1, float q_ip2, float vel);
    float flux6(float q_im3, float q_im2, float q_im1, float q_i, float q_ip1, float q_ip2);
    
    // AUTOGRAD FIX: Tensor versions for gradient tracking
    torch::Tensor flux3_tensor(const torch::Tensor& q_im2, const torch::Tensor& q_im1, 
                               const torch::Tensor& q_i, const torch::Tensor& q_ip1, 
                               const torch::Tensor& vel);
    torch::Tensor flux4_tensor(const torch::Tensor& q_im2, const torch::Tensor& q_im1, 
                               const torch::Tensor& q_i, const torch::Tensor& q_ip1);
    torch::Tensor flux5_tensor(const torch::Tensor& q_im3, const torch::Tensor& q_im2, 
                               const torch::Tensor& q_im1, const torch::Tensor& q_i, 
                               const torch::Tensor& q_ip1, const torch::Tensor& q_ip2, 
                               const torch::Tensor& vel);
    torch::Tensor flux6_tensor(const torch::Tensor& q_im3, const torch::Tensor& q_im2, 
                               const torch::Tensor& q_im1, const torch::Tensor& q_i, 
                               const torch::Tensor& q_ip1, const torch::Tensor& q_ip2);
    
    // Helper functions for coupled variables
    void compute_coupled_variables(WRFStateComponents& state);
    
    // Performance optimized functions
    void compute_horizontal_advection_u(torch::Tensor& u_tend, const WRFStateComponents& state, 
                                       int i_start, int i_end, int j_start, int j_end);
    void compute_horizontal_advection_v(torch::Tensor& v_tend, const WRFStateComponents& state,
                                       int i_start, int i_end, int j_start, int j_end);
    void compute_horizontal_advection_w(torch::Tensor& w_tend, const WRFStateComponents& state,
                                       int i_start, int i_end, int j_start, int j_end);
    torch::Tensor compute_omega(const WRFStateComponents& state);
    
    // Apply boundary conditions based on type
    void apply_boundary_conditions(torch::Tensor& F, const WRFStateComponents& state);
    
    // Cache-optimized vertical diffusion
    void add_vertical_diffusion_blocked(torch::Tensor& F, const WRFStateComponents& state);
    
    // Check if indices need halo exchange (for future parallel implementation)
    bool needs_halo_exchange(int i, int j, int stencil_size) const;
    
    // Diffusion operators
    void add_horizontal_diffusion_u(torch::Tensor& u_tend, const WRFStateComponents& state);
    void add_horizontal_diffusion_v(torch::Tensor& v_tend, const WRFStateComponents& state);
    void add_horizontal_diffusion_w(torch::Tensor& w_tend, const WRFStateComponents& state);
    void add_vertical_diffusion(torch::Tensor& F, const WRFStateComponents& state);
    
    // Physics tendencies interface
    void add_physics_tendencies(torch::Tensor& F, const WRFStateComponents& state);
    void add_microphysics_tendencies(torch::Tensor& F, const torch::Tensor& U);
    
    // Legacy interface stubs (for compatibility only)
    void add_advection_terms(torch::Tensor& F, const torch::Tensor& u,
                            const torch::Tensor& v, const torch::Tensor& w,
                            const torch::Tensor& rho, const torch::Tensor& theta);
    void add_coriolis_terms(torch::Tensor& F, const torch::Tensor& u,
                           const torch::Tensor& v);
    void add_pressure_gradient_slow(torch::Tensor& F, const torch::Tensor& theta,
                                   const torch::Tensor& phi);
    void add_acoustic_terms(torch::Tensor& F, const torch::Tensor& rho,
                           const torch::Tensor& u, const torch::Tensor& v,
                           const torch::Tensor& w, const torch::Tensor& mu,
                           const torch::Tensor& phi);
    void add_buoyancy_terms(torch::Tensor& F, const torch::Tensor& theta,
                           const torch::Tensor& rho);
    void add_pressure_gradient_fast(torch::Tensor& F, const torch::Tensor& phi);
    void add_divergence_damping(torch::Tensor& F, const torch::Tensor& u,
                               const torch::Tensor& v, const torch::Tensor& w);
    void add_external_mode_filter(torch::Tensor& F, const torch::Tensor& mu);
    void add_acoustic_divergence(torch::Tensor& F, const torch::Tensor& rho,
                               const torch::Tensor& u, const torch::Tensor& v, 
                               const torch::Tensor& w);
    void add_acoustic_pressure_gradient(torch::Tensor& F, const torch::Tensor& rho,
                                      const torch::Tensor& phi, const torch::Tensor& mu);
    void add_vertical_acoustic_coupling(torch::Tensor& F, const torch::Tensor& w,
                                      const torch::Tensor& phi);
    void add_compressible_heating(torch::Tensor& F, const torch::Tensor& u,
                                const torch::Tensor& v, const torch::Tensor& w,
                                const torch::Tensor& t);
    void add_metric_terms(torch::Tensor& F, const torch::Tensor& u,
                        const torch::Tensor& v, const torch::Tensor& w);
    void add_horizontal_diffusion(torch::Tensor& F, const torch::Tensor& u,
                                const torch::Tensor& v, const torch::Tensor& w,
                                const torch::Tensor& t);
    // Removed duplicate declaration - use WRFStateComponents version
    void add_upper_damping(torch::Tensor& F, const WRFStateComponents& state);
    
    // CRITICAL FIX Bug #15-16: Add pressure evolution equation for acoustic modes
    void add_pressure_evolution_rhs(torch::Tensor& F, const WRFStateComponents& state);

private:
#ifdef ENABLE_PHASE_4_7_3D_BATCHING
    // Phase 4.7: 3D Batching Geometry Initialization
    // Design: PHASE_4.7_3D_BATCHING_DESIGN.md lines 169-267
    // Purpose: Precompute 3D geometry tensors with device/dtype safety
    void initialize_geometry_3d(const WRFStateComponents& state);
#endif
};

} // namespace sdirk3
} // namespace wrf

#endif // UNIFIED_RHS_H