// R13 C1: the forward integrates one model and the adjoint differentiates another.
//
// THE FACT. imex_slow_in_tangent is 0 at runtime (R12 W1, confirmed from the config echo), so
// compute_k_slow detaches k_slow. The forward advances with F_I + F_E; the graph the adjoint
// walks is F_I + detach(F_E). For a cost function
//
//     J(x0) = 1/2 |x0-xb|_B^-1^2 + 1/2 sum_k |H_k Phi_0k(x0) - y_k|_R^-1^2
//
// the gradient needs D(Phi_0k)^T exactly. A gradient taken with the slow channel detached is
// not an approximation to it -- it is the exact gradient of a different function. That is
// admissible as a weak-constraint formulation, but only once the dropped component is carried
// in an explicit Q_k with a control variable; a bare detach is not that.
//
// THE SHARP EDGE THIS FILE EXISTS FOR. A finite-difference JVP CANNOT SEE A DETACH. FD
// perturbs the input and evaluates the primal, and detach() changes no primal value -- so an
// FD quotient of the operational function returns the FULL tangent, differing from the true
// operational tangent by the entire dropped channel. So when the forward-mode helper falls
// back to FD on this function it does not return a noisier answer, it returns a DIFFERENT
// OPERATOR, silently. That is why fd_fallback voids a verdict rather than annotating it, and
// it is measured here rather than asserted.

#include "../wrf_sdirk3_jvp_fwad_or_fd.h"
#include "../wrf_sdirk3_probe_validity.h"

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

double nrm(const torch::Tensor& t) {
    torch::NoGradGuard ng;
    return t.detach().to(torch::kFloat64).norm().item<double>();
}

double rel(const torch::Tensor& a, const torch::Tensor& b) {
    torch::NoGradGuard ng;
    const double n = nrm(a);
    return n > 0.0 ? (a.detach().to(torch::kFloat64) -
                      b.detach().to(torch::kFloat64)).norm().item<double>() / n : -1.0;
}

}  // namespace

int main() {
    torch::manual_seed(20260822);
    const auto f64 = torch::TensorOptions().dtype(torch::kFloat64);
    const int n = 24;

    // A stand-in for the two channels: both linear, so J_I v == A v and J_E v == B v exactly
    // and there is no discretization tolerance to negotiate about.
    auto A = torch::randn({n, n}, f64);
    auto B = torch::randn({n, n}, f64) * 0.25;   // the slow channel, deliberately smaller
    auto u = torch::randn({n}, f64);
    auto v = torch::randn({n}, f64);
    v = v / v.norm();

    auto F_implicit = [&](const torch::Tensor& x) { return torch::mv(A, x); };
    auto F_explicit = [&](const torch::Tensor& x) { return torch::mv(B, x); };
    auto F_primal = [&](const torch::Tensor& x) { return F_implicit(x) + F_explicit(x); };
    // Production's graph, at imex_slow_in_tangent = 0.
    auto F_operational = [&](const torch::Tensor& x) {
        return F_implicit(x) + F_explicit(x).detach();
    };

    bool fb_primal = false, fb_op = false;
    const auto Jv_primal = wrf::sdirk3::compute_jvp_fwad_or_fd(F_primal, u, v, 0, 0.0f,
                                                               &fb_primal);
    const auto Jv_op = wrf::sdirk3::compute_jvp_fwad_or_fd(F_operational, u, v, 0, 0.0f,
                                                           &fb_op);
    check(!fb_primal && !fb_op,
          "both tangents came from forward-mode duals, not from a fallback");

    const auto want_primal = torch::mv(A + B, v);
    const auto want_op = torch::mv(A, v);
    check(rel(Jv_primal, want_primal) < 1e-12,
          "the primal tangent is (A+B)v (rel=" + sci(rel(Jv_primal, want_primal)) + ")");
    check(rel(Jv_op, want_op) < 1e-12,
          "the operational tangent is A v -- detach() really severs the slow channel "
          "(rel=" + sci(rel(Jv_op, want_op)) + ")");

    const double e_drop = (nrm(Jv_primal) - 0.0) > 0.0
        ? nrm(Jv_primal - Jv_op) / nrm(Jv_primal) : -1.0;
    check(e_drop > 1e-3,
          "e_drop is NONZERO by construction (" + sci(e_drop) + "): the operational tangent "
          "is not the primal derivative, so this configuration cannot carry an exact 4D-Var "
          "gradient");
    check(std::abs(nrm(Jv_primal - Jv_op) - nrm(torch::mv(B, v))) < 1e-12 * nrm(Jv_primal),
          "and what it drops is EXACTLY the slow channel's tangent B v");

    // THE FD TRAP. Same function, finite differences.
    const double eps = 1e-6;
    const auto fd_op = (F_operational(u + eps * v) - F_operational(u - eps * v)) / (2.0 * eps);
    check(rel(fd_op, want_primal) < 1e-6,
          "an FD quotient of the OPERATIONAL function returns the PRIMAL tangent (A+B)v -- FD "
          "evaluates primal values and detach() changes none of them");
    check(rel(fd_op, want_op) > 1e-3,
          "so an FD fallback here does not degrade the operator, it REPLACES it: the error is "
          "the whole dropped channel (rel to the true operational tangent = " +
          sci(rel(fd_op, want_op)) + ")");

    // Which is why the verdict rule treats a fallback as disqualifying rather than as noise.
    {
        wrf::sdirk3::TangentInputs in;
        in.fd_fallback = true;
        check(!wrf::sdirk3::tangent_verdict(in).valid,
              "the verdict rule voids a tangent that fell back to FD");
        check(wrf::sdirk3::tangent_semantics_name(
                  wrf::sdirk3::TangentSemantics::OperationalDetachedSlow) ==
                  std::string("operational_detached_slow"),
              "and the record names WHICH function was linearized, which no config echo "
              "recovers after the fact");
    }

    // Cancellation: a norm ratio is not an additive share. Constructed so the two channels
    // oppose and ||J_E v|| / ||J_full v|| exceeds 1 -- the reading "fraction of the tangent"
    // would report more than all of it.
    {
        auto Bopp = -A * 0.9;
        auto full = torch::mv(A + Bopp, v);
        auto slow = torch::mv(Bopp, v);
        const double ratio = nrm(slow) / nrm(full);
        check(ratio > 1.0,
              "with opposing channels the slow/full norm RATIO is " + sci(ratio) + " > 1 -- "
              "which is why it is not named a fraction, and why cos_EI belongs beside it");
    }

    constexpr int expected_checks = 10;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) {
        std::cout << "OPERATIONAL_PRIMAL_TANGENT_CONTRACT: PASS" << std::endl;
        return 0;
    }
    std::cout << "OPERATIONAL_PRIMAL_TANGENT_CONTRACT: FAIL (" << failures << ")" << std::endl;
    return 1;
}
