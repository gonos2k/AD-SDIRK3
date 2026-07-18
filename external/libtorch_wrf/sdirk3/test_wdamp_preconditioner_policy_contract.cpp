// PR 9D commit 1: standing contract pinning the W-damping operator/
// preconditioner source semantics. The authority is
// resolve_wdamp_preconditioner_policy (wrf_sdirk3_wdamp_preconditioner_policy.h):
//   - the physical W direct diagonal is ALWAYS 0 (WRF-parity smooth tangent
//     has no direct w path — see WDamp_Tangent_Contract's pure-w case);
//   - the scalar w_damp_alpha reaches the W diagonal ONLY via the explicit
//     precond_extra_wdamp regularization, never mirrored from the RHS config;
//   - the retired legacy formula (implicit_wdamp || extra ? alpha : 0) is the
//     negative control and must DISAGREE on the default fixture.
#include <cmath>
#include <cstdio>
#include "wrf_sdirk3_wdamp_preconditioner_policy.h"

namespace {

int g_cases = 0, g_fail = 0;
void check(bool ok, const char* what) {
    ++g_cases;
    if (!ok) { ++g_fail; std::printf("FAIL: %s\n", what); }
}

using wrf::sdirk3::SDIRK3Config;
using wrf::sdirk3::resolve_wdamp_preconditioner_policy;
using wrf::sdirk3::wdamp_rhs_config_enabled;

// A config seeded to the em_b_wave-ish defaults, tweaked per case.
SDIRK3Config base_config() {
    SDIRK3Config c;
    c.wrf_w_damping = 0;
    c.implicit_wdamp = true;
    c.wrf_w_crit_cfl = 1.0f;
    c.w_damp_alpha = 0.3f;
    c.precond_extra_wdamp = false;
    c.precond_match_rhs = true;
    return c;
}

// The RETIRED legacy formula, kept only as this test's negative control.
float legacy_w_diagonal(const SDIRK3Config& c) {
    const bool eff = c.implicit_wdamp || c.precond_extra_wdamp;
    return eff ? c.w_damp_alpha : 0.0f;
}

}  // namespace

int main() {
    // (1) WRF off + implicit_wdamp on + extra off -> not rhs-enabled, no diag.
    {
        auto c = base_config();  // wrf_w_damping=0, implicit_wdamp=true
        auto p = resolve_wdamp_preconditioner_policy(c);
        check(!p.rhs_config_enabled, "case1: WRF off -> rhs_config_enabled false");
        check(p.physical_w_diagonal == 0.0f, "case1: physical diag 0");
        check(p.extra_regularization_alpha == 0.0f, "case1: extra diag 0");
    }
    // (2) WRF on + implicit_wdamp off -> not rhs-enabled, no diag.
    {
        auto c = base_config();
        c.wrf_w_damping = 1;
        c.implicit_wdamp = false;
        auto p = resolve_wdamp_preconditioner_policy(c);
        check(!p.rhs_config_enabled, "case2: implicit_wdamp off -> not enabled");
        check(p.physical_w_diagonal == 0.0f, "case2: physical diag 0");
    }
    // (3) WRF on + implicit_wdamp on -> rhs-enabled, but STILL no physical diag.
    {
        auto c = base_config();
        c.wrf_w_damping = 1;
        auto p = resolve_wdamp_preconditioner_policy(c);
        check(p.rhs_config_enabled, "case3: WRF on + implicit on -> enabled");
        check(p.physical_w_diagonal == 0.0f,
              "case3: rhs-enabled does NOT add a physical W diagonal");
        check(p.extra_regularization_alpha == 0.0f,
              "case3: rhs-enabled does NOT add a scalar diagonal");
    }
    // (4) precond_match_rhs=false does not revive a hidden legacy diagonal.
    {
        auto c = base_config();
        c.wrf_w_damping = 1;
        c.precond_match_rhs = false;
        auto p = resolve_wdamp_preconditioner_policy(c);
        check(p.physical_w_diagonal == 0.0f,
              "case4: precond_match_rhs=false -> still no hidden W diagonal");
        check(p.extra_regularization_alpha == 0.0f,
              "case4: precond_match_rhs=false -> no scalar diagonal");
    }
    // (5) extra=true, alpha=0.3 -> explicit regularization present.
    {
        auto c = base_config();
        c.precond_extra_wdamp = true;
        c.w_damp_alpha = 0.3f;
        auto p = resolve_wdamp_preconditioner_policy(c);
        check(p.extra_regularization, "case5: extra flag set");
        check(p.extra_regularization_alpha == 0.3f,
              "case5: extra regularization alpha = 0.3");
        check(p.physical_w_diagonal == 0.0f,
              "case5: extra regularization is NOT a physical diagonal");
    }
    // (6) WRF off + extra=true -> regularization still present (RHS-independent).
    {
        auto c = base_config();
        c.wrf_w_damping = 0;
        c.precond_extra_wdamp = true;
        auto p = resolve_wdamp_preconditioner_policy(c);
        check(!p.rhs_config_enabled, "case6: WRF off -> not rhs-enabled");
        check(p.extra_regularization_alpha == 0.3f,
              "case6: explicit regularization independent of RHS config");
    }
    // (7) extra=false, change alpha -> numerical policy unchanged (0).
    {
        auto c = base_config();
        c.precond_extra_wdamp = false;
        c.w_damp_alpha = 0.3f;
        auto p1 = resolve_wdamp_preconditioner_policy(c);
        c.w_damp_alpha = 0.9f;
        auto p2 = resolve_wdamp_preconditioner_policy(c);
        check(p1.extra_regularization_alpha == p2.extra_regularization_alpha &&
                  p1.extra_regularization_alpha == 0.0f,
              "case7: alpha change with extra OFF -> policy unchanged (0)");
        check(wdamp_preconditioner_signature(p1) ==
                  wdamp_preconditioner_signature(p2),
              "case7: signature unchanged when extra OFF");
    }
    // (8) extra=true, change alpha -> policy fingerprint changes.
    {
        auto c = base_config();
        c.precond_extra_wdamp = true;
        c.w_damp_alpha = 0.3f;
        auto p1 = resolve_wdamp_preconditioner_policy(c);
        c.w_damp_alpha = 0.9f;
        auto p2 = resolve_wdamp_preconditioner_policy(c);
        check(p1.extra_regularization_alpha != p2.extra_regularization_alpha,
              "case8: alpha change with extra ON -> alpha follows");
        check(wdamp_preconditioner_signature(p1) !=
                  wdamp_preconditioner_signature(p2),
              "case8: signature changes when extra ON and alpha changes");
    }
    // (9) NaN / Inf / negative extra alpha -> stable config failure.
    {
        auto expect_throw = [](float bad, const char* label) {
            auto c = base_config();
            c.precond_extra_wdamp = true;
            c.w_damp_alpha = bad;
            bool threw = false;
            try {
                resolve_wdamp_preconditioner_policy(c);
            } catch (const std::invalid_argument& e) {
                threw = std::string(e.what()).find(
                            "SDIRK3_WDAMP_PRECOND_INVALID_ALPHA") == 0;
            }
            check(threw, label);
        };
        expect_throw(std::nanf(""), "case9: NaN extra alpha -> stable failure");
        expect_throw(INFINITY, "case9: Inf extra alpha -> stable failure");
        expect_throw(-0.3f, "case9: negative extra alpha -> stable failure");
    }
    // (10) NEGATIVE CONTROL: the retired legacy formula must DISAGREE with the
    //      policy on the default fixture (WRF off but implicit_wdamp on).
    {
        auto c = base_config();  // implicit_wdamp=true, extra=false
        auto p = resolve_wdamp_preconditioner_policy(c);
        const float legacy = legacy_w_diagonal(c);  // = 0.3 (implicit on)
        const float policy_diag =
            p.physical_w_diagonal + p.extra_regularization_alpha;  // = 0
        check(legacy != policy_diag,
              "case10: legacy formula (0.3) DISAGREES with the policy (0) on "
              "the default fixture — the stale mirror is removed");
    }

    const int kExpected = 3 + 2 + 3 + 2 + 3 + 2 + 2 + 2 + 3 + 1;  // = 23
    if (g_cases != kExpected) {
        std::printf("FAIL: case-count ratchet: executed %d, expected %d\n",
                    g_cases, kExpected);
        ++g_fail;
    }
    if (g_fail == 0) {
        std::printf("WDamp preconditioner policy contract: ALL PASS (%d/%d)\n",
                    g_cases, kExpected);
        return 0;
    }
    std::printf("WDamp preconditioner policy contract: %d FAILURE(S)\n", g_fail);
    return 1;
}
