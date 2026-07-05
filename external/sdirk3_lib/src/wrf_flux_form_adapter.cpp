#include "sdirk3/wrf_flux_form_adapter.h"
#include <torch/torch.h>
#include <cmath>

namespace wrf {
namespace sdirk3 {

// ============================================================================
// FluxFormStateVector Implementation
// ============================================================================

FluxFormStateVector::FluxFormStateVector(
    int its, int ite, int jts, int jte, int kts, int kte,
    int ims, int ime, int jms, int jme, int kms, int kme,
    bool use_perturbation)
    : UnifiedStateVector(its, ite, jts, jte, kts, kte, 
                        ims, ime, jms, jme, kms, kme, 
                        N_FLUX_VARS - N_BASE_VARS),
      use_perturbation_form_(use_perturbation) {
    
    // Initialize stagger maps for C-grid
    int nx = ite - its + 1;
    int ny = jte - jts + 1;
    int nz = kte - kts + 1;
    
    // U-staggering: (i+1/2, j, k)
    u_stagger_map_ = torch::zeros({nx+1, ny, nz}, torch::kFloat32);
    
    // V-staggering: (i, j+1/2, k)
    v_stagger_map_ = torch::zeros({nx, ny+1, nz}, torch::kFloat32);
    
    // W-staggering: (i, j, k+1/2)
    w_stagger_map_ = torch::zeros({nx, ny, nz+1}, torch::kFloat32);
}

torch::Tensor FluxFormStateVector::decouple_momentum(
    const torch::Tensor& U_coupled, 
    const torch::Tensor& mu_d) {
    // u = U / μ_d
    // Handle division by zero with small epsilon
    auto mu_safe = torch::where(
        mu_d.abs() < 1e-10,
        torch::ones_like(mu_d) * 1e-10,
        mu_d
    );
    return U_coupled / mu_safe;
}

torch::Tensor FluxFormStateVector::couple_momentum(
    const torch::Tensor& u_velocity,
    const torch::Tensor& mu_d) {
    // U = μ_d * u (using custom JVP rule)
    return MassWeightedJVP::apply(u_velocity, mu_d);
}

torch::Tensor FluxFormStateVector::get_perturbation(FluxFormVariable var) const {
    if (!use_perturbation_form_) {
        return get_variable(static_cast<StateVariable>(var));
    }
    
    auto total = get_variable(static_cast<StateVariable>(var));
    
    switch(var) {
        case PHI_PERT:
            // φ' = φ - φ₀
            return total - base_state_.phi_base.detach();
        
        case MU_DRY:
            // μ_d' = μ_d - μ_d₀
            return total - base_state_.mu_base.detach();
        
        default:
            // Other variables don't have base state separation
            return total;
    }
}

void FluxFormStateVector::set_velocity(
    const torch::Tensor& u, const torch::Tensor& v,
    const torch::Tensor& w, const torch::Tensor& mu_d) {
    
    // Couple momentum using mass-weighted transformation
    auto U = couple_momentum(u, mu_d);
    auto V = couple_momentum(v, mu_d);
    
    // W is not mass-weighted in WRF
    set_variable(static_cast<StateVariable>(U_COUPLED), U);
    set_variable(static_cast<StateVariable>(V_COUPLED), V);
    set_variable(static_cast<StateVariable>(W_VELOCITY), w);
    set_variable(static_cast<StateVariable>(MU_DRY), mu_d);
}

// ============================================================================
// FluxFormRHS Implementation
// ============================================================================

FluxFormRHS::FluxFormRHS(
    std::shared_ptr<WRFGridInfo> grid_info,
    std::shared_ptr<PhysicsConfig> physics_config,
    std::shared_ptr<FluxFormStateVector> flux_state)
    : UnifiedRHS(grid_info, physics_config),
      flux_state_(flux_state),
      use_custom_jvp_(true),
      freeze_limiters_(false) {
}

torch::Tensor FluxFormRHS::forward(const torch::Tensor& Phi) {
    // Initialize residual tensor
    auto R = torch::zeros_like(Phi);
    
    // Extract coupled variables
    auto U = Phi.index({torch::indexing::Slice(), U_COUPLED});
    auto V = Phi.index({torch::indexing::Slice(), V_COUPLED});
    auto W = Phi.index({torch::indexing::Slice(), W_VELOCITY});
    auto Theta_m = Phi.index({torch::indexing::Slice(), THETA_M_COUPLED});
    auto phi_pert = Phi.index({torch::indexing::Slice(), PHI_PERT});
    auto mu_d = Phi.index({torch::indexing::Slice(), MU_DRY});
    
    // Decouple to get velocities
    auto u = flux_state_->decouple_momentum(U, mu_d);
    auto v = flux_state_->decouple_momentum(V, mu_d);
    
    // 1. Compute flux divergence for momentum
    auto flux_div_u = compute_flux_divergence(U, V, torch::zeros_like(U), u);
    auto flux_div_v = compute_flux_divergence(U, V, torch::zeros_like(V), v);
    
    // 2. Compute pressure from EOS
    auto theta_m = flux_state_->decouple_momentum(Theta_m, mu_d);
    auto alpha_d = torch::ones_like(theta_m) / grid_info_->rho_base;  // Simplified
    auto p_pert = compute_pressure_eos(theta_m, alpha_d, 
                                       flux_state_->base_state().p_base);
    
    // 3. Solve hydrostatic balance
    auto phi_hydro = solve_hydrostatic(alpha_d, mu_d);
    
    // 4. Compute pressure gradient (perturbation form)
    auto [pgf_u, pgf_v] = compute_pressure_gradient_perturbation(
        phi_pert, p_pert, mu_d, flux_state_->base_state()
    ).chunk(2, /*dim=*/1);
    
    // 5. Add buoyancy for W equation
    auto buoyancy = grid_info_->g * (theta_m - flux_state_->base_state().theta_base) 
                    / flux_state_->base_state().theta_base;
    
    // 6. Mass continuity
    auto div_V = compute_flux_divergence(U, V, torch::zeros_like(mu_d), 
                                         torch::ones_like(mu_d));
    
    // Assemble residual
    R.index_put_({torch::indexing::Slice(), U_COUPLED}, 
                 -flux_div_u - pgf_u);
    R.index_put_({torch::indexing::Slice(), V_COUPLED}, 
                 -flux_div_v - pgf_v);
    R.index_put_({torch::indexing::Slice(), W_VELOCITY}, 
                 buoyancy);
    R.index_put_({torch::indexing::Slice(), MU_DRY}, 
                 -div_V);
    
    // Apply boundary damping
    apply_w_damping(R.index({torch::indexing::Slice(), W_VELOCITY}), W);
    
    return R;
}

torch::Tensor FluxFormRHS::compute_flux_divergence(
    const torch::Tensor& U_coupled,
    const torch::Tensor& V_coupled,
    const torch::Tensor& Omega_coupled,
    const torch::Tensor& scalar) {
    
    // Flux divergence: ∇·(V*a) = ∂_x(U*a) + ∂_y(V*a) + ∂_η(Ω*a)
    
    // Compute fluxes
    auto flux_x = U_coupled * scalar;
    auto flux_y = V_coupled * scalar;
    auto flux_z = Omega_coupled * scalar;
    
    // Compute divergence using finite differences
    // Simplified for demonstration - actual implementation needs proper staggering
    auto dx = grid_info_->rdx;
    auto dy = grid_info_->rdy;
    auto dz = grid_info_->rdz;
    
    // ∂_x(U*a)
    auto div_x = (flux_x.roll(-1, 0) - flux_x.roll(1, 0)) * dx * 0.5;
    
    // ∂_y(V*a)  
    auto div_y = (flux_y.roll(-1, 1) - flux_y.roll(1, 1)) * dy * 0.5;
    
    // ∂_η(Ω*a)
    auto div_z = (flux_z.roll(-1, 2) - flux_z.roll(1, 2)) * dz * 0.5;
    
    return div_x + div_y + div_z;
}

torch::Tensor FluxFormRHS::compute_pressure_eos(
    const torch::Tensor& theta_m,
    const torch::Tensor& alpha_d,
    const torch::Tensor& pb) {
    
    // Equation of state: p = p0 * (R_d*θ_m/(p0*α_d))^γ - pb
    auto gamma = grid_info_->cp / grid_info_->cv;
    auto p0 = grid_info_->p0;
    auto rd = grid_info_->rd;
    
    // Compute perturbation pressure
    auto arg = rd * theta_m / (p0 * alpha_d);
    auto p_total = p0 * torch::pow(arg, gamma);
    
    // Return perturbation (stop-gradient on base)
    return p_total - pb.detach();
}

torch::Tensor FluxFormRHS::solve_hydrostatic(
    const torch::Tensor& alpha_d,
    const torch::Tensor& mu_d) {
    
    // Hydrostatic equation: ∂_η φ = -α_d * μ_d
    // This is a vertical integration
    
    int nz = alpha_d.size(2);
    auto phi = torch::zeros_like(alpha_d);
    
    // Bottom boundary condition (terrain)
    phi.index_put_({torch::indexing::Slice(), torch::indexing::Slice(), 0}, 
                   flux_state_->base_state().phi_base.index({torch::indexing::Slice(), 
                                                            torch::indexing::Slice(), 0}));
    
    // Integrate upward
    for (int k = 1; k < nz; k++) {
        auto dz = grid_info_->dnw.index({k});
        auto integrand = -alpha_d.index({torch::indexing::Slice(), 
                                        torch::indexing::Slice(), k-1}) * mu_d;
        phi.index_put_({torch::indexing::Slice(), torch::indexing::Slice(), k},
                      phi.index({torch::indexing::Slice(), 
                               torch::indexing::Slice(), k-1}) + integrand * dz);
    }
    
    return phi;
}

torch::Tensor FluxFormRHS::compute_pressure_gradient_perturbation(
    const torch::Tensor& phi_pert,
    const torch::Tensor& p_pert,
    const torch::Tensor& mu_d,
    const BaseState& base_state) {
    
    // Perturbation pressure gradient force
    // PGF = -μ_d * α * ∇p - (α/α_d) * ∂_η p * ∇φ
    // Split into base and perturbation parts
    
    auto alpha_d = torch::ones_like(p_pert) / grid_info_->rho_base;
    
    // Horizontal gradients (simplified)
    auto dpx = (p_pert.roll(-1, 0) - p_pert.roll(1, 0)) * grid_info_->rdx * 0.5;
    auto dpy = (p_pert.roll(-1, 1) - p_pert.roll(1, 1)) * grid_info_->rdy * 0.5;
    
    auto dphix = (phi_pert.roll(-1, 0) - phi_pert.roll(1, 0)) * grid_info_->rdx * 0.5;
    auto dphiy = (phi_pert.roll(-1, 1) - phi_pert.roll(1, 1)) * grid_info_->rdy * 0.5;
    
    // Vertical pressure gradient
    auto dpz = (p_pert.roll(-1, 2) - p_pert.roll(1, 2)) * grid_info_->rdz * 0.5;
    
    // Combine terms (simplified - actual needs proper coefficient assembly)
    auto pgf_x = -mu_d * alpha_d * dpx - dpz * dphix;
    auto pgf_y = -mu_d * alpha_d * dpy - dpz * dphiy;
    
    return torch::cat({pgf_x.unsqueeze(1), pgf_y.unsqueeze(1)}, /*dim=*/1);
}

void FluxFormRHS::apply_w_damping(torch::Tensor& rw_tend, const torch::Tensor& w) {
    // Apply Rayleigh damping to W at model top
    auto damp_coef = grid_info_->damp_top;
    
    if (damp_coef > 0.0) {
        int nz = w.size(2);
        int n_damp_levels = 5;  // Top 5 levels
        
        for (int k = nz - n_damp_levels; k < nz; k++) {
            auto factor = damp_coef * std::pow((k - (nz - n_damp_levels)) / 
                                              float(n_damp_levels), 2);
            rw_tend.index_put_({torch::indexing::Slice(), 
                              torch::indexing::Slice(), k},
                             rw_tend.index({torch::indexing::Slice(), 
                                          torch::indexing::Slice(), k}) 
                             - factor * w.index({torch::indexing::Slice(), 
                                               torch::indexing::Slice(), k}));
        }
    }
}

void FluxFormRHS::apply_divergence_damping(torch::Tensor& F, const torch::Tensor& div) {
    // Apply divergence damping for acoustic wave control
    auto kdamp = grid_info_->kdamp;
    
    if (kdamp > 0.0) {
        F -= kdamp * div;
    }
}

// ============================================================================
// WRFAcousticPreconditioner Implementation
// ============================================================================

void WRFAcousticPreconditioner::setup(double dt_gamma) {
    // Call base class setup
    AcousticPreconditioner::setup(dt_gamma);
    
    // Additional setup for hydrostatic coupling
    dt_gamma_ = dt_gamma;
    
    // Build hydrostatic operator matrix
    int nz = grid_info_->nz;
    hydrostatic_operator_ = torch::zeros({nz, nz}, torch::kFloat32);
    
    // Fill tridiagonal structure for ∂_η φ = -α_d * μ_d
    for (int k = 0; k < nz-1; k++) {
        auto dz = grid_info_->dnw.index({k});
        hydrostatic_operator_.index_put_({k+1, k}, -1.0 / dz);
        hydrostatic_operator_.index_put_({k, k}, 1.0 / dz);
    }
    
    // Schur complement for (U,V)↔(W,φ) coupling
    // Simplified - actual implementation needs proper assembly
    auto cs2 = grid_info_->cs * grid_info_->cs;
    schur_complement_ = torch::eye(nz, torch::kFloat32) * cs2 * dt_gamma * dt_gamma;
}

torch::Tensor WRFAcousticPreconditioner::apply(const torch::Tensor& r) {
    // Apply preconditioner M^{-1} * r
    // where M approximates the acoustic-gravity wave coupling
    
    int nx = grid_info_->nx;
    int ny = grid_info_->ny;
    int nz = grid_info_->nz;
    
    // Extract components
    auto r_w = r.index({torch::indexing::Slice(0, nx*ny*nz)}).reshape({nx, ny, nz});
    auto r_phi = r.index({torch::indexing::Slice(nx*ny*nz, 2*nx*ny*nz)}).reshape({nx, ny, nz});
    auto r_mu = r.index({torch::indexing::Slice(2*nx*ny*nz, 2*nx*ny*nz + nx*ny)}).reshape({nx, ny});
    
    // Solve vertical system with hydrostatic constraint
    auto [z_w, z_phi, z_mu] = solve_vertical_hydrostatic(r_w, r_phi, r_mu);
    
    // Apply horizontal smoothing (simplified)
    if (use_multigrid_) {
        // Use multigrid for horizontal acoustic operator
        z_w = z_w;  // Placeholder for multigrid V-cycle
        z_phi = z_phi;
    }
    
    // Reassemble solution
    auto z = torch::zeros_like(r);
    z.index_put_({torch::indexing::Slice(0, nx*ny*nz)}, z_w.flatten());
    z.index_put_({torch::indexing::Slice(nx*ny*nz, 2*nx*ny*nz)}, z_phi.flatten());
    z.index_put_({torch::indexing::Slice(2*nx*ny*nz, 2*nx*ny*nz + nx*ny)}, z_mu.flatten());
    
    return z;
}

torch::Tensor WRFAcousticPreconditioner::solve_vertical_hydrostatic(
    const torch::Tensor& r_w,
    const torch::Tensor& r_phi,
    const torch::Tensor& r_mu) {
    
    // Solve coupled system:
    // [A_ww  A_wphi  A_wmu  ] [z_w  ]   [r_w  ]
    // [A_phiw A_phiphi A_phimu] [z_phi] = [r_phi]
    // [A_muw  A_muphi  A_mumu ] [z_mu ]   [r_mu ]
    
    // With hydrostatic constraint: ∂_η φ = -α_d * μ_d
    
    int nx = r_w.size(0);
    int ny = r_w.size(1);
    int nz = r_w.size(2);
    
    auto z_w = torch::zeros_like(r_w);
    auto z_phi = torch::zeros_like(r_phi);
    auto z_mu = torch::zeros_like(r_mu);
    
    // Column-wise solve (parallelizable)
    for (int i = 0; i < nx; i++) {
        for (int j = 0; j < ny; j++) {
            // Extract column
            auto col_rw = r_w.index({i, j, torch::indexing::Slice()});
            auto col_rphi = r_phi.index({i, j, torch::indexing::Slice()});
            auto col_rmu = r_mu.index({i, j});
            
            // Solve tridiagonal system for this column
            // Simplified Thomas algorithm
            auto col_zw = col_rw.clone();
            auto col_zphi = col_rphi.clone();
            auto col_zmu = col_rmu.clone();
            
            // Apply hydrostatic constraint
            for (int k = 1; k < nz; k++) {
                auto dz = grid_info_->dnw.index({k});
                col_zphi[k] = col_zphi[k-1] - dz * col_zmu;
            }
            
            // Store solution
            z_w.index_put_({i, j, torch::indexing::Slice()}, col_zw);
            z_phi.index_put_({i, j, torch::indexing::Slice()}, col_zphi);
            z_mu.index_put_({i, j}, col_zmu);
        }
    }
    
    return {z_w, z_phi, z_mu};
}

// ============================================================================
// Fortran Interface Implementation
// ============================================================================

extern "C" {

void* wrf_flux_form_init(
    int its, int ite, int jts, int jte, int kts, int kte,
    int ims, int ime, int jms, int jme, int kms, int kme,
    const float* phi_base, const float* p_base,
    const float* alpha_d_base, const float* mu_base) {
    
    auto adapter = new FluxFormStateVector(
        its, ite, jts, jte, kts, kte,
        ims, ime, jms, jme, kms, kme,
        true  // use_perturbation
    );
    
    // Initialize base state
    int nx = ite - its + 1;
    int ny = jte - jts + 1;
    int nz = kte - kts + 1;
    
    // FIX 2025-12-31 Batch37 Issue 3: Explicit CPU device for host pointer from_blob
    auto cpu_opts = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU);
    adapter->base_state_.phi_base = torch::from_blob(
        const_cast<float*>(phi_base), {nx, ny, nz}, cpu_opts).clone();
    adapter->base_state_.p_base = torch::from_blob(
        const_cast<float*>(p_base), {nx, ny, nz}, cpu_opts).clone();
    adapter->base_state_.alpha_d_base = torch::from_blob(
        const_cast<float*>(alpha_d_base), {nx, ny, nz}, cpu_opts).clone();
    adapter->base_state_.mu_base = torch::from_blob(
        const_cast<float*>(mu_base), {nx, ny}, cpu_opts).clone();
    
    adapter->base_state_.detach_all();
    
    return adapter;
}

void wrf_pack_state(
    void* adapter,
    float* flux_state,
    const float* u, const float* v, const float* w,
    const float* theta_m, const float* phi, const float* mu_d,
    const float* qv, int nx, int ny, int nz) {
    
    auto flux_adapter = static_cast<FluxFormStateVector*>(adapter);
    
    // Convert to tensors
    // FIX 2025-12-31 Batch37 Issue 3: Explicit CPU device for host pointer from_blob
    auto cpu_opts = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU);
    auto u_tensor = torch::from_blob(const_cast<float*>(u),
                                     {nx+1, ny, nz}, cpu_opts);
    auto v_tensor = torch::from_blob(const_cast<float*>(v),
                                     {nx, ny+1, nz}, cpu_opts);
    auto w_tensor = torch::from_blob(const_cast<float*>(w),
                                     {nx, ny, nz+1}, cpu_opts);
    auto mu_tensor = torch::from_blob(const_cast<float*>(mu_d),
                                      {nx, ny}, cpu_opts);
    
    // Pack using mass-weighted coupling
    flux_adapter->set_velocity(u_tensor, v_tensor, w_tensor, mu_tensor);
    
    // Copy to output
    // FIX Round194: Ensure contiguous + Float32 before data_ptr<float>()
    auto packed = flux_adapter->data();
    TORCH_CHECK(packed.is_contiguous() && packed.scalar_type() == torch::kFloat32,
        "wrf_pack_state: packed tensor must be contiguous Float32");
    // FIX Round194: Explicit size_t cast for overflow safety
    std::memcpy(flux_state, packed.data_ptr<float>(),
                static_cast<size_t>(packed.numel()) * sizeof(float));
}

void wrf_unpack_state(
    void* adapter,
    const float* flux_state,
    float* u, float* v, float* w,
    float* theta_m, float* phi, float* mu_d,
    float* qv, int nx, int ny, int nz) {
    
    auto flux_adapter = static_cast<FluxFormStateVector*>(adapter);
    
    // Load packed state
    // FIX 2025-12-31 Batch37 Issue 3: Explicit CPU device for host pointer from_blob
    auto cpu_opts = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU);
    auto n_vars = flux_adapter->n_vars_;
    // FIX Round193: Use int64_t to prevent int overflow on large grids (>2^31 elements)
    int64_t n_points = static_cast<int64_t>(nx) * ny * nz;
    auto packed = torch::from_blob(const_cast<float*>(flux_state),
                                   {n_points, n_vars}, cpu_opts);
    flux_adapter->data() = packed;
    
    // Extract coupled variables
    auto U = flux_adapter->get_variable(static_cast<StateVariable>(U_COUPLED));
    auto V = flux_adapter->get_variable(static_cast<StateVariable>(V_COUPLED));
    auto W = flux_adapter->get_variable(static_cast<StateVariable>(W_VELOCITY));
    auto mu_tensor = flux_adapter->get_variable(static_cast<StateVariable>(MU_DRY));
    
    // Decouple momentum
    auto u_tensor = flux_adapter->decouple_momentum(U, mu_tensor);
    auto v_tensor = flux_adapter->decouple_momentum(V, mu_tensor);
    
    // Copy to output arrays
    // FIX Round193: Verify tensors are contiguous and Float32 before data_ptr<float>()
    TORCH_CHECK(u_tensor.is_contiguous() && u_tensor.scalar_type() == torch::kFloat32,
        "wrf_unpack_state: u_tensor must be contiguous Float32");
    TORCH_CHECK(v_tensor.is_contiguous() && v_tensor.scalar_type() == torch::kFloat32,
        "wrf_unpack_state: v_tensor must be contiguous Float32");
    TORCH_CHECK(W.is_contiguous() && W.scalar_type() == torch::kFloat32,
        "wrf_unpack_state: W must be contiguous Float32");
    TORCH_CHECK(mu_tensor.is_contiguous() && mu_tensor.scalar_type() == torch::kFloat32,
        "wrf_unpack_state: mu_tensor must be contiguous Float32");
    // FIX Round194: size_t cast to prevent overflow on large grids
    std::memcpy(u, u_tensor.data_ptr<float>(), static_cast<size_t>(u_tensor.numel()) * sizeof(float));
    std::memcpy(v, v_tensor.data_ptr<float>(), static_cast<size_t>(v_tensor.numel()) * sizeof(float));
    std::memcpy(w, W.data_ptr<float>(), static_cast<size_t>(W.numel()) * sizeof(float));
    std::memcpy(mu_d, mu_tensor.data_ptr<float>(), static_cast<size_t>(mu_tensor.numel()) * sizeof(float));
}

void wrf_compute_jvp(
    void* adapter,
    float* jvp,
    const float* state,
    const float* vector,
    int n_vars,
    int n_points,
    int use_ad) {
    
    auto flux_adapter = static_cast<FluxFormStateVector*>(adapter);
    
    // Convert to tensors
    // FIX 2025-12-31 Batch37 Issue 3: Explicit CPU device for host pointer from_blob
    auto cpu_opts = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU);
    auto state_tensor = torch::from_blob(const_cast<float*>(state),
                                         {n_points, n_vars}, cpu_opts);
    auto vector_tensor = torch::from_blob(const_cast<float*>(vector),
                                          {n_points, n_vars}, cpu_opts);
    
    torch::Tensor jvp_result;
    
    if (use_ad) {
        // Use automatic differentiation
        state_tensor.requires_grad_(true);
        
        // Create RHS module (simplified)
        auto grid_info = std::make_shared<WRFGridInfo>();
        auto physics_config = std::make_shared<PhysicsConfig>();
        auto rhs_module = std::make_shared<FluxFormRHS>(
            grid_info, physics_config, 
            std::shared_ptr<FluxFormStateVector>(flux_adapter)
        );
        
        // Compute JVP using autograd
        auto F = rhs_module->forward(state_tensor);
        jvp_result = torch::autograd::grad(
            {F}, {state_tensor}, {vector_tensor},
            /*retain_graph=*/false,
            /*create_graph=*/false
        )[0];
        
    } else {
        // Use finite differences
        double eps = std::sqrt(std::numeric_limits<float>::epsilon());
        
        // Forward difference: JVP ≈ (F(x + εv) - F(x)) / ε
        auto state_plus = state_tensor + eps * vector_tensor;
        
        // Evaluate RHS (simplified)
        auto F = state_tensor;  // Placeholder
        auto F_plus = state_plus;  // Placeholder
        
        jvp_result = (F_plus - F) / eps;
    }
    
    // Copy result
    // FIX Round194: Ensure contiguous + Float32 before data_ptr<float>()
    auto jvp_safe = jvp_result.is_contiguous() ? jvp_result
                    : jvp_result.contiguous();
    TORCH_CHECK(jvp_safe.scalar_type() == torch::kFloat32,
        "wrf_flux_jvp: jvp_result must be Float32");
    // FIX Round194: Explicit size_t cast for overflow safety
    std::memcpy(jvp, jvp_safe.data_ptr<float>(),
                static_cast<size_t>(jvp_safe.numel()) * sizeof(float));
}

} // extern "C"

} // namespace sdirk3
} // namespace wrf