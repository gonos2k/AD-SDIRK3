// 9F.D122 (review section 4): the mass-coordinate scheme has ONE authority.
//
// The two corrections shipped as independent booleans -- wrf_omega_ww_cp and
// mu_horizontal_div_only. Four states are expressible; only two are schemes. The mixed states
// are not independent physics options, they are INCOMPLETE intermediates:
//
//   Omega ON  / mu-horiz OFF : Omega is right, but the column-mass equation still carries a
//                              vertical term advance_mu_t does not have
//   Omega OFF / mu-horiz ON  : the mass equation is right, but ph vertical advection and the
//                              mu divergence still ride on Omega = mu*w
//
// Two booleans cannot express "these two go together". A mode can, and it makes a partial
// state a DELIBERATE diagnostic choice rather than something a namelist typo produces.
//
// This file exists because that is a CLAIM until something rejects the alternative. The
// mutation cases below are the point: each one asserts the authority actually says no.

#include "../wrf_sdirk3_config.h"

#include <iostream>
#include <string>

namespace {

int failures = 0;
int check_count = 0;

void check(bool ok, const std::string& what) {
    ++check_count;
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;
    if (!ok) ++failures;
}

using Mode = wrf::sdirk3::SDIRK3Config::MassCoordinateMode;

// A config carrying the LEGACY booleans set the "wrong" way round, so every mode assertion
// below is proved to come from the mode and not from the booleans leaking through.
wrf::sdirk3::SDIRK3Config with_mode(Mode m, bool raw_omega, bool raw_mu) {
    wrf::sdirk3::SDIRK3Config c;
    c.mass_coordinate_mode = static_cast<int>(m);
    c.wrf_omega_ww_cp = raw_omega;
    c.mu_horizontal_div_only = raw_mu;
    return c;
}

}  // namespace

int main() {
    std::cout << "=== Mass_Coordinate_Mode_Contract ===" << std::endl;

    // ------------------------------------------------------------ 1. default is no change
    {
        wrf::sdirk3::SDIRK3Config c;
        check(c.mass_coordinate_mode == static_cast<int>(Mode::Legacy),
              "default mode is Legacy (repo guardrail: opt-in, no behavior change)");
        check(!c.effective_wrf_omega_ww_cp() && !c.effective_mu_horizontal_div_only(),
              "default resolves to BOTH corrections off");
        check(std::string(c.mass_coordinate_mode_name()) == "Legacy",
              "default names itself Legacy");
    }

    // -------------------------------------------- 2. WRFParity forces BOTH, whatever the raws
    // This is the whole point of the type: the WRF scheme cannot be half-applied.
    {
        auto c = with_mode(Mode::WRFParity, false, false);   // raws say "off"
        check(c.effective_wrf_omega_ww_cp(),
              "WRFParity forces Omega=calc_ww_cp even when the legacy boolean says off");
        check(c.effective_mu_horizontal_div_only(),
              "WRFParity forces horizontal-only mu even when the legacy boolean says off");
        check(std::string(c.mass_coordinate_mode_name()) == "WRFParity", "WRFParity names itself");
    }

    // ------------------------------ 3. THE CASE THAT MOTIVATED THIS: no half-applied parity
    // With two booleans, (ON, OFF) and (OFF, ON) are reachable by a single typo and look like
    // ordinary settings. Under the mode they are reachable ONLY by naming a diagnostic mode.
    {
        auto p = with_mode(Mode::WRFParity, true, false);
        check(p.effective_wrf_omega_ww_cp() && p.effective_mu_horizontal_div_only(),
              "WRFParity CANNOT be half-applied: a raw mu=false does not produce a partial scheme");

        auto q = with_mode(Mode::WRFParity, false, true);
        check(q.effective_wrf_omega_ww_cp() && q.effective_mu_horizontal_div_only(),
              "WRFParity CANNOT be half-applied: a raw omega=false does not produce a partial scheme");
    }

    // -------------------------------------- 4. the diagnostic modes ARE partial, and say so
    // They are kept deliberately: measuring the Omega correction ALONE is what showed it
    // carries the whole effect (every block collapses before the mu term is touched). A mode
    // that forbade partial states would have forbidden that measurement.
    {
        auto o = with_mode(Mode::DiagnosticOmegaOnly, false, true);
        check(o.effective_wrf_omega_ww_cp(), "DiagnosticOmegaOnly: Omega on");
        check(!o.effective_mu_horizontal_div_only(),
              "DiagnosticOmegaOnly: mu correction OFF even though the raw boolean says on");

        auto m = with_mode(Mode::DiagnosticMuOnly, true, false);
        check(!m.effective_wrf_omega_ww_cp(),
              "DiagnosticMuOnly: Omega OFF even though the raw boolean says on");
        check(m.effective_mu_horizontal_div_only(), "DiagnosticMuOnly: mu correction on");
    }

    // ------------------------------- 5. Legacy mode DEFERS to the booleans (back-compatibility)
    // The two namelist booleans already shipped. Legacy must keep honouring them or existing
    // runs change meaning silently -- which is the failure this whole file is about.
    {
        check(with_mode(Mode::Legacy, true, false).effective_wrf_omega_ww_cp(),
              "Legacy honours the shipped wrf_omega_ww_cp boolean");
        check(with_mode(Mode::Legacy, false, true).effective_mu_horizontal_div_only(),
              "Legacy honours the shipped mu_horizontal_div_only boolean");
        check(!with_mode(Mode::Legacy, false, false).effective_wrf_omega_ww_cp() &&
                  !with_mode(Mode::Legacy, false, false).effective_mu_horizontal_div_only(),
              "Legacy with both booleans off is the untouched baseline");
        check(with_mode(Mode::Legacy, true, true).effective_wrf_omega_ww_cp() &&
                  with_mode(Mode::Legacy, true, true).effective_mu_horizontal_div_only(),
              "Legacy with both booleans on reproduces the WRF-parity pair");
    }

    // --------------------------------- 6. every mode resolves; none falls through to garbage
    {
        // The expected resolution of EVERY declared mode, written out. A loop that only calls
        // the accessors and discards the result proves nothing -- the first version of this
        // block did exactly that and its "all_resolve" flag could never become false. An
        // assertion nothing can falsify is not an assertion.
        struct Row { Mode m; bool omega; bool mu; const char* name; };
        const Row table[] = {
            {Mode::Legacy,              false, false, "Legacy"},               // raws are off here
            {Mode::WRFParity,           true,  true,  "WRFParity"},
            {Mode::DiagnosticOmegaOnly, true,  false, "DiagnosticOmegaOnly"},
            {Mode::DiagnosticMuOnly,    false, true,  "DiagnosticMuOnly"},
        };
        bool all_named = true, all_resolve = true;
        for (const auto& row : table) {
            auto c = with_mode(row.m, false, false);
            if (std::string(c.mass_coordinate_mode_name()) != row.name) all_named = false;
            if (c.effective_wrf_omega_ww_cp() != row.omega ||
                c.effective_mu_horizontal_div_only() != row.mu) all_resolve = false;
        }
        check(all_named, "every declared mode reports its documented name");
        check(all_resolve, "every declared mode resolves to its documented (omega, mu) pair");

        // An out-of-range value must be VISIBLY invalid rather than silently reading as Legacy.
        wrf::sdirk3::SDIRK3Config bad;
        bad.mass_coordinate_mode = 7;
        check(std::string(bad.mass_coordinate_mode_name()) == "INVALID",
              "an out-of-range mode names itself INVALID rather than impersonating Legacy");
    }

    // ------------------- 7. the mode is the AUTHORITY: a non-zero mode ignores the raws entirely
    // Stated as its own case because it is the property every consumer relies on. If a future
    // edit makes a consumer read the raw boolean again, the effective value and the raw value
    // diverge and this catches it.
    {
        for (Mode m : {Mode::WRFParity, Mode::DiagnosticOmegaOnly, Mode::DiagnosticMuOnly}) {
            auto a = with_mode(m, false, false);
            auto b = with_mode(m, true, true);
            check(a.effective_wrf_omega_ww_cp() == b.effective_wrf_omega_ww_cp() &&
                      a.effective_mu_horizontal_div_only() ==
                          b.effective_mu_horizontal_div_only(),
                  std::string("mode ") + a.mass_coordinate_mode_name() +
                      " resolves identically regardless of the legacy booleans");
        }
    }

    constexpr int expected_checks = 22;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) { std::cout << "MASS_COORDINATE_MODE: PASS" << std::endl; return 0; }
    std::cout << "MASS_COORDINATE_MODE: FAIL (" << failures << ")" << std::endl;
    return 1;
}
