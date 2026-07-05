// WRF-compatible hydrostatic pressure calculation for SDIRK3
// Implements exact WRF pressure integration as in calc_p_rho_phi

#include <torch/torch.h>
#include <vector>

namespace wrf {
namespace sdirk3 {

// Compute pressure using WRF's hydrostatic integration
// Following calc_p_rho_phi from module_big_step_utilities_em.F
torch::Tensor compute_pressure_hydrostatic(
    const torch::Tensor& theta_full,    // Full potential temperature [j,k,i] = [ny, nz, nx]
    const torch::Tensor& mu_full,       // Full column mass [j,i] = [ny, nx]
    const torch::Tensor& mu_base,       // Base state column mass [j,i] = [ny, nx]
    const torch::Tensor& p_base,        // Base state pressure [j,k,i] = [ny, nz, nx]
    const torch::Tensor& muts,          // Dry column mass at base state [j,i] = [ny, nx]
    const std::vector<float>& c1h,      // Vertical coordinate coefficient
    const std::vector<float>& c2h,      // Vertical coordinate coefficient
    const std::vector<float>& rdnw,     // 1/(dnw[k]) - reciprocal of eta thickness at w-levels
    const std::vector<float>& rdn,      // 1/(dn[k]) - reciprocal of eta thickness at mass levels
    float rd,                           // Gas constant for dry air
    float cv,                           // Specific heat at constant volume
    float cp,                           // Specific heat at constant pressure
    float p0,                           // Reference pressure (100000 Pa)
    float p1000mb)                      // 1000 mb in Pa
{
    auto options = theta_full.options();
    // Following [j,k,i] = [ny,nz,nx] layout per WRF-SDIRK3-design.md
    int ny = theta_full.size(0);  // j dimension
    int nz = theta_full.size(1);  // k dimension
    int nx = theta_full.size(2);  // i dimension
    
    // Constants
    [[maybe_unused]] float cvpm = cv / cp;
    [[maybe_unused]] float r_d = rd;
    
    // PARITY FIX 2025-12-11: Device/dtype alignment for GPU runs.
    // from_blob wraps host memory - cannot create CUDA tensor from host pointer.
    // Instead, create CPU tensor from vector data, then move to target device.
    // This ensures proper ownership and GPU compatibility.
    auto cpu_options = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU);
    auto c1h_tensor = torch::tensor(std::vector<float>(c1h.begin(), c1h.end()), cpu_options).to(options.device());
    auto c2h_tensor = torch::tensor(std::vector<float>(c2h.begin(), c2h.end()), cpu_options).to(options.device());
    auto rdnw_tensor = torch::tensor(std::vector<float>(rdnw.begin(), rdnw.end()), cpu_options).to(options.device());
    auto rdn_tensor = torch::tensor(std::vector<float>(rdn.begin(), rdn.end()), cpu_options).to(options.device());
    
    // Initialize pressure perturbation array with [j,k,i] layout
    torch::Tensor p_pert = torch::zeros({ny, nz, nx}, options);
    
    // Get mu perturbation
    auto mu_pert = mu_full - mu_base;
    
    // === WRF Hydrostatic Integration ===
    // Following the exact algorithm from calc_p_rho_phi (hydrostatic case)
    
    // Top layer (k = nz-1 in C++ indexing, ktf in Fortran)
    int k = nz - 1;
    {
        // Expand scalars to match spatial dimensions [j,i] for [j,k,i] layout
        auto c1k = c1h_tensor[k].unsqueeze(0).unsqueeze(0).expand({ny, nx});
        auto c2k = c2h_tensor[k].unsqueeze(0).unsqueeze(0).expand({ny, nx});
        auto rdnwk = rdnw_tensor[k].unsqueeze(0).unsqueeze(0).expand({ny, nx});
        
        // WRF formula for top layer (note the negative sign)
        // p(i,k,j) = - 0.5*((c1(k)*mu(i,j))+qf1*(c1(k)*muts(i,j)+c2(k)))/rdnw(k)/qf2
        // For dry dynamics, qf1=0, qf2=1
        // FIX 2025-12-26: Use copy_() for in-place modification (select=... rebinds temporary)
        p_pert.select(1, k).copy_(-0.5f * (c1k * mu_pert) / rdnwk);  // select dimension 1 for k in [j,k,i] layout
    }
    
    // Integrate down from top to bottom
    for (int k = nz - 2; k >= 0; --k) {
        // Expand scalars to match spatial dimensions [j,i] for [j,k,i] layout
        auto c1k = c1h_tensor[k].unsqueeze(0).unsqueeze(0).expand({ny, nx});
        auto c2k = c2h_tensor[k].unsqueeze(0).unsqueeze(0).expand({ny, nx});
        auto rdnk1 = rdn_tensor[k+1].unsqueeze(0).unsqueeze(0).expand({ny, nx});
        
        // WRF formula for remaining layers
        // p(i,k,j) = p(i,k+1,j) - ((c1(k)*mu(i,j)) + qf1*(c1(k)*muts(i,j)+c2(k)))/qf2/rdn(k+1)
        // For dry dynamics, qf1=0, qf2=1
        // FIX 2025-12-26: Use copy_() for in-place modification (select=... rebinds temporary)
        p_pert.select(1, k).copy_(p_pert.select(1, k+1) - (c1k * mu_pert) / rdnk1);  // select dimension 1 for k in [j,k,i] layout
    }
    
    // Return perturbation pressure
    return p_pert;
}

// PARITY FIX 2025-12-13: Tensor-based overload to avoid CPU round-trips in PGF.
// Callers can pass device-resident tensors directly, eliminating host↔device
// transfers that occurred when converting c1h_/c2h_ to vectors per timestep.
torch::Tensor compute_pressure_hydrostatic(
    const torch::Tensor& theta_full,    // Full potential temperature [j,k,i] = [ny, nz, nx]
    const torch::Tensor& mu_full,       // Full column mass [j,i] = [ny, nx]
    const torch::Tensor& mu_base,       // Base state column mass [j,i] = [ny, nx]
    const torch::Tensor& p_base,        // Base state pressure [j,k,i] = [ny, nz, nx]
    const torch::Tensor& muts,          // Dry column mass at base state [j,i] = [ny, nx]
    const torch::Tensor& c1h,           // Vertical coordinate coefficient [nz] (device tensor)
    const torch::Tensor& c2h,           // Vertical coordinate coefficient [nz] (device tensor)
    const torch::Tensor& rdnw,          // 1/(dnw[k]) [nz] (device tensor)
    const torch::Tensor& rdn,           // 1/(dn[k]) [nz] (device tensor)
    float rd,                           // Gas constant for dry air
    float cv,                           // Specific heat at constant volume
    float cp,                           // Specific heat at constant pressure
    float p0,                           // Reference pressure (100000 Pa)
    float p1000mb)                      // 1000 mb in Pa
{
    auto options = theta_full.options();
    int ny = theta_full.size(0);
    int nz = theta_full.size(1);
    int nx = theta_full.size(2);

    // Ensure coefficient tensors are on the same device as theta_full
    auto c1h_tensor = c1h.to(options.device(), options.dtype(), /*non_blocking=*/true);
    auto c2h_tensor = c2h.to(options.device(), options.dtype(), /*non_blocking=*/true);
    auto rdnw_tensor = rdnw.to(options.device(), options.dtype(), /*non_blocking=*/true);
    auto rdn_tensor = rdn.to(options.device(), options.dtype(), /*non_blocking=*/true);

    // Initialize pressure perturbation array with [j,k,i] layout
    torch::Tensor p_pert = torch::zeros({ny, nz, nx}, options);

    // Get mu perturbation
    auto mu_pert = mu_full - mu_base;

    // === WRF Hydrostatic Integration ===
    // Top layer (k = nz-1 in C++ indexing)
    int k = nz - 1;
    {
        auto c1k = c1h_tensor[k].unsqueeze(0).unsqueeze(0).expand({ny, nx});
        auto rdnwk = rdnw_tensor[k].unsqueeze(0).unsqueeze(0).expand({ny, nx});
        // FIX 2025-12-26: Use copy_() for in-place modification (select=... rebinds temporary)
        p_pert.select(1, k).copy_(-0.5f * (c1k * mu_pert) / rdnwk);
    }

    // Integrate down from top to bottom
    for (int kk = nz - 2; kk >= 0; --kk) {
        auto c1k = c1h_tensor[kk].unsqueeze(0).unsqueeze(0).expand({ny, nx});
        auto rdnk1 = rdn_tensor[kk+1].unsqueeze(0).unsqueeze(0).expand({ny, nx});
        // FIX 2025-12-26: Use copy_() for in-place modification (select=... rebinds temporary)
        p_pert.select(1, kk).copy_(p_pert.select(1, kk+1) - (c1k * mu_pert) / rdnk1);
    }

    return p_pert;
}

// Compute inverse density (specific volume) using WRF's approach
torch::Tensor compute_inverse_density_hydrostatic(
    const torch::Tensor& theta_full,    // Full potential temperature
    const torch::Tensor& p_full,        // Full pressure
    const torch::Tensor& p_base,        // Base state pressure
    const torch::Tensor& th_base,       // Base state potential temperature
    const torch::Tensor& alb,           // Base state inverse density
    float rd,                           // Gas constant
    float cv,                           // Specific heat at constant volume
    float cp,                           // Specific heat at constant pressure
    float p1000mb)                      // Reference pressure (1000 mb)
{
    // BUGFIX 2026-07-06: WRF's cvpm is NEGATIVE (-cv/cp), share/module_model_constants.F:26.
    // The previous `cv/cp` (positive exponent) made the inverse density ~5x too small
    // (measured vs dyn_em: alt 0.4976 vs 2.4525) — an inverse-EOS sign error shared by
    // computeUnifiedRHS. Formula matches WRF calc_p_rho_phi (module_big_step_utilities_em.F:1119).
    float cvpm = -cv / cp;

    // WRF formula: al = (r_d/p1000mb)*(t+t0)*(((p+pb)/p1000mb)**cvpm) - alb
    // Note: theta_full = t + t0 (full potential temperature)
    auto pi = torch::pow(p_full / p1000mb, cvpm);
    auto al_full = (rd / p1000mb) * theta_full * pi;
    auto al_pert = al_full - alb;
    
    return al_pert;
}

} // namespace sdirk3
} // namespace wrf