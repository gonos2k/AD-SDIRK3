// test_fgmres_contract.cpp — acceptance tests for the FGMRES conversion
// (full-repo review P1-1; reviewer-directed test matrix A–F).
//
// Exercises the REAL production krylov_methods::solve_fgmres / solve_gmres
// (linked from the production archive), on small deterministic nonsymmetric
// systems where every property is checkable against a direct solve.
//
//   A. variable-PC correctness: the preconditioner CHANGES mid-solve (the
//      production failure mode: ratio-guard identity lock). FGMRES must reach
//      the true-residual tolerance; standard GMRES with the same variable PC
//      is the NEGATIVE CONTROL (its M_inv(sum yV) reconstruction is
//      inconsistent with its Arnoldi Hessenberg).
//   B. fixed-PC equivalence: with a FIXED linear M^{-1}, FGMRES and GMRES
//      agree (same math), solutions within FP32 tolerance.
//   C. unpreconditioned regression: M_inv == nullptr routes identically —
//      FGMRES stores no Z, uses V, and must be BITWISE equal to GMRES.
//   D. apply-count contract: M_inv is called EXACTLY once per Arnoldi vector
//      in FGMRES (zero calls in trial/final reconstruction); standard GMRES
//      is the negative control (it re-applies M_inv on aggregates).
//   E. multi-restart + nonzero warm start + variable PC.
//   F. ratio-guard-style mid-solve lock: real M^{-1} then identity, both Z
//      kinds coexisting in one basis, converged true residual.
//
// No RNG anywhere: matrices are fixed, so failures are reproducible.

#include "wrf_sdirk3_newton_solver.h"

#include <torch/torch.h>

#include <cstdio>
#include <functional>

namespace {

int g_failures = 0;

void expect(bool cond, const char* what) {
    if (cond) std::printf("PASS  %s\n", what);
    else { std::printf("FAIL  %s\n", what); ++g_failures; }
}

// Deterministic nonsymmetric well-conditioned 8x8 test matrix:
// diagonally dominant (diag 3..10) with asymmetric off-diagonals.
torch::Tensor make_A() {
    const int n = 8;
    auto A = torch::zeros({n, n});
    for (int i = 0; i < n; ++i) {
        A[i][i] = 3.0f + i;
        if (i + 1 < n) A[i][i + 1] = 0.7f;          // super-diagonal
        if (i - 1 >= 0) A[i][i - 1] = -0.3f;        // sub-diagonal (asymmetric)
        if (i + 3 < n) A[i][i + 3] = 0.2f;          // wide band, one-sided
    }
    return A;
}

float true_rel_residual(const torch::Tensor& A, const torch::Tensor& b,
                        const torch::Tensor& x) {
    auto r = b - torch::matmul(A, x);
    return (r.norm() / b.norm()).item<float>();
}

}  // namespace

int main() {
    using wrf::sdirk3::krylov_methods::solve_gmres;
    using wrf::sdirk3::krylov_methods::solve_fgmres;

    const auto A_mat = make_A();
    auto A_op = [&A_mat](const torch::Tensor& v) -> torch::Tensor {
        return torch::matmul(A_mat, v);
    };
    const auto b = torch::tensor({1.0f, -2.0f, 3.0f, -4.0f, 5.0f, -6.0f, 7.0f, -8.0f});
    const auto x0_zero = torch::zeros({8});
    const auto x_direct = torch::linalg_solve(A_mat.to(torch::kFloat64),
                                              b.to(torch::kFloat64).unsqueeze(1))
                              .squeeze(1).to(torch::kFloat32);

    const float tol = 1e-5f;
    const int restart = 8, max_restarts = 3;

    // A strong diagonal preconditioner (exact inverse of diag(A)) vs identity —
    // the two "phases" a ratio-guard lock switches between.
    const auto diag_inv = 1.0f / A_mat.diagonal();
    auto M_diag = [&diag_inv](const torch::Tensor& v) { return v * diag_inv; };

    // ---------- A + F: variable PC (real -> identity lock after 2 applies) ----
    {
        int calls = 0, switch_at = 2, calls_real = 0, calls_id = 0;
        std::function<torch::Tensor(const torch::Tensor&)> M_var =
            [&](const torch::Tensor& v) -> torch::Tensor {
                const int c = calls++;
                if (c < switch_at) { ++calls_real; return v * diag_inv; }
                ++calls_id; return v;  // identity "lock", as the ratio guard does
            };
        auto rf = solve_fgmres(A_op, b, x0_zero, 0, 0.0f, restart, tol,
                               max_restarts, M_var, nullptr, nullptr, false, false);
        const float err_f = true_rel_residual(A_mat, b, rf.x);
        expect(rf.success && err_f <= tol,
               "A: FGMRES with mid-solve PC change converges (true residual)");
        expect(calls_real >= 1 && calls_id >= 1,
               "F: both PC phases (real + identity-lock) coexist in one solve");
        expect((rf.x - x_direct).norm().item<float>() /
                   x_direct.norm().item<float>() < 1e-3f,
               "A: FGMRES solution matches direct solve");

        // NEGATIVE CONTROL: standard GMRES with the same variable PC and a
        // SINGLE restart cycle. Its final correction M_latest^{-1}(sum yV) is
        // inconsistent with the Arnoldi Hessenberg once the PC changed
        // mid-basis. (With multiple restart cycles the inconsistency
        // SELF-HEALS: the next cycle re-solves the residual equation under the
        // now-locked PC over a fresh full-dimension basis — the first run of
        // this test proved that by having GMRES come out BETTER after 2 cycles.
        // The single-cycle budget is where the corruption is observable, and is
        // also the realistic regime: production restarts are far below n.)
        calls = 0; calls_real = 0; calls_id = 0;
        auto rf1 = solve_fgmres(A_op, b, x0_zero, 0, 0.0f, restart, tol,
                                /*max_restarts=*/1, M_var, nullptr, nullptr, false, false);
        const float err_f1 = true_rel_residual(A_mat, b, rf1.x);
        calls = 0; calls_real = 0; calls_id = 0;
        auto rg1 = solve_gmres(A_op, b, x0_zero, 0, 0.0f, restart, tol,
                               /*max_restarts=*/1, M_var, nullptr, nullptr, false, false);
        const float err_g1 = true_rel_residual(A_mat, b, rg1.x);
        expect(err_f1 <= tol,
               "A(neg-1): single-cycle FGMRES under variable PC still reaches tol");
        expect(err_g1 > tol,
               "A(neg-2): single-cycle standard GMRES under variable PC MISSES tol");
        expect(err_g1 > err_f1,
               "A(neg-3): its true residual is worse than FGMRES's");
        std::printf("      [info] single-cycle true rel residual: fgmres=%.3e  gmres(neg-ctrl)=%.3e\n",
                    err_f1, err_g1);
    }

    // ---------- B: fixed-PC equivalence ---------------------------------------
    {
        auto rf = solve_fgmres(A_op, b, x0_zero, 0, 0.0f, restart, tol,
                               max_restarts, M_diag, nullptr, nullptr, false, false);
        auto rg = solve_gmres(A_op, b, x0_zero, 0, 0.0f, restart, tol,
                              max_restarts, M_diag, nullptr, nullptr, false, false);
        const float err_f = true_rel_residual(A_mat, b, rf.x);
        const float err_g = true_rel_residual(A_mat, b, rg.x);
        expect(rf.success && err_f <= tol, "B: FGMRES converges with FIXED PC");
        expect(rg.success && err_g <= tol, "B: GMRES converges with FIXED PC");
        expect((rf.x - rg.x).norm().item<float>() /
                   (rg.x.norm().item<float>() + 1e-30f) < 1e-5f,
               "B: FGMRES == GMRES solution for fixed linear PC (FP32 rel 1e-5)");
    }

    // ---------- C: unpreconditioned path bitwise regression -------------------
    {
        std::function<torch::Tensor(const torch::Tensor&)> M_null;  // empty
        auto rf = solve_fgmres(A_op, b, x0_zero, 0, 0.0f, restart, tol,
                               max_restarts, M_null, nullptr, nullptr, false, false);
        auto rg = solve_gmres(A_op, b, x0_zero, 0, 0.0f, restart, tol,
                              max_restarts, M_null, nullptr, nullptr, false, false);
        expect(torch::equal(rf.x, rg.x),
               "C: M_inv==nullptr -> FGMRES bitwise-identical to GMRES (V used, no Z)");
        expect(rf.iterations == rg.iterations,
               "C: identical Arnoldi iteration count on unpreconditioned path");
    }

    // ---------- D: apply-count contract ---------------------------------------
    {
        int calls_f = 0;
        std::function<torch::Tensor(const torch::Tensor&)> M_count_f =
            [&](const torch::Tensor& v) { ++calls_f; return v * diag_inv; };
        auto rf = solve_fgmres(A_op, b, x0_zero, 0, 0.0f, restart, tol,
                               max_restarts, M_count_f, nullptr, nullptr, false, false);
        expect(calls_f == rf.iterations,
               "D: FGMRES applies M_inv EXACTLY once per Arnoldi vector (none on aggregates)");

        int calls_g = 0;
        std::function<torch::Tensor(const torch::Tensor&)> M_count_g =
            [&](const torch::Tensor& v) { ++calls_g; return v * diag_inv; };
        auto rg = solve_gmres(A_op, b, x0_zero, 0, 0.0f, restart, tol,
                              max_restarts, M_count_g, nullptr, nullptr, false, false);
        expect(calls_g > rg.iterations,
               "D(neg): standard GMRES re-applies M_inv on aggregates (count > Arnoldi)");
        std::printf("      [info] M_inv applies: fgmres=%d (arnoldi=%d)  gmres=%d (arnoldi=%d)\n",
                    calls_f, rf.iterations, calls_g, rg.iterations);
    }

    // ---------- E: multi-restart + nonzero x0 + variable PC -------------------
    {
        // Alternate the preconditioner EVERY call — worst-case variability —
        // and force multiple restart cycles with a small restart length.
        int calls = 0;
        std::function<torch::Tensor(const torch::Tensor&)> M_alt =
            [&](const torch::Tensor& v) -> torch::Tensor {
                return (calls++ % 2 == 0) ? v * diag_inv : v;
            };
        auto x0_warm = 0.1f * torch::ones({8});
        auto rf = solve_fgmres(A_op, b, x0_warm, 0, 0.0f, /*restart=*/3, tol,
                               /*max_restarts=*/6, M_alt, nullptr, nullptr, false, false);
        const float err = true_rel_residual(A_mat, b, rf.x);
        expect(rf.success && err <= tol,
               "E: multi-restart (len 3) + warm start + per-call alternating PC converges");
        expect((rf.x - x_direct).norm().item<float>() /
                   x_direct.norm().item<float>() < 1e-3f,
               "E: solution matches direct solve");
    }

    if (g_failures == 0) {
        std::printf("ALL fgmres_contract assertions PASSED\n");
        return 0;
    }
    std::printf("%d FAILURES\n", g_failures);
    return 1;
}
