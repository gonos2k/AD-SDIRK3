// 9F.D43: the experiment-config environment parser, as a standing contract (review P1-6).
//
// WHY THIS EXISTS. The parser is the only thing standing between an operator's intent
// and what the model actually integrates. Every failure mode here is silent by nature:
// a flag that reads as OFF for "true", a duplicate spelling that reports under the
// wrong experiment name, or a substep count that quietly defaults produces a run which
// LOOKS like the one that was asked for and is not. Config_Provenance covers the
// replay gate and the stage1 integer syntax; nothing covered the boolean token matrix,
// the multi-flag rejection, or re-parsing after the environment changes.
//
// Deliberately TORCH-FREE, like the header under test. If this ever stops compiling
// without libtorch, the "torch-free config header" property has regressed and this
// fixture is where it shows up -- which is why it does not link wrf_sdirk3::core.

#include "../wrf_sdirk3_experiment_config.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

int failures = 0;
int check_count = 0;

void check(bool ok, const std::string& what) {
    ++check_count;
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;
    if (!ok) ++failures;
}

const char* kFlags[] = {
    "WRF_SDIRK3_ABLATE_RU_SLOW",
    "WRF_SDIRK3_ABLATE_RV_SLOW",
    "WRF_SDIRK3_ABLATE_UV_SLOW",
    "WRF_SDIRK3_ABLATE_UV_PGF",
    "WRF_SDIRK3_SPLIT_EXPLICIT_STAGE1_SUBSTEPS",
    "WRF_SDIRK3_UTERMS_TRACE",
    "WRF_SDIRK3_ADVECT_U_SPLIT_DUMP",
};

// Every case starts from a known-clean environment. Without this, one case's leftover
// setenv silently becomes the next case's premise -- and a suite that passes only in
// order is worse than none, because it certifies a state nobody actually configured.
void clear_env() {
    for (const char* n : kFlags) unsetenv(n);
}

bool rejects() {
    try {
        (void)wrf::sdirk3::ExperimentConfig::from_environment();
        return false;
    } catch (const std::invalid_argument&) {
        return true;
    }
}

}  // namespace

int main() {
    using namespace wrf::sdirk3;

    // --- unset -> documented defaults ---
    {
        clear_env();
        const auto c = ExperimentConfig::from_environment();
        check(c.uv_slow == UvSlowExperiment::None, "unset -> uv_slow None");
        check(c.stage1_substeps == 1, "unset -> stage1_substeps 1 (WRF behaviour)");
        const auto d = DiagnosticsConfig::from_environment();
        check(!d.trace_u_terms && !d.dump_advect_u_split,
              "unset -> diagnostics all OFF");
    }

    // --- boolean token matrix, both polarities, case-insensitive ---
    // The ON tokens matter most: a flag accepted only as "1" reads as OFF for "true",
    // which produces a baseline run reported as an experiment.
    {
        const char* on_tokens[]  = {"1", "true", "yes", "on", "TRUE", "Yes", "ON"};
        for (const char* t : on_tokens) {
            clear_env();
            setenv("WRF_SDIRK3_ABLATE_RU_SLOW", t, 1);
            const auto c = ExperimentConfig::from_environment();
            check(c.uv_slow == UvSlowExperiment::DropU,
                  std::string("ON token accepted: \"") + t + "\"");
        }
        const char* off_tokens[] = {"0", "false", "no", "off", "FALSE", "Off"};
        for (const char* t : off_tokens) {
            clear_env();
            setenv("WRF_SDIRK3_ABLATE_RU_SLOW", t, 1);
            const auto c = ExperimentConfig::from_environment();
            check(c.uv_slow == UvSlowExperiment::None,
                  std::string("OFF token accepted: \"") + t + "\"");
        }
    }

    // --- an unrecognised boolean is REJECTED, not defaulted ---
    {
        const char* bad[] = {"2", "y", "n", "maybe", "tru", " 1", ""};
        for (const char* t : bad) {
            clear_env();
            setenv("WRF_SDIRK3_ABLATE_RU_SLOW", t, 1);
            // "" is documented as absent-equivalent, not an error.
            const bool expect_reject = (t[0] != '\0');
            check(rejects() == expect_reject,
                  std::string("invalid boolean \"") + t + "\" -> " +
                  (expect_reject ? "REJECTED" : "treated as unset"));
        }
    }

    // --- RU+RV is DropBoth spelled two ways, and is named as such ---
    {
        clear_env();
        setenv("WRF_SDIRK3_ABLATE_RU_SLOW", "1", 1);
        setenv("WRF_SDIRK3_ABLATE_RV_SLOW", "1", 1);
        std::string msg;
        try { (void)ExperimentConfig::from_environment(); }
        catch (const std::invalid_argument& e) { msg = e.what(); }
        check(!msg.empty(), "RU+RV together -> REJECTED");
        check(msg.find("ABLATE_UV_SLOW") != std::string::npos,
              "RU+RV rejection NAMES the experiment it actually is");
    }

    // --- any two distinct experiments conflict ---
    {
        clear_env();
        setenv("WRF_SDIRK3_ABLATE_UV_SLOW", "1", 1);
        setenv("WRF_SDIRK3_ABLATE_UV_PGF", "1", 1);
        check(rejects(), "two distinct experiments -> REJECTED");
    }
    {
        clear_env();
        setenv("WRF_SDIRK3_ABLATE_RU_SLOW", "1", 1);
        setenv("WRF_SDIRK3_ABLATE_UV_PGF", "1", 1);
        check(rejects(), "drop-one + pgf -> REJECTED");
    }

    // --- exactly one is fine, for each ---
    {
        struct Case { const char* var; UvSlowExperiment want; const char* name; };
        const Case cases[] = {
            {"WRF_SDIRK3_ABLATE_RU_SLOW", UvSlowExperiment::DropU,    "DropU"},
            {"WRF_SDIRK3_ABLATE_RV_SLOW", UvSlowExperiment::DropV,    "DropV"},
            {"WRF_SDIRK3_ABLATE_UV_SLOW", UvSlowExperiment::DropBoth, "DropBoth"},
            {"WRF_SDIRK3_ABLATE_UV_PGF",  UvSlowExperiment::DropPgf,  "DropPgf"},
        };
        for (const auto& c : cases) {
            clear_env();
            setenv(c.var, "1", 1);
            check(ExperimentConfig::from_environment().uv_slow == c.want,
                  std::string("single experiment resolves: ") + c.name);
        }
    }

    // --- stage1_substeps range ---
    {
        const char* ok[]  = {"1", "4", "4096"};
        for (const char* v : ok) {
            clear_env();
            setenv("WRF_SDIRK3_SPLIT_EXPLICIT_STAGE1_SUBSTEPS", v, 1);
            check(ExperimentConfig::from_environment().stage1_substeps == std::atoi(v),
                  std::string("stage1_substeps accepts ") + v);
        }
        const char* bad[] = {"0", "4097", "2junk", "NaN", "-1", "1.5"};
        for (const char* v : bad) {
            clear_env();
            setenv("WRF_SDIRK3_SPLIT_EXPLICIT_STAGE1_SUBSTEPS", v, 1);
            check(rejects(), std::string("stage1_substeps rejects \"") + v + "\"");
        }
    }

    // --- 9F.D46 (review section 10): DiagnosticsConfig WIRING, not just its defaults ---
    // The only diagnostics assertion in this file was "both false when unset", and every
    // token case set an EXPERIMENT variable. So a typo in either diagnostics variable
    // name, or the two fields wired to each other's variable, passed the whole suite --
    // because unset-is-false holds for a misspelled name too. These pin WHICH variable
    // drives WHICH field, independently.
    {
        clear_env();
        setenv("WRF_SDIRK3_UTERMS_TRACE", "on", 1);
        const auto d = DiagnosticsConfig::from_environment();
        check(d.trace_u_terms && !d.dump_advect_u_split,
              "UTERMS_TRACE drives trace_u_terms ONLY (not dump)");
    }
    {
        clear_env();
        setenv("WRF_SDIRK3_ADVECT_U_SPLIT_DUMP", "TRUE", 1);
        const auto d = DiagnosticsConfig::from_environment();
        check(!d.trace_u_terms && d.dump_advect_u_split,
              "ADVECT_U_SPLIT_DUMP drives dump_advect_u_split ONLY (not trace)");
    }
    {
        clear_env();
        setenv("WRF_SDIRK3_UTERMS_TRACE", "1", 1);
        setenv("WRF_SDIRK3_ADVECT_U_SPLIT_DUMP", "1", 1);
        const auto d = DiagnosticsConfig::from_environment();
        check(d.trace_u_terms && d.dump_advect_u_split,
              "both diagnostics enabled together (neither masks the other)");
    }
    // Diagnostics flags are strict too: a diagnostic that silently reads as OFF for a
    // typo produces a run reported as instrumented that measured nothing.
    {
        struct C { const char* var; const char* val; };
        const C bad[] = {
            {"WRF_SDIRK3_UTERMS_TRACE",        "maybe"},
            {"WRF_SDIRK3_ADVECT_U_SPLIT_DUMP", "2"},
        };
        for (const auto& c : bad) {
            clear_env();
            setenv(c.var, c.val, 1);
            bool threw = false;
            try { (void)DiagnosticsConfig::from_environment(); }
            catch (const std::invalid_argument&) { threw = true; }
            check(threw, std::string("invalid diagnostics token ") + c.var + "=" +
                         c.val + " REJECTED");
        }
    }
    // Provenance must move when the config moves, or an evidence artifact can record a
    // diagnostics state it was not produced under.
    {
        clear_env();
        const std::string off = DiagnosticsConfig::from_environment().provenance();
        setenv("WRF_SDIRK3_UTERMS_TRACE", "yes", 1);
        const std::string on = DiagnosticsConfig::from_environment().provenance();
        check(off != on, "diagnostics provenance changes with the config");
    }

    // --- re-parsing reflects a CHANGED environment ---
    // This is the property that makes config object state rather than a latched
    // global: two solvers in one process must be able to differ, and a test must be
    // able to vary the setting within a process. A lazy static would pass every case
    // above and fail this one.
    {
        clear_env();
        const auto a = ExperimentConfig::from_environment();
        setenv("WRF_SDIRK3_ABLATE_UV_PGF", "1", 1);
        const auto b = ExperimentConfig::from_environment();
        check(a.uv_slow == UvSlowExperiment::None && b.uv_slow == UvSlowExperiment::DropPgf,
              "re-parse reflects changed env (not latched on first read)");
        check(a.provenance() != b.provenance(),
              "changed config yields a different provenance string");
    }

    clear_env();

    constexpr int expected_checks = 48;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) { std::cout << "EXPERIMENT_CONFIG_PARSER: PASS" << std::endl; return 0; }
    std::cout << "EXPERIMENT_CONFIG_PARSER: FAIL (" << failures << ")" << std::endl;
    return 1;
}
