#ifndef WRF_SDIRK3_IMEX_SPLIT_OPS_H
#define WRF_SDIRK3_IMEX_SPLIT_OPS_H

#include <torch/torch.h>

#include "wrf_sdirk3_acoustic_coeff_builder.h"

namespace wrf {
namespace sdirk3 {

// Stage reference context frozen at t* = rhs_i for the current stage.
struct IMEXStageContext {
    torch::Tensor y_ref;

    // Optional diagnostics used by fast operator / preconditioner builders.
    torch::Tensor mu_m_ref;        // [nz_m, ny, nx]
    torch::Tensor alpha_d_m_ref;   // [nz_m, ny, nx]
    torch::Tensor cs2_m_ref;       // [nz_m, ny, nx]
    torch::Tensor theta_m_ref;     // [nz_m, ny, nx]
    torch::Tensor my2d;            // [ny, nx]
};

// Split-operator interface for IMEX time integration.
class WrfIMEXSplitOps {
public:
    virtual ~WrfIMEXSplitOps() = default;

    // Slow tendency: advection/physics-like terms.
    virtual torch::Tensor compute_slow_tendency(const torch::Tensor& state) = 0;

    // Build stage context from rhs_i.
    virtual IMEXStageContext build_stage_context(const torch::Tensor& rhs_i) = 0;

    // Build linearized acoustic coefficients at stage reference.
    virtual AcousticLinearCoeffs build_acoustic_coeffs(const IMEXStageContext& context) = 0;

    // Fast tendency: acoustic/stiff operator.
    virtual torch::Tensor compute_fast_tendency(const torch::Tensor& state,
                                                const IMEXStageContext& context,
                                                const AcousticLinearCoeffs& coeffs) = 0;

    // Optional compatibility filters (default: no-op).
    virtual void apply_filters_inplace(torch::Tensor& state) { (void)state; }
};

}  // namespace sdirk3
}  // namespace wrf

#endif  // WRF_SDIRK3_IMEX_SPLIT_OPS_H
