#pragma once

#include "wrf_sdirk3_pressure_packed_adapter.h"
#include <functional>
#include <string>

namespace wrf::sdirk3 {
// Reduced V Schur solve for the explicitly defined P1 approximation. The
// callback is the fixed native primary V tendency, evaluated on physical core
// Phi nodes [m,nz+1,n], theta [m,nz,n], and mu [m,n]. The callback must be
// linear, read-only and side-effect-free. Model and callback must outlive this object. No packed operator or P0 inverse is used in the iteration.
class DirectPressureV {
public:
    using PrimaryCore = std::function<torch::Tensor(const torch::Tensor&,const torch::Tensor&,const torch::Tensor&)>;
    enum class Failure { None, Budget, Invalid };
    struct Result {
        torch::Tensor x; // undefined on failure
        bool converged=false;
        Failure failure=Failure::Invalid;
        int arnoldi=0, pressure_subsolves=0, primary_core_calls=0;
        // Weighted actual V-row residual / weighted affine V-Schur RHS norm.
        double true_rel=-1;
        int64_t reduced_size=0;
        int full_packed_extensions=0;
        std::string reason;
    };
    DirectPressureV(const ReferencePressureModel& model, const torch::Tensor& S_diag, PrimaryCore primary);
    Result solve(const torch::Tensor& packed_rhs) const;
    // Algebra fixtures: independent V metric, homogeneous Schur action, and
    // exact X/Z recovery at a supplied physical V (not a full native operator).
    torch::Tensor weight_v(const torch::Tensor& v) const;
    torch::Tensor schur_action(const torch::Tensor& v) const;
    ReferencePressureFields recover_for_v(const torch::Tensor& packed_rhs,const torch::Tensor& v) const;
private:
    struct Rhs { ReferencePressureFields raw, shifted; torch::Tensor phi0; };
    Rhs unpack(const torch::Tensor& packed) const;
    torch::Tensor pressure_base(const ReferencePressureFields& b, int* count) const;
    torch::Tensor pressure_homogeneous(const torch::Tensor& v, int* count) const;
    torch::Tensor primary(const torch::Tensor& p,const torch::Tensor& v,
                          const ReferencePressureFields& b,const torch::Tensor& phi0,int* count) const;
    void check_v(const torch::Tensor& v) const;
    const ReferencePressureModel& model_;
    PressurePackedAdapter adapter_;
    StateLayout layout_;
    PrimaryCore primary_;
    torch::Tensor weight_, zero_u_,zero_v_,zero_w_,zero_phi_,zero_t_,zero_mu_;
    ReferencePressureFields zero_;
};
}
