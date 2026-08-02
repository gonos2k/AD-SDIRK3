// 9F.D81: the transpose probe, against operators whose transpose is known exactly.
//
// The case that justifies this file is SEVERED_IS_CAUGHT. In D80 the production probe was
// handed an autograd VJP that had recorded nothing and returned its own input; every
// property it measured (symmetry, linearity, repeatability, additivity) still held, because
// the identity is a perfectly good linear operator, and the reported rel=0.5223 read as a
// half-correct transpose rather than as no transpose at all.
//
// A contract that includes a deliberately severed operator catches that in a second. The
// in-solver probe could not have such a case -- that is the whole argument for the header.

#include "../wrf_sdirk3_transpose_probe.h"

#include <torch/torch.h>

#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace probe = wrf::sdirk3::transpose_probe;

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

constexpr int64_t N = 24;
constexpr uint64_t SEED = 20260802;

torch::TensorOptions opts() { return torch::TensorOptions().dtype(torch::kFloat64); }

// Deliberately NON-symmetric. Symmetry is what makes M^T observably different from M, so a
// symmetric fixture would let a probe that confuses the two pass everything.
torch::Tensor make_A() {
    auto gen = at::detail::createCPUGenerator(7);
    auto A = torch::randn({N, N}, gen, opts());
    A = A + torch::eye(N, opts()) * static_cast<double>(N);   // well conditioned
    A.index_put_({0, 1}, A.index({0, 1}) + 5.0);              // and firmly non-symmetric
    return A;
}

std::string summary_of(const probe::TransposeReport& r) { return r.summary(); }

bool mentions(const std::string& s, const std::string& needle) {
    return s.find(needle) != std::string::npos;
}

}  // namespace

int main() {
    std::cout << "=== Transpose_Probe_Contract ===" << std::endl;

    const auto A = make_A();
    auto M       = [&](const torch::Tensor& x) { return A.mv(x); };
    auto M_T     = [&](const torch::Tensor& x) { return A.t().mv(x); };

    // ---------------------------------------------------------------- 1. the correct case
    {
        auto r = probe::probe_transpose(M, M_T, N, opts(), SEED);
        check(r.transpose_supplied, "correct: transpose_supplied");
        check(!r.transpose_is_identity(), "correct: NOT flagged severed");
        check(r.threw.empty(), "correct: did not throw");
        check(r.transpose_error < 1e-12,
              "correct: transpose_error " + sci(r.transpose_error) + " ~ 0");
        check(r.forward_is_fixed_linear(), "correct: M reported fixed-linear");
        check(r.symmetry > 1e-3, "correct: fixture is non-symmetric (" + sci(r.symmetry) + ")");
        check(mentions(summary_of(r), "TRANSPOSE VERIFIED"), "correct: summary says VERIFIED");
    }

    // ------------------------------------------------- 2. THE D80 CASE: a severed transpose
    {
        // Exactly what an autograd VJP does when the graph recorded nothing: hand the
        // cotangent straight back. Note it satisfies every forward property.
        auto severed = [&](const torch::Tensor& x) { return x.clone(); };
        auto r = probe::probe_transpose(M, severed, N, opts(), SEED);
        check(r.transpose_is_identity(), "severed: DETECTED as identity");
        check(r.transpose_identity_residual == 0.0,
              "severed: identity_residual is 0");
        check(r.forward_identity_residual > 1e-6,
              "severed: forward is NOT the identity -- what discriminates");
        check(mentions(summary_of(r), "TRANSPOSE SEVERED"), "severed: summary says SEVERED");
        check(!mentions(summary_of(r), "transpose_error="),
              "severed: summary prints NO rel (the D80 misreading)");
        check(r.forward_is_fixed_linear(),
              "severed: forward properties still all pass -- why rel alone cannot catch it");
    }

    // --------------------------- 3. a genuinely-identity M must NOT be called severed
    {
        auto I  = [](const torch::Tensor& x) { return x.clone(); };
        auto r  = probe::probe_transpose(I, I, N, opts(), SEED);
        check(!r.transpose_is_identity(), "identity M: returning w is CORRECT, not severed");
        check(r.transpose_error < 1e-12, "identity M: transpose verified");
        check(mentions(summary_of(r), "SELF-ADJOINT"), "identity M: summary says self-adjoint");
    }

    // ------------------------------------------------ 4. a wrong (non-severed) transpose
    {
        auto r = probe::probe_transpose(M, M, N, opts(), SEED);   // M instead of M^T
        check(!r.transpose_is_identity(), "wrong: not identity");
        check(r.transpose_error > 1e-3,
              "wrong: transpose_error " + sci(r.transpose_error) + " is large");
        check(mentions(summary_of(r), "TRANSPOSE WRONG"), "wrong: summary says WRONG");
    }

    // ------------------------------------------------------- 5. forward-property detection
    {
        auto S   = (A + A.t()) / 2.0;
        auto Msym = [&](const torch::Tensor& x) { return S.mv(x); };
        auto r = probe::probe_transpose(Msym, {}, N, opts(), SEED);
        check(r.symmetry < 1e-12, "symmetric M: symmetry ~ 0");
        check(!r.transpose_supplied, "no transpose supplied: flag is false");
        check(mentions(summary_of(r), "no transpose supplied"), "no transpose: summary says so");
    }
    {
        auto nonlinear = [&](const torch::Tensor& x) { return A.mv(x) + x * x; };
        auto r = probe::probe_transpose(nonlinear, {}, N, opts(), SEED);
        check(r.linearity > 1e-6, "nonlinear M: linearity " + sci(r.linearity) + " flags it");
        check(!r.forward_is_fixed_linear(), "nonlinear M: not fixed-linear");
        check(mentions(summary_of(r), "NOT A FIXED LINEAR OPERATOR"),
              "nonlinear M: summary refuses a transpose");
    }
    {
        double drift = 0.0;
        auto stateful = [&](const torch::Tensor& x) { drift += 1.0; return A.mv(x) + drift; };
        auto r = probe::probe_transpose(stateful, {}, N, opts(), SEED);
        check(r.repeatability > 1e-6, "stateful M: repeatability flags it");
    }

    // ---------------------------------------------------------------- 6. a throwing claim
    {
        auto boom = [](const torch::Tensor&) -> torch::Tensor {
            throw std::runtime_error("no adjoint here");
        };
        auto r = probe::probe_transpose(M, boom, N, opts(), SEED);
        check(!r.threw.empty(), "throwing transpose: captured, not propagated");
        check(mentions(summary_of(r), "TRANSPOSE THREW"), "throwing: summary says THREW");
        check(!r.transpose_is_identity(), "throwing: not misreported as severed");
    }

    // ------------------------------------------------- 7. same seed => same v, w, same numbers
    {
        auto a = probe::probe_transpose(M, M_T, N, opts(), SEED);
        auto b = probe::probe_transpose(M, M_T, N, opts(), SEED);
        check(a.transpose_error == b.transpose_error && a.symmetry == b.symmetry,
              "seeded: two runs agree exactly");
        auto c = probe::probe_transpose(M, M_T, N, opts(), SEED + 1);
        check(c.symmetry != a.symmetry, "different seed: different draw");
    }

    // The float64 fixture's additivity is ~1e-16 and its linearity exactly 0; the tolerance
    // in forward_is_fixed_linear is what makes both "fixed linear", and demanding exact
    // zero here is what this contract caught in the first version of the header.
    {
        auto r = probe::probe_transpose(M, M_T, N, opts(), SEED);
        check(r.additivity > 0.0, "fp reality: additivity " + sci(r.additivity) + " is NOT 0");
        check(r.linearity == 0.0, "fp reality: linearity IS exactly 0 (doubling is exact)");
        check(!r.forward_is_fixed_linear(0.0), "tolerance: exact-zero bar rejects a clean M");
    }

    // -------------------------------- 8. THE PRODUCTION RATIO, reproduced at fixture scale.
    // The real preconditioner returned identity_residual=8.79e-05 against a forward that
    // moved the same vector by 1.21 -- severed, but NOT bit-exactly, which is why the
    // criterion is a ratio and not an absolute bar. Anything under frac*forward counts.
    {
        auto nearly = [&](const torch::Tensor& x) { return x * (1.0 + 7.3e-05); };
        auto r = probe::probe_transpose(M, nearly, N, opts(), SEED);
        check(r.transpose_identity_residual > 1e-6,
              "production ratio: residual " + sci(r.transpose_identity_residual)
              + " EXCEEDS the absolute bar that failed to fire");
        check(r.transpose_is_identity(),
              "production ratio: the RELATIVE criterion still detects it");
        check(r.forward_identity_residual > 100.0 * r.transpose_identity_residual,
              "production ratio: forward moves vectors orders more than the claim does");
    }

    // ------------------------------- 9. NEAR-identity: what production actually returned.
    // The first version of this header demanded BIT equality and did not fire on the real
    // preconditioner, whose severed graph returned w to within rounding rather than exactly.
    {
        auto nearly = [&](const torch::Tensor& x) { return x * (1.0 + 1e-12); };
        auto r = probe::probe_transpose(M, nearly, N, opts(), SEED);
        check(r.transpose_identity_residual > 0.0, "near-identity: residual is NOT 0");
        check(r.transpose_is_identity(), "near-identity: STILL detected at tol=1e-6");
        check(!r.transpose_is_identity(1e-18), "near-identity: a tight frac declines it");
        check(mentions(summary_of(r), "TRANSPOSE SEVERED"), "near-identity: summary SEVERED");
    }

    constexpr int expected_checks = 41;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) { std::cout << "TRANSPOSE_PROBE: PASS" << std::endl; return 0; }
    std::cout << "TRANSPOSE_PROBE: FAIL (" << failures << ")" << std::endl;
    return 1;
}
