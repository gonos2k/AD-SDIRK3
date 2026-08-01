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


// ---------------------------------------------------------------------------------------
// THE SDIRK SPECIALISATION.
//
// The generic formulas above take R_K and R_U as given. For this solver they are not
// arbitrary -- the stage residual is (newton_solver.cpp:4649)
//
//     R(K, U) = K - F(U + dt*gamma*K)
//
// so, differentiating at the converged root with J = dF/dY evaluated at Y = U + dt*gamma*K,
//
//     R_K = I - dt*gamma*J        <- EXACTLY the operator FGMRES already inverts
//     R_U = -J
//
// and therefore
//
//     dK/dU   =  A^{-1} J          with A = I - dt*gamma*J
//     (dK/dU)^T =  J^T A^{-T}
//
// Two things are worth stating because they are where a wiring bug would hide.
//
// FIRST, R_U is -J, NOT -I. It is tempting to read "U enters R linearly" from the shape of
// the residual and write R_U = -I, which would give dK/dU = A^{-1} and is wrong by a whole
// factor of J. U enters through F, so its derivative carries J.
//
// SECOND, the ORDER differs between the tangent and the adjoint, and they are not
// transposes of each other written the same way round: the tangent applies J and THEN
// solves, the adjoint solves and THEN applies J^T. Getting this backwards produces
// J^T A^{-1} rather than J^T A^{-T}, which is self-consistent-looking and wrong unless A
// is symmetric -- and A = I - dt*gamma*J is not, since J is not.
//
// Both are asserted in Implicit_Diff_Contract against a closed-form J, so the wiring has a
// reference to fail against rather than a comment to agree with.
inline torch::Tensor sdirk_stage_tangent(const LinearSolve& solve_A,      // A^{-1}
                                         const ApplyOperator& apply_J,    // J
                                         const torch::Tensor& dU) {
    return solve_A(apply_J(dU));            // A^{-1} J dU
}

inline torch::Tensor sdirk_stage_adjoint(const LinearSolve& solve_A_transpose,  // A^{-T}
                                         const ApplyOperator& apply_J_transpose, // J^T
                                         const torch::Tensor& Kbar) {
    return apply_J_transpose(solve_A_transpose(Kbar));   // J^T A^{-T} Kbar
}


// ---------------------------------------------------------------------------------------
// TWO PULLBACKS, NOT TWO CANDIDATE ANSWERS (review section 4).
//
// D64 framed the choice as "legacy A^{-T}" versus "correct J^T A^{-T}", with the first
// missing a J^T. That framing was WRONG, and the algebra says so cleanly.
//
// The stage produces TWO outputs from U, and they have different derivatives:
//
//     stage STATE      Y = U + alpha*K(U)
//     stage TENDENCY   K = K(U)
//
// With dK/dU = A^{-1}J and A = I - alpha*J,
//
//     dY/dU = I + alpha*A^{-1}J
//           = A^{-1}(A + alpha*J)
//           = A^{-1}(I - alpha*J + alpha*J)
//           = A^{-1}                                    <- exactly, no approximation
//
// so the two pullbacks are
//
//     stage-STATE cotangent Ybar     ->  Ubar = A^{-T} Ybar
//     stage-TENDENCY cotangent Kbar  ->  Ubar = J^T A^{-T} Kbar
//
// BOTH ARE CORRECT. Which one applies is determined by which cotangent the caller holds,
// and applying the tendency map to a state cotangent silently mixes two different
// quantities -- the kind of error that produces a plausible gradient and a 4D-Var
// minimiser that optimises something other than the objective.
//
// This matters for runAdjointReplay: its argument is named lambda_terminal, its saved
// trajectory is packed STATE tensors, and there is no Kbar, stage index or Butcher
// coefficient anywhere in the signature. By that evidence it carries a STATE cotangent,
// for which A^{-T} alone is right -- so the "legacy" path was not missing a J^T; it was
// being handed a different cotangent than D64 assumed.
//
// The enum exists so the choice is made in the type system rather than by an int whose
// out-of-range values quietly fall through to one of the two.
enum class StageCotangent {
    State,      // Ybar: pullback is A^{-T}
    Tendency    // Kbar: pullback is J^T A^{-T}
};

// Ubar = A^{-T} Ybar   -- for a stage-STATE cotangent.
inline torch::Tensor stage_state_pullback(const LinearSolve& solve_A_transpose,
                                          const torch::Tensor& Ybar) {
    return solve_A_transpose(Ybar);
}

// Ubar = J^T A^{-T} Kbar   -- for a stage-TENDENCY cotangent.
inline torch::Tensor stage_tendency_pullback(const LinearSolve& solve_A_transpose,
                                             const ApplyOperator& apply_J_transpose,
                                             const torch::Tensor& Kbar) {
    return apply_J_transpose(solve_A_transpose(Kbar));
}

// Dispatch, so a caller that carries the kind alongside the vector cannot apply the wrong
// map by passing the wrong integer.
inline torch::Tensor stage_pullback(StageCotangent kind,
                                    const LinearSolve& solve_A_transpose,
                                    const ApplyOperator& apply_J_transpose,
                                    const torch::Tensor& cotangent) {
    switch (kind) {
        case StageCotangent::State:
            return stage_state_pullback(solve_A_transpose, cotangent);
        case StageCotangent::Tendency:
            return stage_tendency_pullback(solve_A_transpose, apply_J_transpose, cotangent);
    }
    throw std::invalid_argument(
        "SDIRK3 stage_pullback: unknown StageCotangent. Refusing to default to either map "
        "-- they are pullbacks of different quantities, and guessing produces a plausible "
        "gradient for the wrong objective.");
}

}  // namespace implicit_diff
}  // namespace sdirk3
}  // namespace wrf

#endif  // WRF_SDIRK3_IMPLICIT_DIFF_H
