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

    constexpr int expected_checks = 5;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) { std::cout << "ADJOINT_SOLVE_AUTHORITY: PASS" << std::endl; return 0; }
    std::cout << "ADJOINT_SOLVE_AUTHORITY: FAIL (" << failures << ")" << std::endl;
    return 1;
}
