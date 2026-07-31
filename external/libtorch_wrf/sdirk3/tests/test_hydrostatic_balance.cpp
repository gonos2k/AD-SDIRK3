// 9F.D48: discrete hydrostatic balance contract (review section 3).
//
// WHY THIS EXISTS. Every base-state field had pointer, shape and layout safety enforced,
// but nothing checked that they TOGETHER satisfy the model's discrete vertical balance.
// WRF does not merely make a continuous relation hold; ideal_init_method=2 integrates phb
// directly from alb so that
//     R_H,k = rdnw_k*(phb_{k+1} - phb_k) + alb_k*(c1h_k*mub + c2h_k)
// vanishes for the DISCRETE operator.
//
// MEASURED FIRST, on em_b_wave/wrfinput_d01 with WRF's own fields:
//     WRF signed rdnw (-201.3 .. -28.1):  mean|R| / mean|term| = 9.5e-07  <- float32 eps
//     |rdnw| (the C++ magnitude convention): 2.000                       <- terms ADD
// That 2.000 is the load-bearing observation. A wrong eta orientation does not degrade
// the balance slightly -- it makes the two terms add rather than cancel, so the residual
// is exactly twice the term. The negative control below asserts that, which turns the
// vertical-metric contract from a comment into a test.
//
// Self-contained: the base state is CONSTRUCTED in exact balance by inverting R_H = 0,
// the same way WRF builds it, so this needs no wrfinput and runs in CI. Torch-free.

#include "../wrf_sdirk3_hydrostatic_balance.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;
int check_count = 0;

void check(bool ok, const std::string& what) {
    ++check_count;
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;
    if (!ok) ++failures;
}

// A dry b_wave-like column: 64 mass levels, znw decreasing 1 -> 0 exactly as WRF builds
// it, so dnw < 0 and rdnw < 0. Those signs are the whole point of the exercise.
template <typename T>
struct Column {
    std::vector<T> rdnw, alb, c1h, c2h, phb;
    T mub;
};

template <typename T>
Column<T> make_balanced_column(int nk = 64) {
    Column<T> c;
    c.mub = T(1.0e5);                       // ~1000 hPa column mass
    c.rdnw.resize(nk); c.alb.resize(nk);
    c.c1h.assign(nk, T(1));                 // dry mass coordinate: c1h=1, c2h=0
    c.c2h.assign(nk, T(0));

    for (int k = 0; k < nk; ++k) {
        // znw DECREASES 1 -> 0 with k (WRF): dnw = znw[k+1]-znw[k] < 0, so rdnw < 0.
        const T znw_k  = T(1) - T(k)     / T(nk);
        const T znw_k1 = T(1) - T(k + 1) / T(nk);
        const T dnw    = znw_k1 - znw_k;    // negative
        c.rdnw[k] = T(1) / dnw;             // negative
        // A plausible alb profile: increases upward (density falls). Magnitude and shape
        // do not matter for the identity -- only that phb is integrated FROM it.
        const T eta_mid = T(0.5) * (znw_k + znw_k1);
        c.alb[k] = T(0.8) / std::pow(eta_mid, T(0.7));
    }
    // phb by construction: this is what makes the residual vanish.
    c.phb = wrf::sdirk3::integrate_phb_hydrostatic<T>(
        c.rdnw, c.alb, c.c1h, c.c2h, c.mub, /*phb_surface=*/T(0));
    return c;
}

}  // namespace

int main() {
    using wrf::sdirk3::hydrostatic_residual;

    // --- double precision: the identity itself, with no float noise to hide behind ---
    {
        const auto c = make_balanced_column<double>();
        const auto R = hydrostatic_residual<double>(c.rdnw, c.phb, c.alb, c.c1h, c.c2h, c.mub);
        check(R.relative < 1e-14,
              "balanced column: relative residual at double eps (" +
              std::to_string(R.relative) + ")");
        check(R.mean_term > 1e4, "term magnitude is O(1e4-1e5), so the ratio is meaningful");
        check(c.phb.back() > c.phb.front(),
              "geopotential INCREASES upward (rdnw<0 and alb>0 give a positive increment)");
    }

    // --- float precision: what the model actually computes ---
    // The reference measurement on WRF's own state was 9.5e-07. A float32 bound of 1e-5
    // is loose enough to absorb the upward phb accumulation and tight enough that the
    // sign error below (2.0) cannot slip through by three orders of magnitude.
    {
        const auto c = make_balanced_column<float>();
        const auto R = hydrostatic_residual<float>(c.rdnw, c.phb, c.alb, c.c1h, c.c2h, c.mub);
        check(R.relative < 1e-5f,
              "balanced column in float32: relative residual < 1e-5 (" +
              std::to_string(R.relative) + "); WRF's own state measures 9.5e-07");
    }

    // --- THE NEGATIVE CONTROL: |rdnw| must BREAK the balance, and break it by 2x ---
    // This is the vertical-metric contract as an assertion. C++ stores |rdnw| and every
    // operator must re-apply the eta sign; if one forgets, this is the signature it
    // produces. A stale comment in this repo once read "all vertical derivatives use
    // positive rdnw/rdn directly" -- believing it yields exactly this failure.
    {
        const auto c = make_balanced_column<double>();
        std::vector<double> rdnw_abs(c.rdnw.size());
        for (std::size_t k = 0; k < c.rdnw.size(); ++k) rdnw_abs[k] = std::abs(c.rdnw[k]);
        const auto R = hydrostatic_residual<double>(rdnw_abs, c.phb, c.alb, c.c1h, c.c2h, c.mub);
        check(R.relative > 1.0,
              "|rdnw| BREAKS the balance (relative = " + std::to_string(R.relative) + ")");
        check(std::abs(R.relative - 2.0) < 1e-9,
              "and breaks it by EXACTLY 2x -- the terms add instead of cancelling, which "
              "is the signature of a sign error rather than a discretisation error");
    }

    // --- a perturbed alb must also break it, so the test is not merely sign-sensitive ---
    // Without this, a residual that only ever responds to the sign flip could be passing
    // for the wrong reason.
    {
        auto c = make_balanced_column<double>();
        c.alb[c.alb.size() / 2] *= 1.01;     // 1% error in ONE level
        const auto R = hydrostatic_residual<double>(c.rdnw, c.phb, c.alb, c.c1h, c.c2h, c.mub);
        // Judge on max|R|/mean_term, NOT the mean. A single bad level out of 64 is
        // diluted ~64x in the mean: measured 9.1e-5, which sits under a naive 1e-4 bound
        // and would have read as "not detected". The max is the sensitive statistic, and
        // it is why the review asked for per-level and max rather than a global norm.
        const double peak_rel = R.max_abs / R.mean_term;
        check(peak_rel > 1e-3,
              "a 1% alb error in ONE level is detected on max|R|/term (" +
              std::to_string(peak_rel) + "), where the mean dilutes it to " +
              std::to_string(R.relative));
        // and it is LOCALISED to the level that was perturbed
        const std::size_t kbad = c.alb.size() / 2;
        double worst = 0.0; std::size_t kworst = 0;
        for (std::size_t k = 0; k < R.per_level.size(); ++k) {
            if (std::abs(R.per_level[k]) > worst) { worst = std::abs(R.per_level[k]); kworst = k; }
        }
        check(kworst == kbad,
              "and the residual peaks at exactly the perturbed level (k=" +
              std::to_string(kworst) + ")");
    }

    // --- the residual must be RELATIVE, because absolute grows with height ---
    // phb is integrated upward, so roundoff accumulates: on WRF's state |R| went 0 at
    // k=0 to ~0.7 at k=63 while terms grew 7.3e4 -> 5.4e5. An absolute bound would
    // spuriously fail at the model top; this pins the reason.
    {
        const auto c = make_balanced_column<float>();
        const auto R = hydrostatic_residual<float>(c.rdnw, c.phb, c.alb, c.c1h, c.c2h, c.mub);
        const std::size_t nk = R.per_level.size();
        check(std::abs(R.per_level[0]) <= std::abs(R.per_level[nk - 1]),
              "absolute residual does not shrink with height (accumulation is expected)");
        check(R.relative < 1e-5f,
              "but the RELATIVE residual stays small, which is why it is the contract");
    }

    // --- a DEGENERATE scale must not read as perfect balance (review section 7) ---
    // mean_term == 0 with a non-zero residual is alb == 0 or mub == 0: not an atmosphere.
    // The helper used to return relative = 0 for it, i.e. the best possible score for the
    // worst possible input. Same class as substituting eps for a broken metric -- invalid
    // input disguised as a good number.
    {
        auto c = make_balanced_column<double>();
        std::vector<double> alb_zero(c.alb.size(), 0.0);          // no scale left
        const auto R = hydrostatic_residual<double>(c.rdnw, c.phb, alb_zero, c.c1h, c.c2h, c.mub);
        check(std::isinf(R.relative) && R.relative > 0.0,
              "alb == 0 with a non-zero residual gives +infinity, not 0");
        check(R.mean_abs > 0.0, "and the residual really is non-zero, so the case is real");

        // the genuinely trivial case still reports 0
        std::vector<double> phb_flat(c.phb.size(), 0.0);
        const auto R0 = hydrostatic_residual<double>(c.rdnw, phb_flat, alb_zero, c.c1h, c.c2h, c.mub);
        check(R0.relative == 0.0,
              "but 0/0 -- nothing to balance and nothing unbalanced -- still reports 0");
    }

    constexpr int expected_checks = 13;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) { std::cout << "HYDROSTATIC_BALANCE: PASS" << std::endl; return 0; }
    std::cout << "HYDROSTATIC_BALANCE: FAIL (" << failures << ")" << std::endl;
    return 1;
}
