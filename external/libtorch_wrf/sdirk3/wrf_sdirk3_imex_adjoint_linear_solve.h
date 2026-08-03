#ifndef WRF_SDIRK3_IMEX_ADJOINT_LINEAR_SOLVE_H
#define WRF_SDIRK3_IMEX_ADJOINT_LINEAR_SOLVE_H

#include <torch/torch.h>

#include <functional>
#include <vector>
#include <cmath>
#include <limits>

#include "wrf_sdirk3_newton_solver.h"

namespace wrf {
namespace sdirk3 {

// Reverse-mode VJP: computes J(y)^T * vector.
inline torch::Tensor compute_vjp_reverse_mode(
    const std::function<torch::Tensor(const torch::Tensor&)>& function,
    const torch::Tensor& y,
    const torch::Tensor& vector) {
    auto y_req = y.detach().clone().requires_grad_(true);
    auto f_y = function(y_req);
    auto scalar = (f_y * vector).sum();

    auto gradients = torch::autograd::grad(
        {scalar},
        {y_req},
        {},
        /*retain_graph=*/false,
        /*create_graph=*/false,
        /*allow_unused=*/false);
    return gradients[0];
}

// 9F.D99 (review section 6): the reverse visit order, as a pure testable function.
//
// The D94 defect was that per-checkpoint work sat inside a one-shot latch, so it ran on the
// FIRST visited checkpoint only. It survived because the reachable regime has exactly ONE
// checkpoint (the model fail-closes on step 1 at dt=600), which makes the one-shot and
// per-checkpoint forms observationally identical in every run that can be performed.
//
// The review's point is that a standing contract does NOT need a live dt=600 run: the ORDER
// is a pure property of the checkpoint count. So the replay's loop now takes its order from
// here, and the contract exercises it at n = 3 -- a length the model itself cannot yet reach.
//
// Deliberately NOT a copy of the loop for a test to inspect: production consumes this, so a
// green contract is evidence about the code that runs. Testing a re-implemented loop would
// only prove the re-implementation.
inline std::vector<size_t> reverse_visit_order(size_t n_checkpoints) {
    std::vector<size_t> order;
    order.reserve(n_checkpoints);
    for (size_t k = n_checkpoints; k-- > 0;) order.push_back(k);
    return order;
}

// 9F.D97 (review section 9): the convergence decision as a PURE FUNCTION.
//
// WHAT IT DELIBERATELY DOES NOT TAKE: the preconditioned residual. D91 moved the authority to
// the physical residual by editing an `if`; a later edit could move it back. Here the
// preconditioned residual is not a parameter, so it CANNOT be given authority -- the
// signature enforces the policy that a comment previously asked for.
//
// Being pure is what makes the positive side testable at all. The negative control
// (Adjoint_Solve_Authority_Contract) proves the solve will not return a WRONG answer, but it
// has no positive case: solve_gmres assumes WRF-shaped state, so a toy never converges under
// any preconditioner. That gap was real -- an implementation that threw on EVERY solve would
// have passed. A pure decision function needs no harness, so "can it say Converged?" becomes
// a testable question.
enum class SolveVerdict {
    Converged,   // the physical residual meets the tolerance -- safe to return a gradient
    Continue,    // not converged, but nothing is broken; more iterations could still help
    Fatal        // NaN/Inf residual, or Krylov breakdown -- more iterations cannot help
};

// MIXED ABSOLUTE/RELATIVE TOLERANCE (review section 8): ||r|| <= atol + rtol*||b||.
//
// A pure relative test is unstable as ||b|| -> 0, where the exact answer is x = 0 and any
// tiny residual is correct; the old form divided by max(||b||, 1e-30) and produced a huge
// ratio for a residual that was fine. The degenerate case is handled explicitly rather than
// by a magic floor.
//
// atol DEFAULTS TO 0, which reproduces the current pure-relative behaviour exactly. That is
// the conservative choice for this campaign (opt-in, no behaviour change), not necessarily
// the right long-run one: a 4D-Var driver that feeds near-zero cotangents will want a real
// atol, and picking it is a physical decision about how small a gradient component is
// negligible -- not something this function should invent.
inline SolveVerdict assess_adjoint_solve(double residual_norm,
                                         double rhs_norm,
                                         bool krylov_breakdown,
                                         double rtol,
                                         double atol = 0.0) {
    // Only genuinely unusable inputs are Fatal before the residual is even read: a
    // non-finite residual is not a small residual, and no budget helps.
    if (!std::isfinite(residual_norm) || !std::isfinite(rhs_norm)) return SolveVerdict::Fatal;
    if (residual_norm < 0.0 || rhs_norm < 0.0) return SolveVerdict::Fatal;

    // 9F.D98 (review section 4): THE RESIDUAL IS READ BEFORE breakdown.
    //
    // D97 tested krylov_breakdown FIRST and returned Fatal, which rejects a HAPPY BREAKDOWN
    // -- and happy breakdown is the BEST outcome a Krylov method has. h_{j+1,j} = 0 means
    // the Krylov subspace is invariant and already contains the exact solution; GMRES stops
    // early because it is DONE, not because it failed.
    //
    // The solver cannot distinguish them for us: solve_gmres returns breakdown = true for
    // both "Early breakdown, already converged" and "Early breakdown, NOT converged"
    // (newton_solver.cpp:1875). The residual is what separates them, so the residual must be
    // consulted first.
    //
    // This was an AVAILABILITY defect, not a correctness one -- it could not return a wrong
    // gradient, but it could permanently refuse a right one. And my own contract row
    // ("breakdown -> Fatal even with a small residual") PINNED it as if it were the spec: the
    // test was derived from what I had written rather than from what GMRES means.
    const double bar = atol + rtol * rhs_norm;
    if (residual_norm <= bar) return SolveVerdict::Converged;

    // Past here the residual is too large, so breakdown is the real thing: the subspace is
    // invariant and does NOT contain an acceptable solution. Restarting explores the same
    // space again, so more budget cannot help.
    if (krylov_breakdown) return SolveVerdict::Fatal;

    // ||b|| == 0: the exact solution is x = 0. Only a residual within atol is convergence;
    // anything else means the operator moved a vector it should not have, which more
    // iterations will not fix.
    if (rhs_norm == 0.0) return SolveVerdict::Fatal;

    return SolveVerdict::Continue;
}

inline const char* to_string(SolveVerdict v) {
    switch (v) {
        case SolveVerdict::Converged: return "Converged";
        case SolveVerdict::Continue:  return "Continue";
        case SolveVerdict::Fatal:     return "Fatal";
    }
    return "Unknown";
}

// Solves (I - alpha * J^T) x = rhs with left preconditioning by P^{-T}.
template <typename PreconditionerT>
torch::Tensor solve_transpose_linear_system_gmres(
    const std::function<torch::Tensor(const torch::Tensor&)>& fast_operator,
    const torch::Tensor& linearization_point,
    const torch::Tensor& rhs,
    double alpha,
    PreconditionerT& preconditioner,
    int gmres_restart = 15,
    int gmres_max_iterations = 80,
    float gmres_tolerance = 1e-5f) {
    preconditioner.set_alpha(alpha);

    auto operator_transpose = [&](const torch::Tensor& v) -> torch::Tensor {
        auto jtv = compute_vjp_reverse_mode(fast_operator, linearization_point, v);
        return v - alpha * jtv;
    };

    auto left_operator = [&](const torch::Tensor& v) -> torch::Tensor {
        return preconditioner.apply_transpose(operator_transpose(v));
    };

    auto rhs_left = preconditioner.apply_transpose(rhs);
    auto x0 = torch::zeros_like(rhs);

    auto gmres = krylov_methods::solve_gmres(
        left_operator,
        rhs_left,
        x0,
        /*stage_id=*/0,
        /*ru_share_hint=*/0.0f,
        gmres_restart,
        gmres_tolerance,
        gmres_max_iterations,
        nullptr,
        nullptr,
        nullptr,
        false,
        false);

    // 9F.D91 (review P0-1): the PHYSICAL residual is the stopping authority.
    double rel_true = std::numeric_limits<double>::infinity();
    double r_phys_norm_cached = std::numeric_limits<double>::infinity();

    // 9F.D87 (review section 6): report the TRUE, UNPRECONDITIONED residual.
    //
    // solve_gmres is handed rhs_left = P^T b and left_operator = P^T A^T, so its rel_error
    // is ||P^T(b - A^T x)|| / ||P^T b|| -- the PRECONDITIONED norm. With the identity
    // preconditioner that coincides with the physical residual; with any real P it does
    // not. Comparing rel_error ACROSS preconditioners therefore compares different norms,
    // and the D84 dt-ladder did exactly that: its identity column was a true residual and
    // its M^-T column was not, so the two were never comparable.
    //
    // One extra operator application against the 400 in the solve. The verdict has to be
    // made on the same physical quantity for every preconditioner or it is not a comparison.
    {
        // operator_transpose forms a VJP, so it MUST NOT be inside a NoGradGuard. Only the
        // reductions below are guarded.
        //
        // I wrapped this whole block in a guard first, and it died with "element 0 of
        // tensors does not require grad" -- the FIFTH occurrence of that pattern in this
        // campaign (D59 power_iterate_sigma_max, D66 the adjoint driver, D80's misreading,
        // D85 the probe itself, now here), and the second within one session, minutes after
        // fixing it in the probe. The reflex to open a diagnostic with NoGradGuard is the
        // bug; the rule is "guard each .item(), and NOTHING that calls back into an operator
        // which may need a graph".
        auto r_phys = rhs - operator_transpose(gmres.x);
        torch::NoGradGuard no_grad;
        r_phys_norm_cached = r_phys.norm().template item<double>();
        rel_true = (r_phys.norm() / rhs.norm().clamp_min(1e-30)).template item<double>();
        std::cerr << "SDIRK3_TRANSPOSE_TRUE_RESIDUAL rel_true=" << rel_true
                  << " rel_preconditioned=" << gmres.rel_error
                  << " |x|=" << gmres.x.norm().template item<double>()
                  << std::endl << std::flush;
    }

    // ADJOINT FAIL-CLOSE (full-repo review P1-2): the forward solver fail-closes
    // on non-convergence, but this transpose solve previously returned gmres.x
    // UNCONDITIONALLY — a stalled/broken-down adjoint solve produced a
    // partially-converged lambda the caller could not distinguish from a real
    // gradient, silently corrupting any 4D-Var descent direction built on it.
    // A wrong gradient is strictly worse than no gradient: refuse instead. The
    // throw propagates through runAdjointReplay (catch/restore/rethrow) -> the
    // C wrapper (-1) -> Fortran (ierr stays 1), the already-verified error chain.
    // 9F.D91 (review P0-1): JUDGE ON THE PHYSICAL RESIDUAL.
    //
    // D87 computed rel_true and only PRINTED it; the gate still read gmres.rel_error, which
    // is ||P^T(b - A^T x)|| / ||P^T b||. A preconditioner that shrinks the residual along
    // some direction makes that ratio small while the physical residual stays O(1):
    //
    //     A = I,  b = (1,1)^T,  x = (1,0)^T   =>   rel_true = 1/sqrt(2) ~ 0.707
    //     P^T = diag(1, 1e-6)                 =>   rel_precond ~ 1e-6   "converged"
    //
    // That is a FALSE SUCCESS returning a 70%-wrong gradient, and the comment directly below
    // is the reason it matters: a wrong gradient is strictly worse than no gradient. The
    // preconditioned residual describes the Krylov iteration; only the physical one
    // describes the equation the caller asked to solve.
    //
    // gmres.success is kept as a NECESSARY condition -- it also covers breakdown and NaN
    // paths -- but it is no longer sufficient, and gmres.rel_error is now diagnostic only.
    // 9F.D97: the decision comes from the pure function above, so the policy lives in one
    // testable place instead of in this `if`.
    //
    // BREAKDOWN IS gmres.breakdown, NOT !gmres.success. The first wiring passed the latter,
    // and the live run promptly reported verdict=Fatal for a solve that had merely run out of
    // budget ("GMRES max iterations reached"). Budget exhaustion is precisely the Continue
    // case -- more iterations could still help -- and calling it Fatal is the conflation the
    // review warned about: "fatal breakdown과 단순 내부 criterion failure를 구분해야 한다".
    //
    // Behaviourally both verdicts throw today, so this changes no outcome. It changes what
    // the verdict MEANS, which is the whole point of having one: a restart-boundary callback
    // must be able to tell "keep going" from "stop".
    //
    // NanRetryExhausted needs no special case -- a NaN residual is caught by the finiteness
    // test inside assess_adjoint_solve.
    // 9F.D98 (review section 10): take both norms DIRECTLY. D97 reconstructed the residual
    // norm as rel_true * max(rhs_norm, 1e-30) -- algebraically the same in the normal range,
    // but it round-trips through a ratio that was itself clamped, which is an unnecessary
    // underflow/overflow path exactly where rhs -> 0 and the mixed tolerance matters most.
    // r_phys is already in hand.
    double res_norm_phys = 0.0, rhs_norm_phys = 0.0;
    {
        torch::NoGradGuard g;
        res_norm_phys = r_phys_norm_cached;
        rhs_norm_phys = rhs.norm().template item<double>();
    }
    const SolveVerdict verdict = assess_adjoint_solve(
        res_norm_phys, rhs_norm_phys, /*krylov_breakdown=*/gmres.breakdown,
        /*rtol=*/static_cast<double>(gmres_tolerance));
    if (verdict != SolveVerdict::Converged) {
        throw std::runtime_error(
            std::string("sdirk3 adjoint transpose solve did not converge (") +
            "verdict=" + to_string(verdict) +
            ", success=" + (gmres.success ? "true" : "false") +
            ", rel_true=" + std::to_string(rel_true) +
            ", rel_preconditioned=" + std::to_string(gmres.rel_error) +
            ", tol=" + std::to_string(gmres_tolerance) +
            ", iterations=" + std::to_string(gmres.iterations) +
            ", msg=" + gmres.message +
            ") — refusing to return a partially-converged adjoint");
    }
    return gmres.x;
}

}  // namespace sdirk3
}  // namespace wrf

#endif  // WRF_SDIRK3_IMEX_ADJOINT_LINEAR_SOLVE_H
