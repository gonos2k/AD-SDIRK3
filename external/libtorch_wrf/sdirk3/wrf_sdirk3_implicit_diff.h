// wrf_sdirk3_implicit_diff.h -- tangent and adjoint of an IMPLICIT stage, by the implicit
// function theorem rather than by differentiating the solver's iteration trace.
//
// 9F.D63 (review P0-C).
//
// THE PROBLEM. An SDIRK/ARK stage does not compute K by a formula; it SOLVES
//
//     R(K, U) = 0
//
// for K, with an adaptive Newton/FGMRES loop that has a variable iteration count, a line
// search, a trust-region fallback, early stopping, and a preconditioner whose vectors are
// detached. Running autograd through that trace differentiates THE ALGORITHM'S PATH. The
// result is not the derivative of the converged discrete equations: it changes when the
// iteration count changes, it is discontinuous where a branch flips, and it is wrong by
// exactly the amount the solve is unconverged.
//
// For a 4D-Var gradient that is not a tolerable approximation, because the minimiser will
// happily exploit the difference between the gradient it is given and the gradient of the
// objective it is actually minimising.
//
// THE FIX. Differentiate the EQUATION, not the algorithm. From R(K(U), U) = 0,
//
//     R_K dK + R_U dU = 0        =>        dK/dU = -R_K^{-1} R_U
//
//     tangent:  dK   = -R_K^{-1} (R_U dU)
//     adjoint:  Ubar = -R_U^T (R_K^{-T} Kbar),   i.e. solve R_K^T lambda = Kbar first
//
// Both need only matrix-free applications of R_K, R_K^T, R_U, R_U^T -- exactly the JVP and
// VJP machinery the solver already has, and the same operator FGMRES already inverts.
//
// The forward solve stays as it is. Only the derivative changes, and it becomes independent
// of how the forward solve reached its answer. Implicit_Diff_Contract asserts precisely
// that: the tangent from a deliberately sloppy inner solve and from a tight one agree,
// while a trace-differentiated tangent does not.
//
// Everything is std::function so the contract can be run against a synthetic system whose
// exact Jacobian is known -- the discipline that has now caught four instrument defects in
// this campaign, including one in this file's sibling probe.

#ifndef WRF_SDIRK3_IMPLICIT_DIFF_H
#define WRF_SDIRK3_IMPLICIT_DIFF_H

#include <torch/torch.h>

#include <functional>

namespace wrf {
namespace sdirk3 {
namespace implicit_diff {

// A linear solve against R_K (or its transpose), supplied by the caller. In production
// this is the same FGMRES that the Newton step uses; in the contract it is exact.
using LinearSolve = std::function<torch::Tensor(const torch::Tensor& rhs)>;
// A matrix-free application of R_U (or its transpose).
using ApplyOperator = std::function<torch::Tensor(const torch::Tensor& v)>;

// dK = -R_K^{-1} (R_U dU)
inline torch::Tensor stage_tangent(const LinearSolve& solve_RK,
                                   const ApplyOperator& apply_RU,
                                   const torch::Tensor& dU) {
    return -solve_RK(apply_RU(dU));
}

// Ubar = -R_U^T (R_K^{-T} Kbar)
//
// The ORDER is the whole content of the adjoint: solve against R_K^T FIRST, then apply
// R_U^T. Doing it the other way round computes -R_K^{-T}(R_U^T Kbar), which is a different
// operator and agrees with the correct one only when R_K and R_U commute. The
// dot-product identity in the contract is what catches that, and nothing else would.
inline torch::Tensor stage_adjoint(const LinearSolve& solve_RK_transpose,
                                   const ApplyOperator& apply_RU_transpose,
                                   const torch::Tensor& Kbar) {
    return -apply_RU_transpose(solve_RK_transpose(Kbar));
}

}  // namespace implicit_diff
}  // namespace sdirk3
}  // namespace wrf

#endif  // WRF_SDIRK3_IMPLICIT_DIFF_H
