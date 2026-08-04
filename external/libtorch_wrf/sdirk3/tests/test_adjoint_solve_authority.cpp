// 9F.D91 (review P0-1): the transpose solve must judge on the PHYSICAL residual.
//
// D87 computed rel_true and only PRINTED it; the gate still read gmres.rel_error, which is
// ||P^T(b - A^T x)|| / ||P^T b||. A preconditioner that shrinks the residual along some
// direction makes that ratio small while the physical residual stays O(1):
//
//     A = I,  b = (1,1)^T,  x = (1,0)^T   =>   rel_true      ~ 0.707
//     P^T = diag(1, 1e-6)                 =>   rel_precond   ~ 1e-6   "converged"
//
// That is a FALSE SUCCESS handing back a 70%-wrong gradient, and a wrong gradient is
// strictly worse than no gradient -- the 4D-Var minimiser will happily descend along it.
//
// This is the NEGATIVE CONTROL: a deliberately residual-shrinking preconditioner that would
// have passed the old gate. If the authority ever moves back to the preconditioned residual,
// this test fails.

#include "../wrf_sdirk3_imex_adjoint_linear_solve.h"

#include <torch/torch.h>

#include <cmath>
#include <iomanip>
#include <limits>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int failures = 0;
int check_count = 0;

std::string sci(double v) {
    std::ostringstream o; o << std::scientific << std::setprecision(3) << v; return o.str();
}

void check(bool ok, const std::string& what) {
    ++check_count;
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;
    if (!ok) ++failures;
}

// N = 8, not 2: at N = 2 the Arnoldi exhausts the Krylov space on the first cycle and the
// breakdown path returns x0 = 0, which makes the identity control look like a failure of the
// solve rather than of the fixture. The false-success mechanism does not depend on N.
constexpr int64_t N = 8;

torch::TensorOptions opts() { return torch::TensorOptions().dtype(torch::kFloat32); }

// A^T = I - alpha*J^T with J = I, i.e. a plain scaling. Differentiable, so the VJP inside
// solve_transpose_linear_system_gmres has something to work with.
torch::Tensor fast_operator(const torch::Tensor& y) { return y.clone(); }

// P^T that crushes the second component. Legal as a preconditioner and catastrophic as a
// convergence metric.
struct ShrinkingPreconditioner {
    double shrink;
    void set_alpha(double) {}
    torch::Tensor apply_transpose(const torch::Tensor& r) const {
        auto s = torch::ones_like(r);
        s.index_put_({1}, static_cast<float>(shrink));
        return r * s;
    }
};

struct IdentityPreconditioner {
    void set_alpha(double) {}
    torch::Tensor apply_transpose(const torch::Tensor& r) const { return r.clone(); }
};

// Parses both residuals out of the refusal message. The refusal is the observable, so the
// numbers that justify it have to travel with it.
struct Refusal { bool threw = false; double rel_true = -1, rel_precond = -1; };

inline double field_after(const std::string& m, const std::string& key) {
    auto i = m.find(key);
    if (i == std::string::npos) return -1;
    return std::stod(m.substr(i + key.size()));
}

template <typename P>
Refusal solve_and_capture(P& precond, int iters) {
    auto lin = torch::zeros({N}, opts());
    auto rhs = torch::ones({N}, opts());
    Refusal r;
    try {
        wrf::sdirk3::solve_transpose_linear_system_gmres(
            fast_operator, lin, rhs, /*alpha=*/0.5, precond,
            /*gmres_restart=*/iters, /*gmres_max_iterations=*/iters,
            /*gmres_tolerance=*/1e-5f);
        return r;
    } catch (const std::exception& e) {
        const std::string m = e.what();
        r.threw = true;
        r.rel_true = field_after(m, "rel_true=");
        r.rel_precond = field_after(m, "rel_preconditioned=");
        return r;
    }
}

}  // namespace

int main() {
    std::cout << "=== Adjoint_Solve_Authority_Contract ===" << std::endl;

    // THE NEGATIVE CONTROL. A residual-shrinking P^T makes the PRECONDITIONED residual pass
    // the tolerance while the PHYSICAL residual is O(1). The proof that the hole was real is
    // self-contained: rel_precond would have satisfied the OLD gate, and rel_true does not.
    //
    // No positive control here. solve_gmres assumes WRF-shaped state (halo regions, block
    // layout), so an 8-element toy never converges under ANY preconditioner -- an
    // "identity succeeds" case would be testing the harness, not the authority. The
    // discrimination below needs no such case.
    {
        ShrinkingPreconditioner shrink{1e-6};
        const auto r = solve_and_capture(shrink, 1);
        check(r.threw, "shrinking P^T: the solve REFUSES");
        check(r.rel_precond >= 0 && r.rel_precond <= 1e-5,
              "the OLD gate would have PASSED it (rel_precond=" + sci(r.rel_precond) + ")");
        check(r.rel_true > 1e-5,
              "the NEW gate refuses it (rel_true=" + sci(r.rel_true) + ")");
        check(r.rel_true > 1e4 * r.rel_precond,
              "the two metrics disagree by >1e4 -- a false success, not a near miss");
    }

    // A milder shrink must not be enough to fake convergence either.
    {
        ShrinkingPreconditioner mild{1e-3};
        const auto r = solve_and_capture(mild, 1);
        check(r.threw && r.rel_true > 1e-5 && r.rel_precond <= 1e-3,
              "milder shrink (1e-3): still refused, still a disagreement");
    }

    // ================= 9F.D97 (review section 9): THE PURE DECISION FUNCTION =================
    //
    // The negative control above proves the solve will not return a WRONG answer. It has no
    // positive case, because solve_gmres assumes WRF-shaped state and a toy never converges
    // under any preconditioner -- so an implementation that threw on EVERY solve would have
    // passed it. That gap is real, and a pure function closes it: "can it say Converged?" is
    // testable with no harness at all.
    {
        using wrf::sdirk3::SolveVerdict;
        using wrf::sdirk3::assess_adjoint_solve;
        const double rtol = 1e-5;

        // --- the review's matrix, row by row.
        check(assess_adjoint_solve(1e-9, 1.0, false, rtol) == SolveVerdict::Converged,
              "matrix: true residual small -> Converged (the POSITIVE case, previously untestable)");
        check(assess_adjoint_solve(0.707, 1.0, false, rtol) == SolveVerdict::Continue,
              "matrix: true residual large -> NOT converged, regardless of any preconditioned value");
        check(assess_adjoint_solve(0.0, 1.0, false, rtol) == SolveVerdict::Converged,
              "matrix: exactly zero residual -> Converged");
        check(assess_adjoint_solve(std::nan(""), 1.0, false, rtol) == SolveVerdict::Fatal,
              "matrix: NaN residual -> Fatal (not 'small')");
        check(assess_adjoint_solve(std::numeric_limits<double>::infinity(), 1.0, false, rtol)
                  == SolveVerdict::Fatal,
              "matrix: Inf residual -> Fatal");
        check(assess_adjoint_solve(1e-9, std::nan(""), false, rtol) == SolveVerdict::Fatal,
              "matrix: NaN rhs -> Fatal");
        // 9F.D98 (review section 4): THESE TWO ROWS USED TO PIN THE DEFECT.
        // D97 asserted "breakdown -> Fatal even with a small residual", which is exactly
        // backwards: a HAPPY breakdown (h_{j+1,j} = 0 with the residual already under
        // tolerance) is the BEST outcome GMRES has -- the Krylov subspace is invariant and
        // contains the solution. solve_gmres reports breakdown = true for both the converged
        // and non-converged early exits, so only the residual separates them.
        check(assess_adjoint_solve(1e-9, 1.0, true, rtol) == SolveVerdict::Converged,
              "HAPPY breakdown (small residual) -> Converged, not Fatal");
        check(assess_adjoint_solve(0.5, 1.0, true, rtol) == SolveVerdict::Fatal,
              "breakdown with a LARGE residual -> Fatal (invariant subspace, no solution)");
        check(assess_adjoint_solve(0.5, 1.0, false, rtol) == SolveVerdict::Continue,
              "same large residual WITHOUT breakdown -> Continue (budget could still help)");

        // --- near-zero rhs: the case a pure relative test cannot express.
        check(assess_adjoint_solve(1e-12, 0.0, false, rtol, /*atol=*/1e-10)
                  == SolveVerdict::Converged,
              "near-zero rhs + small ABSOLUTE residual -> Converged (needs atol)");
        check(assess_adjoint_solve(1e-3, 0.0, false, rtol, /*atol=*/1e-10)
                  == SolveVerdict::Fatal,
              "near-zero rhs + large residual -> Fatal, not Continue (iterating cannot help)");
        check(assess_adjoint_solve(0.0, 0.0, false, rtol) == SolveVerdict::Converged,
              "zero rhs and zero residual -> Converged even at atol=0");

        // --- mixed tolerance behaves as atol + rtol*||b||.
        check(assess_adjoint_solve(2e-5, 1.0, false, rtol, /*atol=*/1e-4)
                  == SolveVerdict::Converged,
              "mixed tolerance: atol admits what rtol alone would reject");
        check(assess_adjoint_solve(2e-5, 1.0, false, rtol, /*atol=*/0.0)
                  == SolveVerdict::Continue,
              "mixed tolerance: atol=0 reproduces the pure-relative bar (no silent loosening)");
        check(assess_adjoint_solve(1e-5, 1.0, false, rtol) == SolveVerdict::Converged,
              "boundary: residual exactly at rtol*||b|| is accepted");

        // --- the signature itself is the guarantee.
        check(std::string("Converged") == to_string(SolveVerdict::Converged) &&
              std::string("Fatal") == to_string(SolveVerdict::Fatal),
              "verdicts stringify, so a refusal can name which one fired");
    }

    // ============ 9F.D99 (review section 6): PER-CHECKPOINT ORDER, WITHOUT A LIVE RUN ======
    //
    // The D94 defect -- per-checkpoint work sitting inside a one-shot latch -- survived
    // because only ONE checkpoint is reachable (the model fail-closes on step 1 at dt=600),
    // which makes the one-shot and per-checkpoint forms observationally identical in every
    // run that can be performed. The review's point is that the ORDER is a pure property of
    // the checkpoint COUNT, so a standing contract needs no live run at all.
    //
    // This exercises n = 3, a length the model itself cannot currently reach. And it is the
    // SAME function production consumes -- runAdjointReplay takes its visit order from here
    // -- so a green result is evidence about the code that runs, not about a re-implemented
    // loop.
    {
        using wrf::sdirk3::reverse_visit_order;

        const auto three = reverse_visit_order(3);
        check(three.size() == 3, "three checkpoints -> three visits (one per checkpoint)");
        check(three.size() == 3 && three[0] == 2 && three[1] == 1 && three[2] == 0,
              "visited in REVERSE: 2, 1, 0 -- an adjoint runs backwards in time");

        // Every index exactly once: no checkpoint skipped, none visited twice.
        {
            const auto ten = reverse_visit_order(10);
            std::vector<int> seen(10, 0);
            for (size_t k : ten) { if (k < 10) ++seen[k]; }
            bool once = ten.size() == 10;
            for (int c : seen) if (c != 1) once = false;
            check(once, "ten checkpoints: every index visited EXACTLY once");
        }

        check(reverse_visit_order(1).size() == 1 && reverse_visit_order(1)[0] == 0,
              "single checkpoint -> one visit (the only regime currently reachable)");
        check(reverse_visit_order(0).empty(),
              "zero checkpoints -> no visits, and no underflow on the unsigned countdown");
    }

    // ============ 9F.D100 (review section 7): THE BLOCK-WISE GATE, max_q rho_q <= 1 ========
    //
    // The centrepiece is THE MASKING CASE. mu is 3,321 of 1,080,491 entries, so it can be
    // essentially 100% wrong while the GLOBAL ratio stays far under tolerance. This campaign
    // has been bitten by that shape before -- sigma_max(J) looked state-invariant until it
    // was split by RhsMode, because a dominant channel hid one five orders below it.
    {
        using wrf::sdirk3::BlockResidual;
        using wrf::sdirk3::SolveVerdict;
        using wrf::sdirk3::assess_adjoint_solve;
        using wrf::sdirk3::assess_adjoint_solve_blockwise;
        using wrf::sdirk3::worst_block;
        const double rtol = 1e-5;

        // mu carries a SMALL rhs of its own (it is a perturbation field, orders below the
        // coupled momenta) and is 180% wrong. That is what makes the masking real: the first
        // version of this fixture used |b_mu| = 3.0e2, which made the global ratio 2.1e-3 --
        // above tolerance, so the global gate refused too and the test proved nothing. The
        // assertion failing is what caught it.
        std::vector<BlockResidual> masked = {
            {"ru", 1e-6, 1.0e5}, {"rv", 1e-6, 1.0e5}, {"rw", 1e-6, 1.0e4},
            {"ph", 1e-6, 1.0e4}, {"t",  1e-6, 1.0e2}, {"mu", 0.9,  0.5},
        };

        // The global view: sum in quadrature, exactly what the old gate measured.
        double gr = 0.0, gb = 0.0;
        for (const auto& b : masked) { gr += b.residual_norm * b.residual_norm;
                                       gb += b.rhs_norm * b.rhs_norm; }
        gr = std::sqrt(gr); gb = std::sqrt(gb);

        check(assess_adjoint_solve(gr, gb, false, rtol) == SolveVerdict::Converged,
              "MASKING: the GLOBAL gate calls it converged (rel=" +
                  sci(gr / gb) + ") while mu is 100% wrong");
        check(assess_adjoint_solve_blockwise(masked, false, rtol) == SolveVerdict::Continue,
              "MASKING: the BLOCK gate refuses -- max_q rho_q > 1");
        check(worst_block(masked).name == "mu",
              "MASKING: telemetry names mu as the block holding the solve back");

        // Not vacuously strict: all blocks good -> Converged.
        std::vector<BlockResidual> all_good = {
            {"ru", 1e-7, 1.0e5}, {"rv", 1e-7, 1.0e5}, {"rw", 1e-7, 1.0e4},
            {"ph", 1e-7, 1.0e4}, {"t",  1e-7, 1.0e2}, {"mu", 1e-7, 3.0e2},
        };
        check(assess_adjoint_solve_blockwise(all_good, false, rtol) == SolveVerdict::Converged,
              "all blocks within tolerance -> Converged (the gate is not simply strict)");

        // Fatal short-circuits: one NaN block condemns the whole solve.
        auto with_nan = all_good;
        with_nan[3].residual_norm = std::nan("");
        check(assess_adjoint_solve_blockwise(with_nan, false, rtol) == SolveVerdict::Fatal,
              "one NaN block -> Fatal, not merely Continue");

        // Happy breakdown still wins when EVERY block is converged.
        check(assess_adjoint_solve_blockwise(all_good, true, rtol) == SolveVerdict::Converged,
              "happy breakdown with all blocks converged -> Converged (D98 holds per block)");
        check(assess_adjoint_solve_blockwise(masked, true, rtol) == SolveVerdict::Fatal,
              "breakdown with an unconverged block -> Fatal");

        // An empty block list is not convergence.
        check(assess_adjoint_solve_blockwise({}, false, rtol) == SolveVerdict::Fatal,
              "no blocks measured -> Fatal (silence is not convergence)");
    }

    // ========= 9F.D101 (review P0-A): THE POSITIVE SOLVE CONTROL, at last =================
    //
    // A^T = I - alpha*J^T with J = I and alpha = 0.5 is exactly 0.5*I. From x0 = 0:
    //   v1 = b/|b| ;  w = A^T v1 = 0.5 v1 ;  h11 = 0.5 ;  w - h11 v1 = 0  ->  h21 = 0
    // That is a HAPPY BREAKDOWN at j = 1, and the 1-D Krylov space already contains the exact
    // solution: y = |b|/0.5, x = 2b, residual 0.
    //
    // It used to return |x| = 0. I attributed that to solve_gmres assuming WRF-shaped state
    // and wrote "an 8-element toy never converges under ANY preconditioner" into this file as
    // if it were a harness limitation. IT WAS NOT. It was a defect: the breakdown path broke
    // out before the Givens reduction and then a `j <= 2` branch returned the initial guess.
    // The review caught the rationalisation by doing the arithmetic.
    //
    // This is the positive control whose absence I flagged twice as unavoidable.
    {
        IdentityPreconditioner ident;
        auto lin = torch::zeros({N}, opts());
        auto rhs = torch::ones({N}, opts());
        bool threw = false;
        torch::Tensor x;
        try {
            x = wrf::sdirk3::solve_transpose_linear_system_gmres(
                fast_operator, lin, rhs, /*alpha=*/0.5, ident,
                /*gmres_restart=*/10, /*gmres_max_iterations=*/10,
                /*gmres_tolerance=*/1e-5f);
        } catch (const std::exception&) { threw = true; }

        check(!threw, "0.5I: the solve SUCCEEDS (happy breakdown is convergence)");
        if (!threw && x.defined()) {
            const double err =
                (x - 2.0 * rhs).norm().item<double>() / (2.0 * rhs).norm().item<double>();
            check(err < 1e-5, "0.5I: recovers x = 2b exactly (rel err " + sci(err) + ")");
            const double res =
                (rhs - 0.5 * x).norm().item<double>() / rhs.norm().item<double>();
            check(res < 1e-5, "0.5I: physical residual ~ 0 (" + sci(res) + ")");
        } else {
            check(false, "0.5I: recovers x = 2b");
            check(false, "0.5I: physical residual ~ 0");
        }
    }

    // ===== 9F.D103 (review section 6): THE TOLERANCES ARE VALIDATED =====================
    // This gate exists to refuse wrong gradients, and an unvalidated rtol switches it off:
    // rtol = Inf makes bar = Inf, so every finite residual "converges". Fail-OPEN, in the one
    // place that must fail closed.
    {
        using wrf::sdirk3::SolveVerdict;
        using wrf::sdirk3::assess_adjoint_solve;
        const double inf = std::numeric_limits<double>::infinity();

        check(assess_adjoint_solve(1e9, 1.0, false, inf) == SolveVerdict::Fatal,
              "rtol = Inf -> Fatal (it used to accept a residual of 1e9)");
        check(assess_adjoint_solve(1e9, 1.0, false, 1e-5, inf) == SolveVerdict::Fatal,
              "atol = Inf -> Fatal");
        check(assess_adjoint_solve(1e9, 1.0, false, std::nan("")) == SolveVerdict::Fatal,
              "rtol = NaN -> Fatal");
        check(assess_adjoint_solve(1.0, 1.0, false, -1.0) == SolveVerdict::Fatal,
              "negative rtol -> Fatal (a negative tolerance is not a tolerance)");
        check(assess_adjoint_solve(1.0, 1.0, false, 1e-5, -1.0) == SolveVerdict::Fatal,
              "negative atol -> Fatal");
        // Overflowed bar admits everything -- same fail-open shape as rtol = Inf.
        check(assess_adjoint_solve(1e9, 1e300, false, 1e300) == SolveVerdict::Fatal,
              "bar overflows to Inf -> Fatal, not silent acceptance");
        // Still not vacuously strict.
        check(assess_adjoint_solve(1e-9, 1.0, false, 1e-5, 0.0) == SolveVerdict::Converged,
              "ordinary finite tolerances still converge");
    }

    // ===== 9F.D114 (review section 9): A ZERO-RHS *BLOCK* IS NOT FATAL ====================
    // Fatal-on-zero-rhs is right for the WHOLE system (b = 0 => x = 0). It is wrong per
    // block: in a coupled system a block can have b_q = 0 and carry a legitimate nonzero
    // residual through coupling, which further iterations reduce. The review's example:
    //     A = [[1,1],[1,2]], b = (1,0)^T  -- b_2 = 0, r_2 != 0 at intermediate iterates.
    {
        using wrf::sdirk3::BlockResidual;
        using wrf::sdirk3::SolveVerdict;
        using wrf::sdirk3::assess_adjoint_solve;
        using wrf::sdirk3::assess_adjoint_solve_blockwise;
        const double rtol = 1e-5;

        std::vector<BlockResidual> coupled = {
            {"ru", 1e-9, 1.0},     // fine
            {"mu", 0.3,  0.0},     // zero RHS, nonzero residual via coupling
        };
        check(assess_adjoint_solve_blockwise(coupled, false, rtol) == SolveVerdict::Continue,
              "zero-RHS block with a residual -> Continue, NOT Fatal (coupling is legitimate)");
        check(assess_adjoint_solve(0.3, 0.0, false, rtol) == SolveVerdict::Fatal,
              "the WHOLE-system rule is unchanged: b=0 with a residual is still Fatal");

        std::vector<BlockResidual> settled = {
            {"ru", 1e-9, 1.0},
            {"mu", 0.0,  0.0},     // zero RHS, zero residual
        };
        check(assess_adjoint_solve_blockwise(settled, false, rtol) == SolveVerdict::Converged,
              "zero-RHS block that IS zero -> Converged");

        std::vector<BlockResidual> nan_blk = {
            {"ru", 1e-9, 1.0},
            {"mu", std::nan(""), 0.0},
        };
        check(assess_adjoint_solve_blockwise(nan_blk, false, rtol) == SolveVerdict::Fatal,
              "zero-RHS block with a NaN residual is STILL Fatal");
    }

    constexpr int expected_checks = 48;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) { std::cout << "ADJOINT_SOLVE_AUTHORITY: PASS" << std::endl; return 0; }
    std::cout << "ADJOINT_SOLVE_AUTHORITY: FAIL (" << failures << ")" << std::endl;
    return 1;
}
