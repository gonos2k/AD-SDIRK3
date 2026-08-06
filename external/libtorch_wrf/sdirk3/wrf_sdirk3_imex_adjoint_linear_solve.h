#ifndef WRF_SDIRK3_IMEX_ADJOINT_LINEAR_SOLVE_H
#define WRF_SDIRK3_IMEX_ADJOINT_LINEAR_SOLVE_H

#include <torch/torch.h>

#include <functional>
#include <vector>
#include "wrf_sdirk3_state_layout.h"

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
    // 9F.D103 (review section 6): VALIDATE THE TOLERANCES. This gate exists to refuse wrong
    // gradients, and an unvalidated rtol can switch it off entirely: rtol = Inf makes
    // bar = atol + Inf*||b|| = Inf, so EVERY finite residual returns Converged. A negative
    // rtol is equally meaningless. Neither is a tolerance, so neither gets to be one.
    if (!std::isfinite(rtol) || !std::isfinite(atol)) return SolveVerdict::Fatal;
    if (rtol < 0.0 || atol < 0.0) return SolveVerdict::Fatal;

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
    // An overflowed bar admits everything, which is the same fail-open shape as rtol = Inf.
    if (!std::isfinite(bar)) return SolveVerdict::Fatal;
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

// 9F.D100: the layout for a packed adjoint vector. The solver's grid dims are not visible
// from this header, so the caller registers the layout once and this returns it. Empty (and
// therefore invalid) when unset, which makes the block gate fall back to the global test
// rather than guess boundaries -- the same refusal-to-guess as D96.
inline StateLayout& adjoint_residual_layout_slot() {
    static StateLayout slot;
    return slot;
}
// FAIL-CLOSED at the setter, because the reader cannot tell the two failures apart.
//
// layout_for_adjoint_residual returns an invalid layout when the slot is unusable, and the
// caller then silently skips the per-block decomposition and gates on the global norm instead.
// That fallback is correct for "no layout installed yet" and WRONG for "a layout was installed
// and it is bad": a CoupledMomentum layout now fails is_valid(), so it would degrade the adjoint
// gate to a coarser check with no error anywhere -- the wrong basis silently buying weaker
// gating. Refusing here keeps the slot holding only valid layouts, so the fallback means what it
// says.
inline void set_adjoint_residual_layout(const StateLayout& layout) {
    TORCH_CHECK(layout.is_valid(),
                "set_adjoint_residual_layout: refusing an invalid layout. The adjoint's per-block "
                "gate degrades silently to a global norm when the slot is unusable, so an invalid "
                "layout here would weaken the gate rather than report anything. A CoupledMomentum "
                "basis fails is_valid() for this core.");
    adjoint_residual_layout_slot() = layout;
}
inline StateLayout layout_for_adjoint_residual(int64_t numel) {
    const StateLayout& slot = adjoint_residual_layout_slot();
    if (slot.is_valid() && slot.total_size == numel) return slot;
    return StateLayout{};   // invalid -> caller falls back to the global gate
}

// 9F.D100 (review section 7): the BLOCK-WISE gate, max_q rho_q <= 1.
//
// assess_adjoint_solve above judges one GLOBAL ratio ||r||/||b||. That is the right authority
// (physical, not preconditioned) measured in the wrong norm: the packed vector concatenates
// coupled momentum, geopotential, potential temperature and column mass, with block sizes
// from 3,321 (mu) to 217,728 (ru). A global L2 is dominated by the large blocks, so mu could
// be essentially 100% wrong and move the global ratio by well under a percent.
//
// This campaign has already been bitten by exactly that masking: sigma_max(J) looked
// state-invariant until it was split by RhsMode, because a dominant channel was hiding one
// five orders below it. A gradient is only usable if EVERY block is usable, so every block
// gets its own mixed tolerance and the worst one decides.
//
// STRICTER THAN THE GLOBAL TEST, deliberately, and in the safe direction: it can refuse a
// gradient the global test would accept, never accept one the global test would refuse. For
// a fail-close gate whose whole premise is that a wrong gradient is worse than no gradient,
// that is the correct way to be wrong.
struct BlockResidual {
    std::string name;
    double residual_norm = 0.0;
    double rhs_norm = 0.0;
};

inline SolveVerdict assess_adjoint_solve_blockwise(const std::vector<BlockResidual>& blocks,
                                                   bool krylov_breakdown,
                                                   double rtol,
                                                   double atol = 0.0) {
    if (blocks.empty()) return SolveVerdict::Fatal;   // nothing measured is not convergence

    SolveVerdict worst = SolveVerdict::Converged;
    for (const auto& b : blocks) {
        // 9F.D114 (review section 9): A ZERO-RHS *BLOCK* IS NOT FATAL.
        //
        // assess_adjoint_solve treats rhs_norm == 0 as Fatal, which is right for the WHOLE
        // system (b = 0 means x = 0, so a large residual cannot be fixed by iterating). It is
        // WRONG per block: in a coupled system a block can have b_q = 0 and still carry a
        // legitimate nonzero residual through coupling, and further Krylov iterations reduce
        // it. The review's example:
        //
        //     A = [[1,1],[1,2]], b = (1,0)^T   -- b_2 = 0, yet r_2 != 0 at intermediate
        //                                          iterates and converges normally.
        //
        // So a zero-RHS block is judged on the ABSOLUTE tolerance alone and can only reach
        // Continue, never Fatal. Genuine Fatals (non-finite, breakdown, bad tolerances) still
        // come from assess_adjoint_solve on the other blocks.
        if (b.rhs_norm == 0.0) {
            if (!std::isfinite(b.residual_norm) || b.residual_norm < 0.0)
                return SolveVerdict::Fatal;
            if (b.residual_norm > atol) worst = SolveVerdict::Continue;
            continue;
        }
        const SolveVerdict v =
            assess_adjoint_solve(b.residual_norm, b.rhs_norm, krylov_breakdown, rtol, atol);
        if (v == SolveVerdict::Fatal) return SolveVerdict::Fatal;   // Fatal short-circuits
        if (v == SolveVerdict::Continue) worst = SolveVerdict::Continue;
    }
    return worst;
}

// The worst block, for telemetry: which variable is holding the solve back. Returns an empty
// name when there are no blocks.
inline BlockResidual worst_block(const std::vector<BlockResidual>& blocks, double atol = 0.0) {
    BlockResidual worst;
    double worst_ratio = -1.0;
    for (const auto& b : blocks) {
        const double bar = atol + b.rhs_norm;      // rtol factored out: compares like for like
        const double ratio = bar > 0.0 ? b.residual_norm / bar
                                       : (b.residual_norm > 0.0 ? 1e300 : 0.0);
        if (ratio > worst_ratio) { worst_ratio = ratio; worst = b; }
    }
    return worst;
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

    // 9F.D91 (review P0-1): the PHYSICAL residual is the stopping authority.
    // Declared outside the outer loop: each pass overwrites them, and the values after the
    // loop are the ones the verdict was made on.
    double rel_true = std::numeric_limits<double>::infinity();
    double r_phys_norm_cached = std::numeric_limits<double>::infinity();
    double rhs_norm_cached = 0.0;
    std::vector<BlockResidual> block_residuals;

    // 9F.D106 (review section 7): CONTINUE NOW ACTUALLY CONTINUES.
    //
    // D97 introduced the Continue verdict and D98 made it mean something, but the wrapper
    // still threw on anything that was not Converged -- so behaviourally Continue == Fatal
    // and a solve that stopped on the PRECONDITIONED criterion never spent its remaining
    // budget against the PHYSICAL one. That is the review's policy, implemented:
    //
    //   physical passes                      -> return
    //   preconditioned passed, physical did not, budget remains -> continue
    //   Fatal (NaN, real breakdown, bad tolerances)             -> throw immediately
    //   budget exhausted, physical still failing                -> fail-close
    //
    // Each outer pass restarts solve_gmres FROM THE CURRENT ITERATE, so nothing already
    // achieved is discarded.
    WRFNewtonKrylovSolver::GMRESResult gmres;
    torch::Tensor x_current = x0;
    int budget_left = std::max(1, gmres_max_iterations);
    SolveVerdict verdict = SolveVerdict::Continue;
    int outer_passes = 0;

    while (budget_left > 0) {
        ++outer_passes;
        gmres = krylov_methods::solve_gmres(
            left_operator,
            rhs_left,
            x_current,
            /*stage_id=*/0,
            /*ru_share_hint=*/0.0f,
            gmres_restart,
            gmres_tolerance,
            budget_left,
            nullptr,
            nullptr,
            nullptr,
            false,
            false);
        x_current = gmres.x;
        // 9F.D114 (review section 10): SUBTRACT RESTART CYCLES, NOT ARNOLDI VECTORS.
        //
        // solve_gmres's max_iter bounds the OUTER restart loop
        // (`for (int iter = 0; iter < max_iter; ++iter)`), while gmres.iterations returns
        // total_arnoldi_iters -- the vector count. newton_solver.cpp:1041 makes the factor
        // explicit: total_arnoldi_iters < max_iter * restart.
        //
        // D106 passed budget_left as max_iter (restarts, correct) and then subtracted
        // gmres.iterations (vectors, WRONG). With restart=10 and budget 40, one call returns
        // 400 and budget_left becomes -360, so the loop could never run twice.
        //
        // I MEASURED outer_passes=1 and reported it as the correct outcome -- "the budget was
        // consumed in one call". That was a rationalisation of this unit bug: the budget was
        // 40 restarts and I subtracted 400 vectors. gmres.restarts is the matching unit.
        budget_left -= std::max(1, gmres.restarts);
        block_residuals.clear();   // each pass re-measures; never accumulate across passes


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
        // 9F.D103 (review section 6): ONE FP64 reduction, reused everywhere.
        //
        // .item<double>() converts the FINAL SCALAR; it does not make the accumulation FP64.
        // Over ~1e6 elements a float32 norm carries ~1e-4 relative error -- an order of
        // magnitude LARGER than the 1e-5 tolerance it is being compared against, and
        // reduction order differs across platforms and devices. Recomputing the norm at each
        // use also let the logged rel_true drift from the value the verdict used.
        auto r64 = r_phys.detach().to(torch::kFloat64);
        auto b64 = rhs.detach().to(torch::kFloat64);
        r_phys_norm_cached = r64.norm().template item<double>();
        rhs_norm_cached = b64.norm().template item<double>();

        // 9F.D100 (review section 7): per-block norms, taken here because r_phys is in scope.
        // Blocks come from THE shared StateLayout (D93), never a local copy of the offsets.
        const auto layout = layout_for_adjoint_residual(rhs.numel());
        if (layout.is_valid() && layout.total_size == rhs.numel()) {
            for (const auto& blk : layout.blocks) {
                BlockResidual br;
                br.name = blk.name;
                br.residual_norm =
                    r64.slice(0, blk.start, blk.start + blk.size).norm().template item<double>();
                br.rhs_norm =
                    b64.slice(0, blk.start, blk.start + blk.size).norm().template item<double>();
                block_residuals.push_back(br);
            }
        }
        rel_true = rhs_norm_cached > 0.0
                       ? r_phys_norm_cached / rhs_norm_cached
                       : std::numeric_limits<double>::infinity();
        std::cerr << "SDIRK3_TRANSPOSE_TRUE_RESIDUAL rel_true=" << rel_true
                  << " rel_preconditioned=" << gmres.rel_error
                  << " |x|=" << gmres.x.norm().template item<double>();
        if (!block_residuals.empty()) {
            const auto w = worst_block(block_residuals);
            std::cerr << " worst_block=" << w.name
                      << " (|r_q|=" << w.residual_norm << ", |b_q|=" << w.rhs_norm << ")";
            for (const auto& b : block_residuals) {
                std::cerr << " " << b.name << "="
                          << (b.rhs_norm > 0 ? b.residual_norm / b.rhs_norm : -1.0);
            }
        }
        std::cerr << std::endl << std::flush;
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
    // The SAME two numbers feed the log, the verdict, the exception message and the block
    // report. Recomputing any of them is how a refusal ends up citing a residual that is not
    // the one it judged.
    const double res_norm_phys = r_phys_norm_cached;
    const double rhs_norm_phys = rhs_norm_cached;
    // 9F.D100 (review section 7): judge per BLOCK, and let the worst one decide.
    //
    // The global ratio is kept as telemetry, but it cannot be the gate: mu is 3,321 of
    // 1,080,491 entries, so it can be ~100% wrong while the global ratio barely moves.
    // Blocks come from THE shared StateLayout (D93), not a local copy of the offsets.
    verdict =
        block_residuals.empty()
            ? assess_adjoint_solve(res_norm_phys, rhs_norm_phys,
                                   /*krylov_breakdown=*/gmres.breakdown,
                                   /*rtol=*/static_cast<double>(gmres_tolerance))
            : assess_adjoint_solve_blockwise(block_residuals,
                                             /*krylov_breakdown=*/gmres.breakdown,
                                             /*rtol=*/static_cast<double>(gmres_tolerance));

        if (verdict != SolveVerdict::Continue) break;   // Converged or Fatal: done deciding
    }   // outer budget loop
    if (verdict != SolveVerdict::Converged) {
        throw std::runtime_error(
            std::string("sdirk3 adjoint transpose solve did not converge (") +
            "verdict=" + to_string(verdict) +
            ", success=" + (gmres.success ? "true" : "false") +
            ", rel_true=" + std::to_string(rel_true) +
            ", rel_preconditioned=" + std::to_string(gmres.rel_error) +
            ", tol=" + std::to_string(gmres_tolerance) +
            ", outer_passes=" + std::to_string(outer_passes) +
            ", iterations=" + std::to_string(gmres.iterations) +
            ", msg=" + gmres.message +
            ") — refusing to return a partially-converged adjoint");
    }
    return gmres.x;
}

}  // namespace sdirk3
}  // namespace wrf

#endif  // WRF_SDIRK3_IMEX_ADJOINT_LINEAR_SOLVE_H
