// WRF-compatible hydrostatic pressure calculation for SDIRK3
// Implements exact WRF pressure integration as in calc_p_rho_phi

#include <torch/torch.h>
#include <vector>

// 9F.D49: include our own header so declaration/definition drift is a compile error.
#include "wrf_hydrostatic_pressure.h"

namespace wrf {
namespace sdirk3 {

// 9F.D50 (review section 3): THE ETA ORIENTATION, APPLIED HERE.
//
// This file transcribes WRF's calc_p_rho_phi line for line:
//     p(i,ktf,j) = -0.5*(c1(k)*mu(i,j))/rdnw(k)                       (:1117)
//     p(i,k,j)   =  p(i,k+1,j) - (c1(k)*mu(i,j))/rdn(k+1)             (:1187)
// WRF's rdnw and rdn are NEGATIVE (eta decreases with k), so both minus signs are
// really pluses: mu' > 0 raises the perturbation pressure, and integrating downward
// raises it further. That is the physics -- more column mass, more pressure.
//
// But this solver does NOT store WRF's sign. Every path into getRdnwTensor() /
// getRdnTensor() takes a magnitude: the constructor runs require_metric_magnitude()
// (tile_unified_impl.cpp:3233, :4209) and the grid path applies .abs() (:2576). The
// convention block at the top of that file states the obligation plainly -- "the
// eta-direction sign is carried by the operator stencils instead". This operator
// lives in a DIFFERENT FILE and never took it up, so it consumed |rdnw| with WRF's
// signed algebra and returned p' with the sign inverted.
//
// The signature is the same one Hydrostatic_Balance_Contract already pins for the
// balance residual: not a small degradation, an exact factor -1.
// Hydrostatic_Pressure_Orientation_Contract measures it on this function directly --
// the balance test could not, because it never called production code.
//
// ORIENTATION is the named constant below rather than a flipped literal so that the
// convention is greppable and so the WRF line it descends from stays readable.
namespace {
// Multiplies WRF's signed-metric algebra to convert it to the magnitude convention.
// WRF: rdnw < 0. Here: rdnw > 0. Dividing by |rdnw| instead of rdnw negates.
constexpr float kEtaOrientation = -1.0f;
}  // namespace

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
    
    // 9F.D49: two dead constants removed, one of them actively dangerous.
    //     [[maybe_unused]] float cvpm = cv / cp;   <- POSITIVE, i.e. the WRONG SIGN
    //     [[maybe_unused]] float r_d  = rd;
    // cvpm must be -cv/cp. A positive exponent is exactly the defect the 2026-07-06
    // bugfix removed ("alt 0.4976 vs 2.4525, ~5x too small"), left sitting unused in a
    // neighbouring function where the next person to need an exponent would find it
    // first. An unused-but-wrong constant is the same trap as an unused-but-wrong
    // helper; the campaign has now been bitten by that shape four times.
    
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
        // 9F.D50: kEtaOrientation converts WRF's signed-metric algebra to |rdnw|.
        p_pert.select(1, k).copy_(kEtaOrientation * -0.5f * (c1k * mu_pert) / rdnwk);  // select dimension 1 for k in [j,k,i] layout
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
        p_pert.select(1, k).copy_(p_pert.select(1, k+1) - kEtaOrientation * (c1k * mu_pert) / rdnk1);  // select dimension 1 for k in [j,k,i] layout
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
        // 9F.D50: kEtaOrientation converts WRF's signed-metric algebra to |rdnw|.
        p_pert.select(1, k).copy_(kEtaOrientation * -0.5f * (c1k * mu_pert) / rdnwk);
    }

    // Integrate down from top to bottom
    for (int kk = nz - 2; kk >= 0; --kk) {
        auto c1k = c1h_tensor[kk].unsqueeze(0).unsqueeze(0).expand({ny, nx});
        auto rdnk1 = rdn_tensor[kk+1].unsqueeze(0).unsqueeze(0).expand({ny, nx});
        // FIX 2025-12-26: Use copy_() for in-place modification (select=... rebinds temporary)
        p_pert.select(1, kk).copy_(p_pert.select(1, kk+1) - kEtaOrientation * (c1k * mu_pert) / rdnk1);
    }

    return p_pert;
}

// Compute inverse density (specific volume) using WRF's approach
// 9F.D47: the base-state EOS, as ONE function.
//
// MEASURED DEFECT. Six sites computed base/full inverse density inline as
//     alpha = rd * theta / p
// which omits the Exner factor. WRF's authority (dyn_em/start_em.F:636) is
//     alb = (r_d/p1000mb) * (t_init + t0) * (pb/p1000mb)**cvpm
// and since cvpm = -cv/cp = R_d/cp - 1, that expression IS R_d*theta*Pi/p. Verified
// against the em_b_wave wrfinput_d01: this formula reproduces WRF's stored ALB to
// 0.000e+00 relative error, while rd*theta/p is HIGH by a factor 1/Pi --
//     k=0  (984 hPa) 1.005      k=32 (342 hPa) 1.358
//     k=16 (584 hPa) 1.166      k=63 (111 hPa) 1.875
// i.e. +38% mean, +87% at model top.
//
// WHY A SHARED FUNCTION RATHER THAN SIX CORRECTIONS. A 2026-07-06 bugfix already found
// this error class (see the cvpm note below: "alt 0.4976 vs 2.4525"), corrected
// compute_inverse_density_hydrostatic, and left the six inline copies. Correcting copies
// is what produced the defect; there is now one place to be wrong.
torch::Tensor compute_inverse_density(
    const torch::Tensor& theta,     // potential temperature (ABSOLUTE, i.e. t_init + t0)
    const torch::Tensor& pressure,  // pressure at the same points
    float rd, float cv, float cp, float p1000mb)
{
    // 9F.D49 (review section 7): the SCALARS are formed in double, deliberately.
    //
    // They arrive as float, so `-cv/cp` in float rounds the exponent at ~1e-7 relative.
    // That exponent then multiplies log(p/p0) ~ -2.3, and Base_EOS_Contract MEASURED the
    // consequence on a 950->100 hPa column: a float64 tensor came back at rel = 2.8e-08
    // from the closed form -- float accuracy out of a double-precision path. Forming the
    // scalars in double takes it to ~1e-16.
    //
    // This does NOT move the float32 model path, which is why byte-identity still holds.
    // torch treats a C++ double as a WRAPPED number: it never promotes the tensor's
    // dtype, it is cast down to it. And the correctly-rounded double quotient cast to
    // float is the same float as dividing in float, both being correctly rounded
    // (IEEE-754 5.4). Asserted by the fingerprint, not by this argument alone.
    const double cvpm  = -static_cast<double>(cv) / static_cast<double>(cp);  // = R_d/cp - 1;
                                       // WRF share/module_model_constants.F:26
    const double coeff = static_cast<double>(rd) / static_cast<double>(p1000mb);
    return coeff * theta * torch::pow(pressure / static_cast<double>(p1000mb), cvpm);
}

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
    // 9F.D49 (review section 2): CALLS the shared EOS instead of restating it.
    //
    // D47 added compute_inverse_density() to end exactly this duplication and then left
    // this function -- three lines below it, in the same file -- reimplementing the same
    // formula. The D47 commit message claimed "there is one place left to be wrong";
    // that was false, there were two. Fourth instance of this pattern in the campaign,
    // and the first where the duplicate and its replacement were adjacent.
    //
    // The 2026-07-06 note this replaces is kept because it records WHY the exponent is
    // negative: cvpm = -cv/cp (share/module_model_constants.F:26). A previous `cv/cp`
    // made the inverse density ~5x too small (measured vs dyn_em: alt 0.4976 vs 2.4525).
    // That reasoning now lives with the single implementation.
    //
    // theta_full is the FULL potential temperature (t + t0), matching WRF's
    // calc_p_rho_phi (module_big_step_utilities_em.F:1119):
    //     al = (r_d/p1000mb)*(t+t0)*(((p+pb)/p1000mb)**cvpm) - alb
    return compute_inverse_density(theta_full, p_full, rd, cv, cp, p1000mb) - alb;
}

} // namespace sdirk3
} // namespace wrf