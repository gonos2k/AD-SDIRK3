#pragma once

#include "wrf_sdirk3_reference_pressure_model.h"
#include "wrf_sdirk3_state_layout.h"

namespace wrf::sdirk3 {
// Whole-domain periodic-X/symmetric-Y packed WRF layout only. Qhat=E R is
// the native horizontal projection with the additional W/Phi bottom-zero rule.
// The complement projection is identity. Physical Phi0 is included by an
// explicit shift/restore in the core; W0 and other complement couplings remain
// an approximation and are not claimed to match the native operator.
class PressurePackedAdapter {
public:
    PressurePackedAdapter(int nx_physical, int ny_physical, int nz);
    ReferencePressureFields extract(const torch::Tensor& packed) const;
    torch::Tensor extend(const ReferencePressureFields& fields) const;
    torch::Tensor project(const torch::Tensor& packed) const;
    torch::Tensor apply(const ReferencePressureModel& model, const torch::Tensor& packed) const;
    torch::Tensor solve(const ReferencePressureModel& model, const torch::Tensor& packed) const;
    torch::Tensor solve_scaled(const ReferencePressureModel& model, const torch::Tensor& rhs_scaled,
                               const torch::Tensor& scale) const;
    int64_t size() const { return layout_.total_size; }
private:
    int64_t n_, m_, nz_;
    StateLayout layout_;
};
}
