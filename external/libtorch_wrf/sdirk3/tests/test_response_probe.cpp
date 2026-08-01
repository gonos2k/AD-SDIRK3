// 9F.D58: the response probe, validated against operators whose Jacobian is KNOWN.
//
// WHY THIS EXISTS. D55/D56 measured response matrices inside the solver and I read two of
// the numbers as physical results. My own adversarial pass then found:
//   - the normalisation divided a COUPLED tendency by an UNCOUPLED scale, inflating three
//     of the six rows by ~mu ~ 1e5
//   - one highlighted entry was nonlinear, so it was an amplitude artefact, not a coupling
//   - the "seeded" control never passed its generator to randn
// None of those are findings about the dynamics. They are defects in the INSTRUMENT, and
// an instrument pointed only at an operator whose answer is unknown cannot reveal them.
//
// So this file points the same instrument at operators whose Jacobian is known exactly:
// if it cannot recover 2.0 from F(U) = 2U, nothing it says about the dynamical core is
// worth reading.

#include "../wrf_sdirk3_response_probe.h"

#include <torch/torch.h>

#include <cmath>
#include <iomanip>
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

using wrf::sdirk3::probe::ChannelSpec;
using wrf::sdirk3::probe::ProbeSpec;
using wrf::sdirk3::probe::measure_response;

constexpr int64_t NA = 16, NB = 16, N = NA + NB;

// Two channels, deliberately given DIFFERENT state and tendency scales, because that is
// the asymmetry the real solver has and the one D55/D56 collapsed.
ProbeSpec two_channel(double a_state, double a_tend, double b_state, double b_tend) {
    ProbeSpec s;
    s.channels.push_back(ChannelSpec{"a", 0,  NA, a_state, a_tend});
    s.channels.push_back(ChannelSpec{"b", NA, NB, b_state, b_tend});
    s.amplitudes = {0.25, 0.5, 1.0};
    s.seed = 12345;
    s.dt = 1.0;
    return s;
}

// Returns gain_rms: the draw-independent bulk gain. max_abs is still reported by the
// instrument, but it is a worst-cell statistic and makes a poor headline -- see the
// outlier case at the end.
double entry(const wrf::sdirk3::probe::ResponseResult& r,
             const std::string& kicked, double amp, int responding) {
    for (const auto& row : r.rows)
        if (row.kicked == kicked && std::abs(row.amplitude - amp) < 1e-12)
            return row.gain_rms[responding];
    return -1.0;
}

}  // namespace

int main() {
    auto opts = torch::TensorOptions().dtype(torch::kFloat64);
    auto U = torch::zeros({N}, opts);

    // --- a LINEAR operator: the probe must recover its gain exactly ---
    // F(U) = 2U. With state_scale = tendency_scale = 1, a kick of amplitude a produces a
    // normalised max response of exactly 2a, so max_abs/a = 2 at every rung.
    {
        auto F = [](const torch::Tensor& x) { return 2.0 * x; };
        auto spec = two_channel(1.0, 1.0, 1.0, 1.0);
        auto r = measure_response(F, U, spec);
        bool ok = true;
        for (double a : spec.amplitudes) {
            const double got = entry(r, "a", a, 0);
            if (std::abs(got - 2.0) > 1e-9) ok = false;
        }
        check(ok, "F(U)=2U is recovered as gain 2.0 at every amplitude");
        check(r.linearity_spread[0][0] < 1e-12,
              "and the linearity spread is ~0 (" + sci(r.linearity_spread[0][0]) + ")");
    }

    // --- SCALES: the instrument must honour state and tendency scales SEPARATELY ---
    // This is the check that would have caught the D55/D56 defect. F(U)=2U again, but the
    // 'a' channel now has state_scale 10 and tendency_scale 1000. The kick is 10x bigger
    // and the response is divided by 1000, so the reported gain must be 2 * 10/1000 = 0.02.
    // If the two scales were collapsed into one, this would report 2.0 and look correct.
    {
        auto F = [](const torch::Tensor& x) { return 2.0 * x; };
        auto spec = two_channel(10.0, 1000.0, 1.0, 1.0);
        auto r = measure_response(F, U, spec);
        const double got = entry(r, "a", 1.0, 0);
        check(std::abs(got - 0.02) < 1e-9,
              "state_scale and tendency_scale are applied INDEPENDENTLY (got " + sci(got) +
              ", want 2e-02); collapsing them would report 2.0 and look right");
    }

    // --- CROSS-CHANNEL: an off-diagonal coupling is attributed to the right pair ---
    // F_b = 3 * a, F_a = 0. Kicking 'a' must move 'b' and nothing else; kicking 'b' must
    // move nothing. An instrument that mixes up offsets passes the diagonal test and
    // fails this one.
    {
        auto F = [&](const torch::Tensor& x) {
            auto y = torch::zeros_like(x);
            y.narrow(0, NA, NB).copy_(3.0 * x.narrow(0, 0, NA));
            return y;
        };
        auto spec = two_channel(1.0, 1.0, 1.0, 1.0);
        auto r = measure_response(F, U, spec);
        check(std::abs(entry(r, "a", 1.0, 1) - 3.0) < 1e-9, "kick a -> b reports gain 3.0");
        check(entry(r, "a", 1.0, 0) == 0.0, "kick a -> a reports exactly 0");
        check(entry(r, "b", 1.0, 0) == 0.0 && entry(r, "b", 1.0, 1) == 0.0,
              "kick b moves nothing, so the coupling is attributed to the right pair");
    }

    // --- NONLINEARITY: the ladder must detect it, which is how w->w was caught ---
    // F(U) = U^3, at a ZERO base. A quadratic will not do here: centrally differenced at
    // U=0 it gives exactly 0, which is the CORRECT tangent, so the ladder rightly calls it
    // linear. That is worth knowing -- central differencing already removes the leading
    // nonlinear artefact, and the ladder is catching what survives it. The cubic's
    // normalised response grows as a^2 and cannot hide.
    {
        auto F = [](const torch::Tensor& x) { return x * x * x; };
        auto spec = two_channel(1.0, 1.0, 1.0, 1.0);
        auto r = measure_response(F, U, spec);
        check(r.linearity_spread[0][0] > 0.5,
              "F(U)=U^3 is flagged NONLINEAR by the ladder (spread " +
              sci(r.linearity_spread[0][0]) + "), which a single amplitude cannot see");
    }

    // --- CENTRAL differencing must beat one-sided on a quadratic background ---
    // Around a non-zero base, F(U)=U^2 has J = 2U and a Hessian term that the one-sided
    // difference keeps at O(a) and the central difference cancels. This is why the review
    // asked for central differences, and it is checkable rather than assertable.
    {
        auto F = [](const torch::Tensor& x) { return x * x; };
        auto Ub = torch::full({N}, 5.0, opts);
        auto spec = two_channel(1.0, 1.0, 1.0, 1.0);
        spec.amplitudes = {1.0};

        auto rc = measure_response(F, Ub, spec);            // central
        spec.central = false;
        auto ro = measure_response(F, Ub, spec);            // one-sided

        // The exact gain needs no draw at all, which is the payoff of the unit-RMS
        // direction: for F=U^2 the Jacobian is 2U, and gain_rms of a unit-RMS direction
        // through a scalar multiple IS that multiple. At U=5 the answer is exactly 10.
        const double exact = 2.0 * 5.0;

        const double ec = std::abs(entry(rc, "a", 1.0, 0) - exact) / exact;
        const double eo = std::abs(entry(ro, "a", 1.0, 0) - exact) / exact;
        check(ec < eo,
              "central differencing is closer to the exact tangent than one-sided "
              "(central rel " + sci(ec) + " vs one-sided rel " + sci(eo) + ")");
        check(ec < 1e-12,
              "and central is EXACT for a quadratic, since the Hessian term cancels (rel " +
              sci(ec) + ")");
    }

    // --- the SEED must actually seed: two runs agree, a different seed does not ---
    // D54's control created a generator and then called the global torch::randn, so its
    // numbers were not reproducible from the stated seed at all.
    {
        auto F = [](const torch::Tensor& x) { return 2.0 * x; };
        auto s1 = two_channel(1.0, 1.0, 1.0, 1.0);
        auto r1 = measure_response(F, U, s1);
        auto r2 = measure_response(F, U, s1);
        auto s3 = s1; s3.seed = 999;
        auto r3 = measure_response(F, U, s3);
        // gain is seed-independent for a linear operator, so compare the worst-cell INDEX,
        // which is a property of the random direction itself.
        check(r1.rows[0].argmax[0] == r2.rows[0].argmax[0],
              "the same seed reproduces the same direction (argmax matches)");
        check(r1.rows[0].argmax[0] != r3.rows[0].argmax[0],
              "and a different seed does not -- so the seed is genuinely threaded into "
              "randn, which D54's was not");
    }

    // --- max, rms and p99 must be reported together, and must differ on an outlier ---
    // A single bad cell dominates max and is invisible in rms. D55/D56 reported max alone,
    // so nothing distinguished "one cell" from "the whole field".
    {
        auto F = [&](const torch::Tensor& x) {
            auto y = x.clone();
            y.index_put_({3}, y.index({3}) * 1000.0);   // one outlier cell
            return y;
        };
        auto spec = two_channel(1.0, 1.0, 1.0, 1.0);
        spec.amplitudes = {1.0};
        auto r = measure_response(F, U, spec);
        const double mx = r.rows[0].max_abs[0], rms = r.rows[0].rms[0];
        // One cell in NA amplified: max scales with that cell, rms with its share of the
        // mean square, so the ratio approaches sqrt(NA) = 4 rather than the amplification
        // factor. That ceiling is itself the point -- max/rms tells you the response is
        // CONCENTRATED, not how badly.
        check(mx > 3.0 * rms,
              "a single outlier cell drives max well above rms (" + sci(mx) + " vs " +
              sci(rms) + ", ratio ~sqrt(N)), so max alone cannot distinguish one cell "
              "from the field");
        check(r.rows[0].argmax[0] == 3,
              "and argmax locates it exactly (index " + std::to_string(r.rows[0].argmax[0]) + ")");
    }

    // === LEADING SINGULAR AMPLIFICATION, on a matrix whose answer is known ===
    // The fixture is the point: A = [[1, 100], [0, 1]] repeated blockwise. EVERY eigenvalue
    // is 1, and sigma_max = 100.0100 -- a two-order gap between what the spectrum says and
    // what the operator actually does in one application. That gap is precisely why the
    // review asks for singular values rather than eigenvalues on a non-normal system, and
    // an instrument that cannot reproduce it here cannot be trusted to find it in the core.
    {
        using wrf::sdirk3::probe::power_iterate_sigma_max;
        // block-diagonal copies of [[1,100],[0,1]] acting on (a_i, b_i) pairs
        auto A  = [&](const torch::Tensor& x) {
            auto y = x.clone();
            y.narrow(0, 0, NA).add_(100.0 * x.narrow(0, NA, NB));
            return y;
        };
        auto At = [&](const torch::Tensor& x) {           // exact transpose
            auto y = x.clone();
            y.narrow(0, NA, NB).add_(100.0 * x.narrow(0, 0, NA));
            return y;
        };
        auto spec = two_channel(1.0, 1.0, 1.0, 1.0);
        spec.seed = 7;
        auto r = power_iterate_sigma_max(A, At, U, spec, 200, 1e-12);

        const double exact = 0.5 * (std::sqrt(100.0 * 100.0 + 4.0) + 100.0);  // ~100.0100
        const double rel = std::abs(r.sigma_max - exact) / exact;
        check(rel < 1e-6, "sigma_max of a NON-NORMAL block (all eigenvalues 1) is recovered "
                          "as " + sci(r.sigma_max) + " vs exact " + sci(exact) +
                          " (rel " + sci(rel) + ")");
        check(r.sigma_max > 50.0,
              "and it is ~100x the spectral radius of 1, which is the entire reason "
              "eigenvalues cannot be used here");
        check(r.converged, "the power iteration converged rather than hitting the cap");
        // the amplifying direction lives in channel b: A maps b into a with gain 100
        check(r.block_weight[1] > 0.9,
              "the leading right singular vector is concentrated in the channel that DRIVES "
              "the amplification (b weight " + sci(r.block_weight[1]) + ")");
    }

    // --- scaling must move sigma_max the way the algebra says ---
    // Halving the driving channel's state_scale halves its contribution, so sigma_max must
    // halve too. Without this, a wrong scale would silently rescale the headline number.
    {
        using wrf::sdirk3::probe::power_iterate_sigma_max;
        auto A  = [&](const torch::Tensor& x) {
            auto y = x.clone(); y.narrow(0, 0, NA).add_(100.0 * x.narrow(0, NA, NB)); return y; };
        auto At = [&](const torch::Tensor& x) {
            auto y = x.clone(); y.narrow(0, NA, NB).add_(100.0 * x.narrow(0, 0, NA)); return y; };
        auto s1 = two_channel(1.0, 1.0, 1.0, 1.0); s1.seed = 7;
        auto s2 = two_channel(1.0, 1.0, 0.5, 1.0); s2.seed = 7;   // b state_scale halved
        const double a = power_iterate_sigma_max(A, At, U, s1, 200, 1e-12).sigma_max;
        const double b = power_iterate_sigma_max(A, At, U, s2, 200, 1e-12).sigma_max;
        check(std::abs(b / a - 0.5) < 1e-3,
              "halving the driving channel's state_scale halves sigma_max (" + sci(b / a) +
              "), so the scaling enters where it is meant to");
    }

    constexpr int expected_checks = 18;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) { std::cout << "RESPONSE_PROBE: PASS" << std::endl; return 0; }
    std::cout << "RESPONSE_PROBE: FAIL (" << failures << ")" << std::endl;
    return 1;
}
