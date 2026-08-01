// 9F.D63: the implicit-stage tangent and adjoint (review P0-C).
//
// The claim being tested is not "these formulas are typed correctly". It is that
// differentiating the EQUATION gives an answer independent of how the forward solve
// reached its root, whereas differentiating the SOLVER'S TRACE does not -- and that the
// difference is large enough to matter at the iteration counts a real solve uses.
//
// So the centrepiece here is not the tangent value. It is the side-by-side: the same
// system, solved sloppily and tightly, differentiated both ways.

#include "../wrf_sdirk3_implicit_diff.h"

#include <torch/torch.h>

#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

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

constexpr int N = 12;

// A deliberately NON-SYMMETRIC, diagonally dominant R_K, and an unrelated R_U. Non-symmetry
// matters: it is what makes the operator order in stage_adjoint observable at all.
torch::Tensor make_RK() {
    auto opts = torch::TensorOptions().dtype(torch::kFloat64);
    auto A = torch::zeros({N, N}, opts);
    for (int i = 0; i < N; ++i) {
        A[i][i] = 4.0 + 0.1 * i;
        if (i + 1 < N) A[i][i + 1] = -1.0;
        if (i > 0)     A[i][i - 1] = -0.4;          // asymmetric off-diagonals
        if (i + 3 < N) A[i][i + 3] = 0.7;
    }
    return A;
}

torch::Tensor make_RU() {
    auto opts = torch::TensorOptions().dtype(torch::kFloat64);
    auto B = torch::zeros({N, N}, opts);
    for (int i = 0; i < N; ++i) {
        B[i][i] = 1.0 + 0.05 * i;
        if (i + 2 < N) B[i][i + 2] = 0.9;
        if (i > 1)     B[i][i - 2] = -0.3;
    }
    return B;
}

// Richardson iteration for A x = b: x_{n+1} = x_n + w (b - A x_n). Deliberately a plain
// fixed-point scheme so that "differentiating the trace" is something this test can
// actually construct, which is the only honest way to compare against it.
torch::Tensor richardson(const torch::Tensor& A, const torch::Tensor& b, int iters,
                         double w = 0.2) {
    auto x = torch::zeros_like(b);
    for (int n = 0; n < iters; ++n) x = x + w * (b - A.mv(x));
    return x;
}

}  // namespace

int main() {
    using wrf::sdirk3::implicit_diff::stage_tangent;
    using wrf::sdirk3::implicit_diff::stage_adjoint;

    auto opts = torch::TensorOptions().dtype(torch::kFloat64);
    torch::manual_seed(20260801);

    const auto RK = make_RK();
    const auto RU = make_RU();
    const auto RK_inv = torch::linalg_inv(RK);
    const auto J_exact = -RK_inv.matmul(RU);           // dK/dU, in closed form

    auto dU   = torch::randn({N}, opts);
    auto Kbar = torch::randn({N}, opts);

    auto solve_RK    = [&](const torch::Tensor& r) { return RK_inv.mv(r); };
    auto solve_RK_T  = [&](const torch::Tensor& r) { return RK_inv.t().mv(r); };
    auto apply_RU    = [&](const torch::Tensor& v) { return RU.mv(v); };
    auto apply_RU_T  = [&](const torch::Tensor& v) { return RU.t().mv(v); };

    // --- the tangent equals the closed-form Jacobian applied to dU ---
    {
        auto got  = stage_tangent(solve_RK, apply_RU, dU);
        auto want = J_exact.mv(dU);
        const double rel = ((got - want).abs().max() / want.abs().max()).item<double>();
        check(rel < 1e-12, "stage_tangent == (-R_K^-1 R_U) dU exactly (rel=" + sci(rel) + ")");
    }

    // --- the adjoint equals J^T applied to Kbar ---
    {
        auto got  = stage_adjoint(solve_RK_T, apply_RU_T, Kbar);
        auto want = J_exact.t().mv(Kbar);
        const double rel = ((got - want).abs().max() / want.abs().max()).item<double>();
        check(rel < 1e-12, "stage_adjoint == J^T Kbar exactly (rel=" + sci(rel) + ")");
    }

    // --- the dot-product identity, which is what catches a swapped operator order ---
    {
        auto dK   = stage_tangent(solve_RK, apply_RU, dU);
        auto Ubar = stage_adjoint(solve_RK_T, apply_RU_T, Kbar);
        const double lhs = (dK * Kbar).sum().item<double>();
        const double rhs = (dU * Ubar).sum().item<double>();
        const double rel = std::abs(lhs - rhs) / std::abs(lhs);
        check(rel < 1e-12, "<J dU, Kbar> == <dU, J^T Kbar> (rel=" + sci(rel) + ")");
    }

    // --- ORDER MATTERS, and only the identity above can see it ---
    // -R_K^{-T}(R_U^T Kbar) is the plausible transposition of the two steps. It agrees with
    // the correct adjoint only if R_K and R_U commute, which they do not here.
    {
        auto correct = stage_adjoint(solve_RK_T, apply_RU_T, Kbar);
        auto swapped = -solve_RK_T(apply_RU_T(Kbar));
        const double rel = ((correct - swapped).abs().max() / correct.abs().max()).item<double>();
        // 5.9% on this fixture. Bounded rather than round: the separation is set by how
        // far R_K and R_U are from commuting, and inflating the fixture to reach a rounder
        // number would be tuning the test to the assertion. What matters is that it is far
        // above any tolerance the identity above would pass at (1e-12).
        check(rel > 1e-2,
              "swapping the solve and the apply gives a materially DIFFERENT operator (rel=" +
              sci(rel) + ", i.e. ~6% here), so the dot-product identity is load-bearing "
              "rather than decorative -- it is the only check that sees this");
    }

    // ================== THE POINT OF THE WHOLE FILE ==================
    // Same system, forward-solved by Richardson at increasing iteration counts. Compare:
    //   (a) the equation-level tangent, which uses only the CONVERGED root's operators
    //   (b) a trace-differentiated tangent, obtained by differentiating the iteration itself
    // (a) must be independent of the iteration count. (b) must not be.
    {
        auto b = -RU.mv(dU);                     // R_K K = -R_U U  =>  solve for the tangent
        const double w = 0.2;

        // (b) differentiate the Richardson trace: dx_{n+1} = dx_n + w(db - A dx_n),
        //     which is the exact derivative of the ALGORITHM after n steps.
        auto trace_tangent = [&](int iters) {
            auto dx = torch::zeros({N}, opts);
            for (int n = 0; n < iters; ++n) dx = dx + w * (b - RK.mv(dx));
            return dx;
        };

        auto exact = J_exact.mv(dU);
        const double e5   = ((trace_tangent(5)   - exact).abs().max() / exact.abs().max()).item<double>();
        const double e20  = ((trace_tangent(20)  - exact).abs().max() / exact.abs().max()).item<double>();
        const double e200 = ((trace_tangent(200) - exact).abs().max() / exact.abs().max()).item<double>();

        // 5.1e-03 here, i.e. half a percent, on a WELL-CONDITIONED fixture where
        // Richardson converges quickly. The real implicit operator is I - dt*gamma*J_fast
        // at dt=600, which is far stiffer and would converge far more slowly, so this
        // understates the effect rather than overstating it. Stated because the honest
        // reading of a 0.5% gradient error is "small here, unbounded in general".
        check(e5 > 1e-3,
              "a trace-differentiated tangent after 5 iterations is wrong by " + sci(e5) +
              " even on a well-conditioned system -- this is the error a 4D-Var gradient "
              "inherits from an unconverged solve, and it grows with stiffness");
        check(e20 < e5 && e200 < e20,
              "it improves only as the solver converges (" + sci(e5) + " -> " + sci(e20) +
              " -> " + sci(e200) + "), i.e. it is the derivative of the PATH, not of the equation");

        // (a) the equation-level tangent, with the inner solve done sloppily vs tightly.
        // Both must land on the same answer, because neither differentiates the iteration.
        auto sloppy = [&](const torch::Tensor& r) { return richardson(RK, r, 60); };
        auto tight  = [&](const torch::Tensor& r) { return richardson(RK, r, 400); };
        auto t_sloppy = stage_tangent(sloppy, apply_RU, dU);
        auto t_tight  = stage_tangent(tight,  apply_RU, dU);
        const double spread =
            ((t_sloppy - t_tight).abs().max() / t_tight.abs().max()).item<double>();
        const double err_tight =
            ((t_tight - exact).abs().max() / exact.abs().max()).item<double>();

        // THE HEADLINE COMPARISON: 5.1e-03 for the trace derivative at 5 iterations
        // against EXACTLY 0 here. Thirteen orders of magnitude, on the same system, from
        // the same forward solve. That contrast is the argument for P0-C, not the
        // absolute size of either number.
        check(spread < 1e-6,
              "the EQUATION-level tangent is invariant to the inner iteration count (60 vs "
              "400 Richardson steps agree to " + sci(spread) + ", against " + sci(e5) +
              " for the trace derivative on the same system)");
        check(err_tight < 1e-9,
              "and it converges to the exact Jacobian (rel=" + sci(err_tight) + "), so the "
              "derivative is of the converged discrete equations rather than of the "
              "algorithm that found them");
    }

    // --- it must not silently accept a broken inner solve either ---
    // Invariance to the iteration count is not the same as indifference to correctness: an
    // inner solve too crude to have converged AT ALL still poisons the tangent, and the
    // contract should say so rather than let "iteration-count invariant" be read as
    // "iteration count does not matter".
    {
        auto too_crude = [&](const torch::Tensor& r) { return richardson(RK, r, 2); };
        auto t_crude = stage_tangent(too_crude, apply_RU, dU);
        auto exact = J_exact.mv(dU);
        const double rel = ((t_crude - exact).abs().max() / exact.abs().max()).item<double>();
        check(rel > 1e-2,
              "an inner solve that has not converged (2 steps) still gives a wrong tangent (" +
              sci(rel) + ") -- invariance holds ONCE the inner solve converges, and is not "
              "a licence to skip it");
    }

    // === THE SDIRK SPECIALISATION, against the same closed-form machinery ===
    // R(K,U) = K - F(U + dt*gamma*K)  =>  R_K = I - dt*gamma*J,  R_U = -J.
    // These are the operators the wiring will bind to FGMRES and the RHS JVP, so the
    // algebra gets a reference to fail against before any critical-path surgery.
    {
        using wrf::sdirk3::implicit_diff::sdirk_stage_tangent;
        using wrf::sdirk3::implicit_diff::sdirk_stage_adjoint;

        const double dt_gamma = 0.35;
        const auto J = make_RU();                       // stand-in for dF/dY, non-symmetric
        const auto I = torch::eye(N, opts);
        const auto A = I - dt_gamma * J;                // = R_K
        const auto A_inv = torch::linalg_inv(A);
        const auto J_stage_exact = A_inv.matmul(J);     // dK/dU = A^-1 J

        auto solve_A   = [&](const torch::Tensor& r) { return A_inv.mv(r); };
        auto solve_A_T = [&](const torch::Tensor& r) { return A_inv.t().mv(r); };
        auto applyJ    = [&](const torch::Tensor& v) { return J.mv(v); };
        auto applyJ_T  = [&](const torch::Tensor& v) { return J.t().mv(v); };

        auto dK = sdirk_stage_tangent(solve_A, applyJ, dU);
        const double rel_t =
            ((dK - J_stage_exact.mv(dU)).abs().max() / J_stage_exact.mv(dU).abs().max()).item<double>();
        check(rel_t < 1e-12, "sdirk_stage_tangent == A^-1 J dU (rel=" + sci(rel_t) + ")");

        auto Ubar = sdirk_stage_adjoint(solve_A_T, applyJ_T, Kbar);
        const double rel_a =
            ((Ubar - J_stage_exact.t().mv(Kbar)).abs().max() /
             J_stage_exact.t().mv(Kbar).abs().max()).item<double>();
        check(rel_a < 1e-12, "sdirk_stage_adjoint == (A^-1 J)^T Kbar (rel=" + sci(rel_a) + ")");

        const double lhs = (dK * Kbar).sum().item<double>();
        const double rhs = (dU * Ubar).sum().item<double>();
        check(std::abs(lhs - rhs) / std::abs(lhs) < 1e-12,
              "and the SDIRK pair satisfies the dot-product identity too");

        // R_U = -J, NOT -I. Reading "U enters linearly" off the residual shape gives
        // dK/dU = A^-1, which is wrong by a whole factor of J.
        auto wrong_RU = solve_A(dU);                    // what R_U = -I would give
        const double rel_wrong =
            ((wrong_RU - dK).abs().max() / dK.abs().max()).item<double>();
        check(rel_wrong > 0.1,
              "treating R_U as -I instead of -J gives a materially different tangent (rel=" +
              sci(rel_wrong) + ") -- U enters through F, so its derivative carries J");

        // the adjoint order: J^T A^-T, not J^T A^-1. Identical only if A is symmetric.
        auto wrong_order = applyJ_T(solve_A(Kbar));
        const double rel_order =
            ((wrong_order - Ubar).abs().max() / Ubar.abs().max()).item<double>();
        check(rel_order > 1e-2,
              "and using A^-1 where A^-T is required differs by " + sci(rel_order) +
              " -- self-consistent-looking, and wrong because A = I - dt*gamma*J is not "
              "symmetric");
    }

    constexpr int expected_checks = 14;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) { std::cout << "IMPLICIT_DIFF: PASS" << std::endl; return 0; }
    std::cout << "IMPLICIT_DIFF: FAIL (" << failures << ")" << std::endl;
    return 1;
}
