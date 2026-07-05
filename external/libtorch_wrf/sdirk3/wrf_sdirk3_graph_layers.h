#ifndef WRF_SDIRK3_GRAPH_LAYERS_H
#define WRF_SDIRK3_GRAPH_LAYERS_H

#include <torch/torch.h>
#include <memory>
#include <unordered_map>

namespace wrf {
namespace sdirk3 {

/**
 * WRF SDIRK3 Computational Graph Layers
 * 
 * Separates computation into three layers:
 * - M-layer: Invariant metrics (no-grad, cached)
 * - A-layer: Pure dynamics (AD-friendly)
 * - P-layer: Non-smooth physics (FD or smoothed AD)
 */

// ============================================================================
// M-Layer: Metrics and Invariants (cached, no gradient)
// ============================================================================
struct MetricsCache {
    // Grid spacing
    torch::Tensor dx, dy, dz;  // [nx], [ny], [nz]
    
    // Cell volumes and face areas
    torch::Tensor vol;          // [nx, ny, nz]
    torch::Tensor area_x;       // [nx+1, ny, nz] 
    torch::Tensor area_y;       // [nx, ny+1, nz]
    torch::Tensor area_z;       // [nx, ny, nz+1]
    
    // Map scale factors
    torch::Tensor msfuy, msfvx, msfty;  // [nx, ny]
    
    // Dry air mass and coupling coefficients
    torch::Tensor mu_dry;       // [nx, ny] - column dry air mass
    torch::Tensor rdnw, rdn;    // [nz] - vertical metric
    torch::Tensor c1h, c2h;     // [nz] - vertical interpolation
    
    // Terrain and coordinate metrics
    torch::Tensor phb;          // [nx, ny, nz+1] - base geopotential
    torch::Tensor pb;           // [nx, ny, nz] - base pressure
    torch::Tensor theta_ref;    // [nz] - reference potential temperature
    
    // Filter coefficients (W-damping, divergence damping)
    torch::Tensor w_damp_coef;  // Vertical velocity damping
    torch::Tensor div_damp_coef; // Divergence damping
    
    // Boundary relaxation coefficients
    torch::Tensor spec_bdy_width;
    torch::Tensor relax_zone;   // [nx, ny] - relaxation weights
    
    // Constructor to ensure all tensors are detached
    MetricsCache() = default;
    
    void detach_all() {
        dx = dx.detach().requires_grad_(false);
        dy = dy.detach().requires_grad_(false);
        dz = dz.detach().requires_grad_(false);
        vol = vol.detach().requires_grad_(false);
        area_x = area_x.detach().requires_grad_(false);
        area_y = area_y.detach().requires_grad_(false);
        area_z = area_z.detach().requires_grad_(false);
        msfuy = msfuy.detach().requires_grad_(false);
        msfvx = msfvx.detach().requires_grad_(false);
        msfty = msfty.detach().requires_grad_(false);
        mu_dry = mu_dry.detach().requires_grad_(false);
        if (rdnw.defined()) rdnw = rdnw.detach().requires_grad_(false);
        if (rdn.defined()) rdn = rdn.detach().requires_grad_(false);
        if (c1h.defined()) c1h = c1h.detach().requires_grad_(false);
        if (c2h.defined()) c2h = c2h.detach().requires_grad_(false);
        if (phb.defined()) phb = phb.detach().requires_grad_(false);
        if (pb.defined()) pb = pb.detach().requires_grad_(false);
        if (theta_ref.defined()) theta_ref = theta_ref.detach().requires_grad_(false);
        if (w_damp_coef.defined()) w_damp_coef = w_damp_coef.detach().requires_grad_(false);
        if (div_damp_coef.defined()) div_damp_coef = div_damp_coef.detach().requires_grad_(false);
        if (spec_bdy_width.defined()) spec_bdy_width = spec_bdy_width.detach().requires_grad_(false);
        if (relax_zone.defined()) relax_zone = relax_zone.detach().requires_grad_(false);
    }
};

// ============================================================================
// Smoothing Functions for Non-smooth Operations
// ============================================================================
namespace smooth {

/**
 * Smooth approximation of clamp/clip function
 * clip(x, a, b) ≈ a + softplus(x-a, β) - softplus(x-b, β)
 */
inline torch::Tensor soft_clamp(const torch::Tensor& x, 
                                float min_val, float max_val,
                                float beta = 10.0f) {
    auto lower = torch::nn::functional::softplus(x - min_val, beta);
    auto upper = torch::nn::functional::softplus(x - max_val, beta);
    return min_val + lower - upper;
}

/**
 * Smooth absolute value
 * |x| ≈ sqrt(x² + ε²)
 */
inline torch::Tensor soft_abs(const torch::Tensor& x, float eps = 1e-6f) {
    return torch::sqrt(x.square() + eps * eps);
}

/**
 * Smooth maximum
 * max(x, y) ≈ 0.5 * (x + y + soft_abs(x - y))
 */
inline torch::Tensor soft_max(const torch::Tensor& x, 
                              const torch::Tensor& y,
                              float eps = 1e-6f) {
    return 0.5f * (x + y + soft_abs(x - y, eps));
}

/**
 * Smooth minimum
 * min(x, y) ≈ 0.5 * (x + y - soft_abs(x - y))
 */
inline torch::Tensor soft_min(const torch::Tensor& x,
                              const torch::Tensor& y,
                              float eps = 1e-6f) {
    return 0.5f * (x + y - soft_abs(x - y, eps));
}

/**
 * Smooth sign function using tanh
 * sign(x) ≈ tanh(β * x)
 */
inline torch::Tensor soft_sign(const torch::Tensor& x, float beta = 10.0f) {
    return torch::tanh(beta * x);
}

/**
 * Smooth step function using sigmoid
 * step(x > threshold) ≈ sigmoid(β * (x - threshold))
 */
inline torch::Tensor soft_step(const torch::Tensor& x, 
                               float threshold = 0.0f,
                               float beta = 10.0f) {
    return torch::sigmoid(beta * (x - threshold));
}

/**
 * Minmod limiter with smoothing
 * minmod(a, b) = sign(a) * min(|a|, |b|) if sign(a) = sign(b), else 0
 */
inline torch::Tensor soft_minmod(const torch::Tensor& a,
                                 const torch::Tensor& b,
                                 float eps = 1e-6f,
                                 float beta = 10.0f) {
    auto sign_a = soft_sign(a, beta);
    auto sign_b = soft_sign(b, beta);
    auto sign_agree = 0.5f * (1.0f + sign_a * sign_b);  // 1 if same sign, 0 if opposite
    auto min_abs = soft_min(soft_abs(a, eps), soft_abs(b, eps), eps);
    return sign_agree * sign_a * min_abs;
}

} // namespace smooth

// ============================================================================
// A-Layer: Pure Dynamics (AD-friendly operations)
// ============================================================================
class DynamicsLayer {
private:
    const MetricsCache& metrics;
    bool use_smoothing;
    float smoothing_beta;
    
    // Cache for reuse across multiple JVP evaluations
    mutable torch::Tensor cached_U_face;
    mutable torch::Tensor cached_wave_speeds;
    mutable torch::Tensor cached_limiters;
    mutable bool has_cache = false;
    
public:
    DynamicsLayer(const MetricsCache& m, bool smooth = true, float beta = 10.0f)
        : metrics(m), use_smoothing(smooth), smoothing_beta(beta) {}
    
    /**
     * Face reconstruction with optional smoothing
     * U[nvar, nx, ny, nz] -> U_face[nvar, 6, nx*, ny*, nz*]
     * 6 faces: x-, x+, y-, y+, z-, z+
     */
    torch::Tensor reconstruct_faces(const torch::Tensor& U) const;
    
    /**
     * Riemann solver for flux computation
     * Returns flux at cell interfaces
     */
    torch::Tensor riemann_flux(const torch::Tensor& U_left,
                               const torch::Tensor& U_right,
                               int direction) const;
    
    /**
     * Flux divergence computation
     * F_x[nvar, nx+1, ny, nz], F_y[nvar, nx, ny+1, nz], F_z[nvar, nx, ny, nz+1]
     * -> div_F[nvar, nx, ny, nz]
     */
    torch::Tensor compute_divergence(const torch::Tensor& F_x,
                                     const torch::Tensor& F_y,
                                     const torch::Tensor& F_z) const;
    
    /**
     * Dynamics source terms (pressure gradient, Coriolis, diffusion)
     */
    torch::Tensor compute_sources(const torch::Tensor& U) const;
    
    /**
     * Complete dynamics residual R_dyn(U)
     */
    torch::Tensor residual(const torch::Tensor& U) const;
    
    void clear_cache() {
        has_cache = false;
        cached_U_face = torch::Tensor();
        cached_wave_speeds = torch::Tensor();
        cached_limiters = torch::Tensor();
    }
};

// ============================================================================
// P-Layer: Physics (Non-smooth, typically FD-JVP)
// ============================================================================
class PhysicsLayer {
private:
    const MetricsCache& metrics;
    bool freeze_limiters;
    bool use_smoothing;
    
    // Frozen coefficients for consistent linearization
    mutable torch::Tensor frozen_autoconv_mask;
    mutable torch::Tensor frozen_stability_func;
    mutable torch::Tensor frozen_pbl_height;
    mutable bool coeffs_frozen = false;
    
public:
    PhysicsLayer(const MetricsCache& m, bool freeze = true, bool smooth = false)
        : metrics(m), freeze_limiters(freeze), use_smoothing(smooth) {}
    
    /**
     * Microphysics tendencies with optional smoothing/freezing
     */
    torch::Tensor compute_microphysics(const torch::Tensor& U) const;
    
    /**
     * PBL mixing tendencies
     */
    torch::Tensor compute_pbl(const torch::Tensor& U) const;
    
    /**
     * Surface layer fluxes
     */
    torch::Tensor compute_surface(const torch::Tensor& U) const;
    
    /**
     * Complete physics residual R_phys(U)
     */
    torch::Tensor residual(const torch::Tensor& U) const;
    
    void freeze_coefficients(const torch::Tensor& U_ref) {
        // Compute and freeze limiter coefficients at reference state
        coeffs_frozen = true;
        // Implementation depends on specific physics schemes
    }
    
    void unfreeze_coefficients() {
        coeffs_frozen = false;
        frozen_autoconv_mask = torch::Tensor();
        frozen_stability_func = torch::Tensor();
        frozen_pbl_height = torch::Tensor();
    }
};

// ============================================================================
// Complete Graph Manager
// ============================================================================
class ComputationalGraph {
private:
    std::shared_ptr<MetricsCache> metrics;
    std::unique_ptr<DynamicsLayer> dynamics;
    std::unique_ptr<PhysicsLayer> physics;
    
    // Configuration
    bool use_ad_dynamics = true;
    bool use_ad_physics = false;
    bool smooth_dynamics = true;
    bool freeze_physics = true;
    
public:
    ComputationalGraph(int nx, int ny, int nz) {
        metrics = std::make_shared<MetricsCache>();
        initialize_metrics(nx, ny, nz);
        dynamics = std::make_unique<DynamicsLayer>(*metrics, smooth_dynamics);
        physics = std::make_unique<PhysicsLayer>(*metrics, freeze_physics);
    }
    
    void initialize_metrics(int nx, int ny, int nz);
    
    /**
     * Complete residual computation R(U) = R_dyn(U) + R_phys(U)
     */
    torch::Tensor compute_residual(const torch::Tensor& U) const {
        auto R_dyn = dynamics->residual(U);
        auto R_phys = physics->residual(U);
        return R_dyn + R_phys;
    }
    
    /**
     * JVP computation using forward-mode AD for dynamics
     * and finite difference for physics
     */
    torch::Tensor compute_jvp_hybrid(const torch::Tensor& U,
                                     const torch::Tensor& v) const;
    
    /**
     * Pure AD-based JVP (requires smoothing in physics)
     */
    torch::Tensor compute_jvp_ad(const torch::Tensor& U,
                                 const torch::Tensor& v) const;
    
    void set_dynamics_smoothing(bool smooth) {
        smooth_dynamics = smooth;
        dynamics = std::make_unique<DynamicsLayer>(*metrics, smooth);
    }
    
    void set_physics_mode(bool use_ad, bool freeze) {
        use_ad_physics = use_ad;
        freeze_physics = freeze;
        physics = std::make_unique<PhysicsLayer>(*metrics, freeze, use_ad);
    }
    
    MetricsCache& get_metrics() { return *metrics; }
};

} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_GRAPH_LAYERS_H