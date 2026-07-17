// PR 9C commit 1: independent WRF W-damping reference contract.
//
// The reference below is a LINE-BY-LINE scalar transcription of WRF's W_DAMP
// (dyn_em/module_big_step_utilities_em.f90) — deliberately NOT reusing the
// production helper, which is the object under test:
//
//   vert_cfl  = abs( ww(i,k,j) / (c1f(k)*mut(i,j)+c2f(k)) * rdnw(k) * dt )
//   w_damp_on = (ieva > 0) ? w_crit_cfl : w_beta        ! GATE threshold
//   IF ((vert_cfl .gt. w_damp_on) .and. (w_damping == 1)) THEN
//     rw_tend -= SIGN(1.,w) * w_alpha * (vert_cfl - w_crit_cfl)
//                * (c1f(k)*mut(i,j)+c2f(k))              ! EXCESS offset
//
// Pinned semantics (each has a fixture case):
//  - the CFL is built from the MASS-DECOUPLED vertical motion ww/mass
//    (ww = omega-coupled vertical flux), NOT from a raw momentum or from the
//    physical w; the solver state w block carries PHYSICAL w (the Fortran
//    bridge passes grid%w_2), so the production term needs ww separately;
//  - the GATE threshold (w_beta, or w_crit_cfl when zadvect_implicit/ieva>0)
//    and the EXCESS offset (w_crit_cfl) are DIFFERENT constants;
//  - the sign comes from WRF's PHYSICAL w via Fortran SIGN(1.,w):
//    SIGN(1.,+0)=+1 and SIGN(1.,-0)=-1 (signbit semantics, unlike
//    torch::sign which returns 0 at 0);
//  - strict '>' at the gate: vert_cfl == threshold produces exactly zero;
//  - the returned value is the amount SUBTRACTED from rw_tend;
//  - damping applies on interior k only (Fortran DO k=2,kde-1); boundary
//    levels stay zero.
//
// NEGATIVE CONTROL (parity deviation pin): the CURRENT production helper
// (compute_w_damping_term) is asserted to DISAGREE with this reference on a
// below-threshold fixture — it is ungated (negative excess applies) and
// builds its CFL from the raw w-block without mass decoupling. Commit 3
// flips this case to equality when production parity lands.
#include <torch/torch.h>
#include <cmath>
#include <cstdio>
#include <optional>
#include "wrf_sdirk3_w_damping.h"

namespace {

int g_cases = 0;
int g_failures = 0;
void check(bool ok, const char* what) {
    ++g_cases;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

// Fortran SIGN(1., w). Zero semantics pinned by a compiler ORACLE, not
// assumption: gfortran (the WRF build compiler, configure.wrf SFC=gfortran)
// gives SIGN(1.,+0.0)=+1.0 and SIGN(1.,-0.0)=-1.0 (measured 2026-07-17,
// sign_oracle.f90) — i.e. signbit semantics, NOT `w>=0 ? 1 : -1` (which
// returns +1 for -0.0) and NOT torch::sign (which returns 0 at 0).
float fortran_sign1(float w) { return std::signbit(w) ? -1.0f : 1.0f; }

// Scalar reference: the amount SUBTRACTED from rw_tend at one point.
// nullopt = invalid input (nonfinite or nonpositive mass) — fail-close.
std::optional<float> wrf_wdamp_reference(float ww, float w_phys, float mut,
                                         float c1f, float c2f, float rdnw,
                                         float dt, float w_alpha,
                                         float w_crit_cfl, float w_beta,
                                         int ieva, bool w_damping_enabled) {
    const float mass = c1f * mut + c2f;
    if (!std::isfinite(mass) || !(mass > 0.0f) || !std::isfinite(ww) ||
        !std::isfinite(w_phys)) {
        return std::nullopt;
    }
    const float vert_cfl = std::fabs(ww / mass * rdnw * dt);
    const float w_damp_on = (ieva > 0) ? w_crit_cfl : w_beta;
    if (!(w_damping_enabled && vert_cfl > w_damp_on)) return 0.0f;
    return fortran_sign1(w_phys) * w_alpha * (vert_cfl - w_crit_cfl) * mass;
}

}  // namespace

int main() {
    torch::NoGradGuard no_grad;
    // Representative WRF-scale constants.
    const float dt = 600.0f, alpha = 0.3f, crit = 2.0f, beta = 1.0f;
    const float c1f = 0.9f, c2f = 100.0f, rdnw = 64.0f;
    const float mut = 95000.0f;
    const float mass = c1f * mut + c2f;

    // Helper: the ww that produces a given vert_cfl for these constants.
    auto ww_for_cfl = [&](float cfl) { return cfl * mass / (rdnw * dt); };

    // (1) below the gate -> exactly zero (both gate variants).
    {
        auto r = wrf_wdamp_reference(ww_for_cfl(0.5f), 1.0f, mut, c1f, c2f,
                                     rdnw, dt, alpha, crit, beta, 0, true);
        check(r.has_value() && *r == 0.0f, "below gate (ieva=0): exactly zero");
        r = wrf_wdamp_reference(ww_for_cfl(1.5f), 1.0f, mut, c1f, c2f, rdnw,
                                dt, alpha, crit, beta, 1, true);
        check(r.has_value() && *r == 0.0f,
              "below gate (ieva>0, gate=crit): exactly zero");
    }

    // (2) exactly AT the gate -> exactly zero (strict >).
    {
        auto r = wrf_wdamp_reference(ww_for_cfl(beta), 1.0f, mut, c1f, c2f,
                                     rdnw, dt, alpha, crit, beta, 0, true);
        check(r.has_value() && *r == 0.0f, "vert_cfl == gate: exactly zero");
    }

    // (3) above the gate -> hand-computed closed form, both signs of w.
    //     Note ieva=0: gate at beta=1 opens BELOW the excess offset crit=2,
    //     so a 1<cfl<2 point damps with NEGATIVE excess — WRF semantics kept
    //     verbatim (gate and offset are different constants).
    {
        const float cfl = 3.0f;
        auto r = wrf_wdamp_reference(ww_for_cfl(cfl), 2.5f, mut, c1f, c2f,
                                     rdnw, dt, alpha, crit, beta, 0, true);
        const float expect = 1.0f * alpha * (cfl - crit) * mass;
        check(r.has_value() && std::fabs(*r - expect) <= 1e-3f * expect,
              "above gate, w>0: matches closed form");
        r = wrf_wdamp_reference(ww_for_cfl(cfl), -2.5f, mut, c1f, c2f, rdnw,
                                dt, alpha, crit, beta, 0, true);
        check(r.has_value() && std::fabs(*r + expect) <= 1e-3f * expect,
              "above gate, w<0: sign flips with PHYSICAL w");
        // between gate(beta=1) and offset(crit=2): active with negative excess
        const float cfl_mid = 1.5f;
        r = wrf_wdamp_reference(ww_for_cfl(cfl_mid), 1.0f, mut, c1f, c2f,
                                rdnw, dt, alpha, crit, beta, 0, true);
        const float expect_mid = alpha * (cfl_mid - crit) * mass;  // negative
        check(r.has_value() && std::fabs(*r - expect_mid) <=
                                   1e-3f * std::fabs(expect_mid),
              "gate<cfl<offset: WRF applies negative excess verbatim");
    }

    // (4) Fortran SIGN zero semantics: SIGN(1.,+0)=+1, SIGN(1.,-0)=-1.
    {
        check(fortran_sign1(+0.0f) == 1.0f, "SIGN(1.,+0) = +1");
        check(fortran_sign1(-0.0f) == -1.0f, "SIGN(1.,-0) = -1");
        // torch::sign(0)=0 would zero the damping — pinned as WRONG here.
        check(torch::sign(torch::zeros({1})).item<float>() == 0.0f,
              "torch::sign(0)==0 (why raw torch::sign must not be used)");
    }

    // (5) mass-scaling invariant: ww and mass scaled by lambda -> the
    //     physical velocity and CFL are unchanged; the momentum damping
    //     scales exactly by lambda (through the trailing mass factor).
    {
        const float lam = 3.7f;
        const float cfl = 4.0f;
        auto r1 = wrf_wdamp_reference(ww_for_cfl(cfl), 1.0f, mut, c1f, c2f,
                                      rdnw, dt, alpha, crit, beta, 0, true);
        // scale: mut' such that mass' = lam*mass requires c1f*mut' + c2f = lam*mass
        const float mut2 = (lam * mass - c2f) / c1f;
        auto r2 = wrf_wdamp_reference(lam * ww_for_cfl(cfl), 1.0f, mut2, c1f,
                                      c2f, rdnw, dt, alpha, crit, beta, 0,
                                      true);
        check(r1.has_value() && r2.has_value() &&
                  std::fabs(*r2 - lam * *r1) <= 1e-3f * std::fabs(lam * *r1),
              "lambda-scaled ww & mass: CFL invariant, damping scales by lambda");
    }

    // (6) disabled w_damping -> exactly zero even far above the gate.
    {
        auto r = wrf_wdamp_reference(ww_for_cfl(10.0f), 1.0f, mut, c1f, c2f,
                                     rdnw, dt, alpha, crit, beta, 0, false);
        check(r.has_value() && *r == 0.0f, "w_damping disabled: exactly zero");
    }

    // (7) nonfinite / nonpositive mass -> fail-close (no value).
    {
        check(!wrf_wdamp_reference(1.0f, 1.0f, mut, c1f, c2f, rdnw, dt, alpha,
                                   crit, beta, 0, true)
                   .has_value() == false,
              "sanity: valid input has a value");
        const float mut_neg = -(c2f / c1f) - 1.0f;  // mass < 0
        check(!wrf_wdamp_reference(1.0f, 1.0f, mut_neg, c1f, c2f, rdnw, dt,
                                   alpha, crit, beta, 0, true)
                   .has_value(),
              "nonpositive mass fails close");
        check(!wrf_wdamp_reference(NAN, 1.0f, mut, c1f, c2f, rdnw, dt, alpha,
                                   crit, beta, 0, true)
                   .has_value(),
              "nonfinite ww fails close");
    }

    // (8) interior-k / boundary semantics on a grid: reference damping lives
    //     on k = 1..nz_w-2 only (Fortran DO k=2,kde-1); boundary rows zero.
    //     (Checked against the production helper's PADDING ONLY — the pad
    //     geometry is shared semantics; values are checked in (9).)
    {
        const int ny = 2, nzw = 6, nx = 3;
        auto w = torch::full({ny, nzw, nx}, 5.0f);  // huge => any CFL recipe active
        auto mu = torch::full({ny, nx}, mut);
        auto c1 = torch::full({nzw}, c1f);
        auto c2 = torch::full({nzw}, c2f);
        auto dz = torch::full({1, nzw - 2, 1}, 1.0f / rdnw);
        auto legacy = wrf::sdirk3::compute_w_damping_term(
            w, mu, c1, c2, dz, dt, alpha, crit, 1e-3f, 1, nzw - 1, nzw);
        check(legacy.select(1, 0).abs().max().item<float>() == 0.0f,
              "k=0 boundary padded to zero");
        check(legacy.select(1, nzw - 1).abs().max().item<float>() == 0.0f,
              "k=nz_w-1 boundary padded to zero");
    }

    // (9) NEGATIVE CONTROL — the pinned parity deviation. On a fixture where
    //     every point sits BELOW both thresholds in WRF's decoupled CFL, the
    //     reference is exactly zero everywhere, but the CURRENT production
    //     helper (ungated + raw-w CFL) produces a large nonzero term.
    //     Commit 3 flips this case to equality when parity lands.
    {
        const int ny = 2, nzw = 6, nx = 3;
        const float w_small = 0.01f;  // physical w ~ cm/s: decoupled CFL ~ 1e-9
        auto w = torch::full({ny, nzw, nx}, w_small);
        auto mu = torch::full({ny, nx}, mut);
        auto c1 = torch::full({nzw}, c1f);
        auto c2 = torch::full({nzw}, c2f);
        auto dz = torch::full({1, nzw - 2, 1}, 1.0f / rdnw);
        // WRF reference at these points (ww ~ mass * w-ish is irrelevant:
        // even ww = mass * w_small gives vert_cfl = w_small*rdnw*dt ≈ 384 —
        // so use the DECOUPLED definition faithfully with eta-dot scale):
        // a physically-consistent ww for a balanced state is tiny; take
        // ww = 1e-4 * mass (eta-dot 1e-4/s) => vert_cfl = 1e-4*64*600 = 3.84.
        // To pin the BELOW-threshold case use eta-dot 1e-6 => cfl 0.0384.
        auto ref = wrf_wdamp_reference(1e-6f * mass, w_small, mut, c1f, c2f,
                                       rdnw, dt, alpha, crit, beta, 0, true);
        check(ref.has_value() && *ref == 0.0f,
              "reference: below-threshold point damps exactly zero");
        auto legacy = wrf::sdirk3::compute_w_damping_term(
            w, mu, c1, c2, dz, dt, alpha, crit, 1e-3f, 1, nzw - 1, nzw);
        const float legacy_norm = legacy.norm().item<float>();
        check(legacy_norm > 1.0f,
              "NEGATIVE CONTROL: current production term is NONZERO below "
              "threshold (ungated legacy deviation pinned; commit 3 flips "
              "this to equality)");
    }

    // (10) ww == 0: CFL is exactly zero -> below every gate -> zero damping.
    {
        auto r = wrf_wdamp_reference(0.0f, 1.0f, mut, c1f, c2f, rdnw, dt,
                                     alpha, crit, beta, 0, true);
        check(r.has_value() && *r == 0.0f, "ww == 0: exactly zero damping");
    }

    // (11) rdnw*dt variation: doubling rdnw (or dt) doubles vert_cfl and
    //      moves a below-gate point above it; the excess follows linearly.
    {
        const float cfl0 = 0.8f;  // below gate at rdnw
        auto r_lo = wrf_wdamp_reference(ww_for_cfl(cfl0), 1.0f, mut, c1f, c2f,
                                        rdnw, dt, alpha, crit, beta, 0, true);
        check(r_lo.has_value() && *r_lo == 0.0f, "cfl 0.8 below gate: zero");
        auto r_hi = wrf_wdamp_reference(ww_for_cfl(cfl0), 1.0f, mut, c1f, c2f,
                                        2.0f * rdnw, dt, alpha, crit, beta, 0,
                                        true);
        const float expect_hi = alpha * (2.0f * cfl0 - crit) * mass;
        check(r_hi.has_value() &&
                  std::fabs(*r_hi - expect_hi) <= 1e-3f * std::fabs(expect_hi),
              "doubled rdnw: cfl doubles, gate opens, excess linear");
        auto r_dt = wrf_wdamp_reference(ww_for_cfl(cfl0), 1.0f, mut, c1f, c2f,
                                        rdnw, 2.0f * dt, alpha, crit, beta, 0,
                                        true);
        check(r_dt.has_value() &&
                  std::fabs(*r_dt - expect_hi) <= 1e-3f * std::fabs(expect_hi),
              "doubled dt: same as doubled rdnw");
    }

    // (12) w = +0 / -0 through the FULL reference at an active point: the
    //      Fortran-oracle zero semantics decide the damping sign.
    {
        const float cfl = 3.0f;
        const float expect = alpha * (cfl - crit) * mass;
        auto rp = wrf_wdamp_reference(ww_for_cfl(cfl), +0.0f, mut, c1f, c2f,
                                      rdnw, dt, alpha, crit, beta, 0, true);
        check(rp.has_value() &&
                  std::fabs(*rp - expect) <= 1e-3f * expect,
              "active point, w=+0: damping sign +1 (oracle)");
        auto rn = wrf_wdamp_reference(ww_for_cfl(cfl), -0.0f, mut, c1f, c2f,
                                      rdnw, dt, alpha, crit, beta, 0, true);
        check(rn.has_value() &&
                  std::fabs(*rn + expect) <= 1e-3f * expect,
              "active point, w=-0: damping sign -1 (oracle)");
    }

    const int kExpectedCases = 24;
    if (g_cases != kExpectedCases) {
        std::printf("FAIL: case-count ratchet: executed %d, expected %d\n",
                    g_cases, kExpectedCases);
        ++g_failures;
    }
    if (g_failures == 0) {
        std::printf("WRF W-damping reference contract: ALL PASS (%d/%d)\n",
                    g_cases, kExpectedCases);
        return 0;
    }
    std::printf("WRF W-damping reference contract: %d FAILURE(S)\n",
                g_failures);
    return 1;
}
