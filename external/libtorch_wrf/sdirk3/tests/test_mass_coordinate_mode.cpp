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

#include <cstdlib>   // std::atoi on the Registry default token
#include <fstream>
#include <iostream>
#include <sstream>
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

    // ------------------------------------------------------------ 1. default is WRF parity
    // R14 (2026-08-30): the opt-in guardrail is deliberately OVERRIDDEN for this knob. Legacy
    // aliases Omega := mu*w, which this repository's own measurements identified as the root
    // cause of the stage-2 mu/phi/theta anomalies; WRFParity uses calc_ww_cp and the
    // horizontal-only mu tendency, i.e. WRF's definitions. A default that is known to be
    // wrong is not "no behaviour change" -- it is a wrong behaviour shipped by default.
    // Measured on em_b_wave dt=600: mode 1 halves the stage-2 entry residual and moves it
    // entirely into the phi block, where the shipped preconditioner then fails outright.
    // That is the honest failure; the Legacy one was progress on the wrong operator.
    {
        wrf::sdirk3::SDIRK3Config c;
        check(c.mass_coordinate_mode == static_cast<int>(Mode::WRFParity),
              "default mode is WRFParity: WRF's Omega and mu tendency, not the mu*w alias");
        check(c.effective_wrf_omega_ww_cp() && c.effective_mu_horizontal_div_only(),
              "default resolves to BOTH corrections on");
        check(std::string(c.mass_coordinate_mode_name()) == "WRFParity",
              "default names itself WRFParity");
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

    // ---------------- 5. the legacy booleans are a WIRE FORMAT, resolved into the mode once
    // They used to be a SECOND authority: effective_*() read them in the Legacy branch, so
    // mode 0 was four operators, not one. Now production reads the mode alone and the booleans
    // are folded in by resolve_legacy_mass_coordinate_flags(). The old behaviour is preserved
    // exactly, because (omega, mu) -> mode is a bijection onto the four declared modes.
    {
        // Before resolution the raws do NOTHING -- that is the single-authority property.
        check(!with_mode(Mode::Legacy, true, true).effective_wrf_omega_ww_cp() &&
                  !with_mode(Mode::Legacy, true, true).effective_mu_horizontal_div_only(),
              "unresolved: Legacy ignores the raw booleans entirely (one authority)");

        struct Row { bool omega; bool mu; Mode expect; const char* what; };
        const Row rows[] = {
            {true,  true,  Mode::WRFParity,           "(omega, mu) -> WRFParity"},
            {true,  false, Mode::DiagnosticOmegaOnly, "(omega only) -> DiagnosticOmegaOnly"},
            {false, true,  Mode::DiagnosticMuOnly,    "(mu only) -> DiagnosticMuOnly"},
        };
        for (const auto& r : rows) {
            auto c = with_mode(Mode::Legacy, r.omega, r.mu);
            c.resolve_legacy_mass_coordinate_flags();
            check(c.mass_coordinate_mode == static_cast<int>(r.expect) &&
                      c.effective_wrf_omega_ww_cp() == r.omega &&
                      c.effective_mu_horizontal_div_only() == r.mu,
                  std::string("legacy wire format resolves ") + r.what +
                      ", preserving the shipped meaning");
        }

        auto none = with_mode(Mode::Legacy, false, false);
        none.resolve_legacy_mass_coordinate_flags();
        check(none.mass_coordinate_mode == static_cast<int>(Mode::Legacy),
              "both booleans off stays Legacy: nothing to migrate, no warning");

        // A named mode is an explicit choice and must survive a stale boolean.
        auto named = with_mode(Mode::WRFParity, false, false);
        named.resolve_legacy_mass_coordinate_flags();
        check(named.mass_coordinate_mode == static_cast<int>(Mode::WRFParity),
              "a named mode is never rewritten by the legacy booleans");

        // Twice must equal once, or a second config pass would migrate a migrated value.
        auto twice = with_mode(Mode::Legacy, true, false);
        twice.resolve_legacy_mass_coordinate_flags();
        const int after_first = twice.mass_coordinate_mode;
        twice.resolve_legacy_mass_coordinate_flags();
        check(twice.mass_coordinate_mode == after_first,
              "resolution is idempotent");
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
        for (Mode m : {Mode::Legacy, Mode::WRFParity, Mode::DiagnosticOmegaOnly,
                       Mode::DiagnosticMuOnly}) {
            auto a = with_mode(m, false, false);
            auto b = with_mode(m, true, true);
            check(a.effective_wrf_omega_ww_cp() == b.effective_wrf_omega_ww_cp() &&
                      a.effective_mu_horizontal_div_only() ==
                          b.effective_mu_horizontal_div_only(),
                  std::string("mode ") + a.mass_coordinate_mode_name() +
                      " resolves identically regardless of the legacy booleans");
        }
    }

    // ------------- 8. the Registry default FIELD and its description agree with this default
    // They disagreed: the field said 1 while the description said "default 0=Legacy". A user
    // who left the knob unset would have recorded the run as Legacy and got WRFParity. The
    // Registry is a second authority for the same value, so bind it here rather than trust it.
#ifdef SDIRK3_REGISTRY_FILE
    {
        std::ifstream reg(SDIRK3_REGISTRY_FILE);
        check(reg.is_open(), "Registry file is readable: " SDIRK3_REGISTRY_FILE);
        // Match the NAME FIELD, not "the line mentions the name" -- the sibling booleans'
        // descriptions now name this knob, so a substring search picked up the wrong rconfig
        // line and read ".false." as the default. (Caught by this very check.)
        //   rconfig <type> <name> <how-set> <nentries> <default> ...
        std::string line, entry, default_tok;
        while (std::getline(reg, line)) {
            std::istringstream f(line);
            std::string kw, type, name;
            if (!(f >> kw >> type >> name)) continue;
            if (kw != "rconfig" || name != "sdirk3_mass_coordinate_mode") continue;
            std::string how_set, nentries;
            if (f >> how_set >> nentries >> default_tok) entry = line;
            break;
        }
        check(!entry.empty(), "Registry declares rconfig sdirk3_mass_coordinate_mode");

        // Reject a non-integer default outright; atoi would have read ".false." as 0.
        const bool default_is_int =
            !default_tok.empty() &&
            default_tok.find_first_not_of("0123456789") == std::string::npos;
        wrf::sdirk3::SDIRK3Config c;
        check(default_is_int && std::atoi(default_tok.c_str()) == c.mass_coordinate_mode,
              "Registry default field == C++ default (\"" + default_tok + "\" vs " +
                  std::to_string(c.mass_coordinate_mode) + ")");

        // The prose must not contradict the field. "default 0" was the exact stale wording.
        const bool prose_ok = entry.find("default 0") == std::string::npos &&
                              entry.find("[DEFAULT]") != std::string::npos;
        check(prose_ok, "Registry description marks the real default and no longer says 'default 0'");
    }
#else
    check(false, "SDIRK3_REGISTRY_FILE must be defined so the Registry default is checked");
#endif

    constexpr int expected_checks = 30;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) { std::cout << "MASS_COORDINATE_MODE: PASS" << std::endl; return 0; }
    std::cout << "MASS_COORDINATE_MODE: FAIL (" << failures << ")" << std::endl;
    return 1;
}
