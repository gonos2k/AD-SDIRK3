#ifndef WRF_FLUX_FORM_ADAPTER_H
#define WRF_FLUX_FORM_ADAPTER_H

#include "torch_standalone.h"
#include "unified_state_vector.h"
#include "unified_rhs.h"
#include <memory>

namespace wrf {
namespace sdirk3 {

/**
 * WRF Flux-Form Adapter for SDIRK3 Integration
 * 
 * Bridges existing sdirk3_lib infrastructure with WRF-specific
 * flux-form perturbation equations while preserving:
 * 1. Mass conservation through coupled variables
 * 2. Perturbation separation with stop-gradient
 * 3. Hydrostatic balance constraints
 * 4. C-grid staggered operations
 */

// Extended state variables for flux-form
enum FluxFormVariable {
    // Coupled momentum (mass-weighted)
    U_COUPLED = 0,      // U = μ_d * u
    V_COUPLED = 1,      // V = μ_d * v  
    OMEGA_COUPLED = 2,  // Ω = μ_d * ω
    
    // Standard variables
    W_VELOCITY = 3,     // w (not mass-weighted)
    
    // Coupled scalars
    THETA_M_COUPLED = 4, // Θ_m = μ_d * θ_m
    
    // Perturbation variables
    PHI_PERT = 5,       // φ' (perturbation geopotential)
    MU_DRY = 6,         // μ_d (dry air mass)
    
    // Moisture (coupled)
    Q_VAPOR_COUPLED = 7, // Q_v = μ_d * q_v
    Q_CLOUD_COUPLED = 8, // Q_c = μ_d * q_c
    Q_RAIN_COUPLED = 9,  // Q_r = μ_d * q_r
    
    N_FLUX_VARS = 10
};

// Base state storage (stop-gradient)
struct BaseState {
    torch::Tensor phi_base;      // φ₀(z) - base geopotential
    torch::Tensor p_base;        // p₀(z) - base pressure
    torch::Tensor alpha_d_base;  // α_d₀(z) - base specific volume
    torch::Tensor mu_base;       // μ_d₀ - base column mass
    torch::Tensor theta_base;    // θ₀(z) - base potential temperature
    
    // Make base state stop-gradient
    void detach_all() {
        phi_base = phi_base.detach();
        p_base = p_base.detach();
        alpha_d_base = alpha_d_base.detach();
        mu_base = mu_base.detach();
        theta_base = theta_base.detach();
    }
};

/**
 * FluxFormStateVector: Extends UnifiedStateVector for WRF flux-form
 */
class FluxFormStateVector : public UnifiedStateVector {
private:
    BaseState base_state_;
    bool use_perturbation_form_;
    
    // Staggering information
    torch::Tensor u_stagger_map_;  // i+1/2 locations
    torch::Tensor v_stagger_map_;  // j+1/2 locations
    torch::Tensor w_stagger_map_;  // k+1/2 locations
    
public:
    FluxFormStateVector(
        int its, int ite, int jts, int jte, int kts, int kte,
        int ims, int ime, int jms, int jme, int kms, int kme,
        bool use_perturbation = true
    );
    
    // Transform between coupled and uncoupled variables
    torch::Tensor decouple_momentum(const torch::Tensor& U_coupled, 
                                   const torch::Tensor& mu_d);
    torch::Tensor couple_momentum(const torch::Tensor& u_velocity,
                                 const torch::Tensor& mu_d);
    
    // Get perturbation variables
    torch::Tensor get_perturbation(FluxFormVariable var) const;
    
    // Set with automatic coupling
    void set_velocity(const torch::Tensor& u, const torch::Tensor& v,
                     const torch::Tensor& w, const torch::Tensor& mu_d);
    
    // Access base state (read-only, stop-gradient)
    const BaseState& base_state() const { return base_state_; }
};

/**
 * Custom JVP Rules for Mass-Weighted Variables
 */
class MassWeightedJVP : public torch::autograd::Function<MassWeightedJVP> {
public:
    // Forward: U = μ_d * u
    static torch::Tensor forward(
        torch::autograd::AutogradContext* ctx,
        const torch::Tensor& u,
        const torch::Tensor& mu_d
    ) {
        ctx->save_for_backward({u, mu_d});
        return mu_d * u;
    }
    
    // JVP: δU = u * δμ_d + μ_d * δu
    static std::vector<torch::Tensor> jvp(
        torch::autograd::AutogradContext* ctx,
        const std::vector<torch::Tensor>& grad_inputs
    ) {
        auto saved = ctx->get_saved_variables();
        auto u = saved[0];
        auto mu_d = saved[1];
        
        auto du = grad_inputs[0];
        auto dmu_d = grad_inputs[1];
        
        // JVP formula: δU = u * δμ_d + μ_d * δu
        auto dU = u * dmu_d + mu_d * du;
        
        return {dU};
    }
};

/**
 * FluxFormRHS: Extends UnifiedRHS with WRF atomic operations
 */
class FluxFormRHS : public UnifiedRHS {
private:
    std::shared_ptr<FluxFormStateVector> flux_state_;
    
    // Atomic operation flags
    bool use_custom_jvp_;
    bool freeze_limiters_;
    
public:
    FluxFormRHS(
        std::shared_ptr<WRFGridInfo> grid_info,
        std::shared_ptr<PhysicsConfig> physics_config,
        std::shared_ptr<FluxFormStateVector> flux_state
    );
    
    // Override forward with flux-form operations
    torch::Tensor forward(const torch::Tensor& Phi) override;
    
    // Atomic operations with custom JVP
    torch::Tensor compute_flux_divergence(
        const torch::Tensor& U_coupled,
        const torch::Tensor& V_coupled,
        const torch::Tensor& Omega_coupled,
        const torch::Tensor& scalar
    );
    
    torch::Tensor compute_pressure_eos(
        const torch::Tensor& theta_m,
        const torch::Tensor& alpha_d,
        const torch::Tensor& pb  // Base pressure (stop-gradient)
    );
    
    torch::Tensor solve_hydrostatic(
        const torch::Tensor& alpha_d,
        const torch::Tensor& mu_d
    );
    
    torch::Tensor compute_pressure_gradient_perturbation(
        const torch::Tensor& phi_pert,
        const torch::Tensor& p_pert,
        const torch::Tensor& mu_d,
        const BaseState& base_state
    );
    
    // Boundary condition handling
    void apply_w_damping(torch::Tensor& rw_tend, const torch::Tensor& w);
    void apply_divergence_damping(torch::Tensor& F, const torch::Tensor& div);
};

/**
 * Enhanced Acoustic Preconditioner for WRF
 */
class WRFAcousticPreconditioner : public AcousticPreconditioner {
private:
    // Additional coupling for WRF
    torch::Tensor hydrostatic_operator_;  // ∂_η φ = -α_d μ_d
    torch::Tensor schur_complement_;      // (U,V)↔(W,φ) coupling
    
public:
    WRFAcousticPreconditioner(std::shared_ptr<WRFGridInfo> grid_info)
        : AcousticPreconditioner(grid_info) {}
    
    // Enhanced setup with hydrostatic constraint
    void setup(double dt_gamma) override;
    
    // Solve with (W, φ, μ_d) strong coupling
    torch::Tensor apply(const torch::Tensor& r) override;
    
private:
    // Vertical solve with hydrostatic constraint
    torch::Tensor solve_vertical_hydrostatic(const torch::Tensor& r_w,
                                            const torch::Tensor& r_phi,
                                            const torch::Tensor& r_mu);
};

/**
 * Fortran Interface for WRF Integration
 */
extern "C" {
    // Initialize flux-form adapter
    void* wrf_flux_form_init(
        int its, int ite, int jts, int jte, int kts, int kte,
        int ims, int ime, int jms, int jme, int kms, int kme,
        const float* phi_base, const float* p_base,
        const float* alpha_d_base, const float* mu_base
    );
    
    // Convert WRF variables to flux-form state
    void wrf_pack_state(
        void* adapter,
        float* flux_state,      // Output: packed flux-form state
        const float* u,         // Input: u-velocity (staggered)
        const float* v,         // Input: v-velocity (staggered)
        const float* w,         // Input: w-velocity (staggered)
        const float* theta_m,   // Input: moist potential temperature
        const float* phi,       // Input: geopotential
        const float* mu_d,      // Input: dry air mass
        const float* qv,        // Input: water vapor
        int nx, int ny, int nz
    );
    
    // Convert flux-form state back to WRF variables
    void wrf_unpack_state(
        void* adapter,
        const float* flux_state, // Input: packed flux-form state
        float* u,               // Output: u-velocity (staggered)
        float* v,               // Output: v-velocity (staggered)
        float* w,               // Output: w-velocity (staggered)
        float* theta_m,         // Output: moist potential temperature
        float* phi,             // Output: geopotential
        float* mu_d,            // Output: dry air mass
        float* qv,              // Output: water vapor
        int nx, int ny, int nz
    );
    
    // Compute JVP for Newton-Krylov
    void wrf_compute_jvp(
        void* adapter,
        float* jvp,             // Output: J*v product
        const float* state,     // Input: current state
        const float* vector,    // Input: direction vector
        int n_vars,
        int n_points,
        int use_ad             // 1=AD, 0=FD
    );
}

} // namespace sdirk3
} // namespace wrf

#endif // WRF_FLUX_FORM_ADAPTER_H