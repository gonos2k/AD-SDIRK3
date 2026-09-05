// test_vjp_semantics.cpp — nonsymmetric-matrix semantics proof for the AD
// helpers (full-repository review acceptance test #1; Codex round-3 on the
// P1-3 rename: "the changed path is untested").
//
// The defect class this pins down: compute_jvp_autograd was renamed to
// compute_vjp_autograd because its autograd branch computes J^T v — but the
// function ALSO had an FD dispatch (default ON) that returned J*v, so the
// rename alone left one name returning two different mathematical objects.
// For a SYMMETRIC Jacobian J*v == J^T v and no test can tell them apart, so
// this test uses a deliberately NONSYMMETRIC linear map
//
//     F(u) = A u,   A = [[1, 2],
//                        [0, 3]]
//
// where, for v = (1, 1):  J v = A v = (3, 3)   and   J^T v = A^T v = (1, 5).
//
// Assertions (executed against the REAL production translation units,
// wrf_sdirk3_jvp_autograd.cpp + wrf_sdirk3_config.cpp — not a reimplementation):
//   1. compute_vjp_autograd(F, u, v)    == A^T v   (reverse mode, always)
//   2. compute_vjp_autograd(F, u, v)    != A v     (would hold if the old
//                                                    default-FD dispatch ever
//                                                    came back — regression pin)
//   3. compute_jvp_finite_diff(F, u, v) == A v     (true JVP)
//   4. compute_jvp_forward_diff(F, u, v)        == A v     (true JVP, forward diff)
//   5. set_use_finite_diff_jvp(true) must NOT flip compute_vjp_autograd back
//      to returning A v (the toggle is inert for the VJP — the exact round-2
//      failure mode, pinned).
//
// Requires libtorch. Registered in CMake (VJP_Semantics); buildable standalone:
//   clang++ -std=c++17 test_vjp_semantics.cpp wrf_sdirk3_jvp_autograd.cpp \
//     wrf_sdirk3_config.cpp -I. $(torch include/lib flags) -o t && ./t

#include "wrf_sdirk3_jvp_autograd.h"
#include "wrf_sdirk3_imex_adjoint_linear_solve.h"

#include <torch/torch.h>

#include <cstdio>

namespace {

int g_failures = 0;

void expect_close(const torch::Tensor& got, const torch::Tensor& want,
                  const char* what) {
    const bool ok = got.defined() &&
                    torch::allclose(got, want, /*rtol=*/1e-4, /*atol=*/1e-5);
    if (ok) {
        std::printf("PASS  %s\n", what);
    } else {
        std::printf("FAIL  %s\n", what);
        std::printf("      got:  [%g, %g]\n",
                    got[0].item<double>(), got[1].item<double>());
        std::printf("      want: [%g, %g]\n",
                    want[0].item<double>(), want[1].item<double>());
        ++g_failures;
    }
}

void expect_not_close(const torch::Tensor& got, const torch::Tensor& not_want,
                      const char* what) {
    const bool distinct = got.defined() &&
                          !torch::allclose(got, not_want, 1e-4, 1e-5);
    if (distinct) {
        std::printf("PASS  %s\n", what);
    } else {
        std::printf("FAIL  %s (matched the value it must NOT equal)\n", what);
        ++g_failures;
    }
}

}  // namespace

int main() {
    // (No outer NoGradGuard: it would disable grad mode process-wide and kill
    // the reverse-mode graph compute_vjp_autograd builds internally — the
    // first run of this test proved that the hard way.)
    // Nonsymmetric A: J v and J^T v are distinct — the only regime in which
    // JVP/VJP confusion is observable.
    auto A = torch::tensor({{1.0f, 2.0f}, {0.0f, 3.0f}});
    auto F = [&A](const torch::Tensor& u) -> torch::Tensor {
        return torch::matmul(A, u);
    };
    auto u = torch::tensor({1.0f, 1.0f});
    auto v = torch::tensor({1.0f, 1.0f});

    const auto Av  = torch::tensor({3.0f, 5.0f - 2.0f});  // A v   = (3, 3)
    const auto ATv = torch::tensor({1.0f, 5.0f});          // A^T v = (1, 5)

    using wrf::sdirk3::compute_vjp_autograd;
    using wrf::sdirk3::compute_jvp_finite_diff;
    using wrf::sdirk3::compute_jvp_forward_diff;
    using wrf::sdirk3::set_use_finite_diff_jvp;

    // 1+2: the VJP is J^T v and is NOT J v.
    {
        auto vjp = compute_vjp_autograd(F, u, v);
        expect_close(vjp, ATv, "compute_vjp_autograd == A^T v (reverse mode)");
        expect_not_close(vjp, Av,
                         "compute_vjp_autograd != A v (JVP/VJP genuinely distinct here)");
    }

    // 3: finite-difference JVP is J v. Explicit epsilon=1e-2: F is LINEAR, so
    // any step is exact in real arithmetic, while the tiny defaults (1e-5 /
    // 1e-6) sit at float32's resolution near u=1.0 and drown the difference
    // quotient in cancellation noise (first run measured 0.4% / 4.6% error).
    {
        auto jvp = compute_jvp_finite_diff(F, u, v, /*epsilon=*/1e-2f);
        expect_close(jvp, Av, "compute_jvp_finite_diff == A v (true JVP)");
    }

    // 4: forward-diff JVP is J v (renamed from the misleading "dual"; same epsilon note).
    {
        auto jvp = compute_jvp_forward_diff(F, u, v, /*epsilon=*/1e-2f);
        expect_close(jvp, Av, "compute_jvp_forward_diff == A v (true JVP, renamed)");
    }

    // 5: the legacy FD toggle must NOT flip the VJP back into a JVP — the
    // exact round-2 failure mode (default-ON dispatch returning A v).
    {
        set_use_finite_diff_jvp(true);
        auto vjp = compute_vjp_autograd(F, u, v);
        expect_close(vjp, ATv,
                     "compute_vjp_autograd == A^T v even with FD toggle ON (dispatch removed)");
        set_use_finite_diff_jvp(false);
    }

    // A detached constant is a valid zero-Jacobian operator.  The reverse
    // helpers must handle it explicitly because autograd::grad cannot operate
    // on an output with no grad_fn, even with allow_unused=true.
    {
        auto constant = [](const torch::Tensor& x) {
            return torch::zeros_like(x);
        };
        auto zero = compute_vjp_autograd(constant, u, v);
        expect_close(zero, torch::zeros_like(u),
                     "compute_vjp_autograd detached constant == zero");
        if (zero.requires_grad()) {
            std::printf("FAIL  detached constant VJP unexpectedly requires grad\n");
            ++g_failures;
        } else {
            std::printf("PASS  detached constant VJP has no grad edge\n");
        }
        bool threw = false;
        try {
            (void)compute_vjp_autograd(constant, u, torch::zeros({3}, u.options()));
        } catch (const c10::Error&) {
            threw = true;
        }
        if (threw) {
            std::printf("PASS  detached constant rejects cotangent shape mismatch\n");
        } else {
            std::printf("FAIL  detached constant accepted cotangent shape mismatch\n");
            ++g_failures;
        }

        auto captured = torch::tensor(7.0f).requires_grad_(true);
        auto independent = [&captured](const torch::Tensor& x) {
            return captured.expand_as(x);
        };
        auto independent_vjp = wrf::sdirk3::compute_vjp_reverse_mode(
            independent, u, v);
        expect_close(independent_vjp, torch::zeros_like(u),
                     "reverse VJP independent of input == input-shaped zero");

        auto rectangular = [](const torch::Tensor& x) {
            return torch::zeros({3}, x.options());
        };
        auto rectangular_vjp = compute_vjp_autograd(
            rectangular, u, torch::zeros({3}));
        if (rectangular_vjp.sizes() == u.sizes() &&
            torch::count_nonzero(rectangular_vjp).item<int>() == 0) {
            std::printf("PASS  rectangular constant VJP is input-shaped zero\n");
        } else {
            std::printf("FAIL  rectangular constant VJP shape/value\n");
            ++g_failures;
        }
    }

    // The production transpose solve can invoke a VJP while diagnostics hold
    // NoGradGuard.  A linear graph must still be recorded in that context;
    // only a genuinely detached output is a zero Jacobian.
    {
        torch::NoGradGuard no_grad;
        auto linear = [](const torch::Tensor& x) { return 3.0f * x; };
        auto got = compute_vjp_autograd(linear, u, v);
        expect_close(got, 3.0f * v,
                     "compute_vjp_autograd re-enables graph under NoGradGuard");
        auto got_reverse = wrf::sdirk3::compute_vjp_reverse_mode(linear, u, v);
        expect_close(got_reverse, 3.0f * v,
                     "compute_vjp_reverse_mode re-enables graph under NoGradGuard");
        auto zero = compute_vjp_autograd(
            [](const torch::Tensor& x) { return torch::zeros_like(x); }, u, v);
        expect_close(zero, torch::zeros_like(u),
                     "detached constant remains zero under NoGradGuard");
    }

    if (g_failures == 0) {
        std::printf("ALL vjp_semantics assertions PASSED\n");
        return 0;
    }
    std::printf("%d FAILURES\n", g_failures);
    return 1;
}
