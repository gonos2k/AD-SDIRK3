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
//   4. compute_jvp_dual(F, u, v)        == A v     (true JVP, dual numbers)
//   5. set_use_finite_diff_jvp(true) must NOT flip compute_vjp_autograd back
//      to returning A v (the toggle is inert for the VJP — the exact round-2
//      failure mode, pinned).
//
// Requires libtorch. Registered in CMake (VJP_Semantics); buildable standalone:
//   clang++ -std=c++17 test_vjp_semantics.cpp wrf_sdirk3_jvp_autograd.cpp \
//     wrf_sdirk3_config.cpp -I. $(torch include/lib flags) -o t && ./t

#include "wrf_sdirk3_jvp_autograd.h"

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
    using wrf::sdirk3::compute_jvp_dual;
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

    // 4: "dual" JVP is J v (its implementation is also FD-based; same epsilon note).
    {
        auto jvp = compute_jvp_dual(F, u, v, /*epsilon=*/1e-2f);
        expect_close(jvp, Av, "compute_jvp_dual == A v (true JVP)");
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

    if (g_failures == 0) {
        std::printf("ALL vjp_semantics assertions PASSED\n");
        return 0;
    }
    std::printf("%d FAILURES\n", g_failures);
    return 1;
}
