// wrf_sdirk3_experiment_config.h -- what an experiment selects, and what a
// diagnostic observes. TORCH-FREE ON PURPOSE.
//
// 9F.D33 (review section 7). These types are needed as SOLVER MEMBERS, so the solver
// header must see them. Putting them in the diagnostics header would have dragged
// torch, iostream and fstream into every translation unit that includes the solver --
// the dependency blow-up the review warned about. They have no tensor dependency, so
// they belong in their own small header.
//
// THE INVARIANT THESE TWO TYPES EXIST TO PROTECT:
//
//     ExperimentConfig  MAY change the trajectory.
//     DiagnosticsConfig MUST NOT change the NUMERICAL RESULT.
//
// 9F.D35 (review section 10): the second line used to read "MUST NOT change the
// trajectory", which is stronger than anything the code structurally guarantees --
// diagnostics ON does full tensor clones, GPU->CPU syncs, multi-line I/O and a binary
// write, all of which change timing, allocation and failure surface. The contract
// that IS meant is narrower and testable:
//
//     diagnostics must not mutate numerical state, alter an operand, add an RHS
//     evaluation, or feed any result into a solver decision.
//
// Byte-identical fingerprint with diagnostics OFF is evidence for it but is NOT the
// same statement: it does not exercise the ON path. A standing OFF-vs-ON contract
// (equal RHS count, equal ordered stage-state digest, equal termination signature)
// would close that, and does not exist yet.
//
// Separate types so that boundary cannot blur. This campaign has been misled in both
// directions -- a "diagnostic" that silently changed an operand, and an "experiment"
// that turned out to be a no-op.

#ifndef WRF_SDIRK3_EXPERIMENT_CONFIG_H
#define WRF_SDIRK3_EXPERIMENT_CONFIG_H

#include <c10/util/Exception.h>

#include <cctype>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace wrf {
namespace sdirk3 {

// Which u/v slow-channel ablation is in flight. Exactly one, by construction: the
// previous four-boolean form made illegal combinations representable and then tried
// to reject them afterwards, and kept failing to (RU+RV slipped past two guards).
enum class UvSlowExperiment { None, DropU, DropV, DropBoth, DropPgf };


// Settings that MAY change the trajectory.
struct ExperimentConfig {
    UvSlowExperiment uv_slow = UvSlowExperiment::None;
    int stage1_substeps = 1;   // acoustic stage-1 subdivision (1 == WRF)

    // Reads and VALIDATES the environment once. Rejects illegal combinations and
    // malformed values rather than silently defaulting -- a silently-defaulted
    // experiment reports success while running the baseline.
    static ExperimentConfig from_environment();  // defined below
};

// Settings that MUST NOT change the trajectory.
struct DiagnosticsConfig {
    bool trace_u_terms = false;
    bool dump_advect_u_split = false;

    static DiagnosticsConfig from_environment();  // defined below
};


namespace uslow_detail {

// Strict boolean. A diagnostic flag that silently reads as OFF for "true"/"on"/a typo
// is the worst failure mode in this campaign: it produces a run that looks like the
// experiment and is not.
inline bool strict_env_flag(const char* name) {
    const char* v = std::getenv(name);
    if (v == nullptr || v[0] == '\0') return false;
    std::string t(v);
    for (auto& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (t == "1" || t == "true" || t == "yes" || t == "on")  return true;
    if (t == "0" || t == "false" || t == "no" || t == "off") return false;
    TORCH_CHECK(false, "env flag ", name, "=\"", v,
                "\" is not a boolean (use 1/true/yes/on or 0/false/no/off)");
    return false;
}

}  // namespace uslow_detail

inline const char* uv_slow_experiment_name(UvSlowExperiment m) {
    switch (m) {
        case UvSlowExperiment::DropU:    return "DropU(ABLATE_RU_SLOW)";
        case UvSlowExperiment::DropV:    return "DropV(ABLATE_RV_SLOW)";
        case UvSlowExperiment::DropBoth: return "DropBoth(ABLATE_UV_SLOW)";
        case UvSlowExperiment::DropPgf:  return "DropPgf(ABLATE_UV_PGF)";
        case UvSlowExperiment::None:     break;
    }
    return "None(production)";
}

inline ExperimentConfig ExperimentConfig::from_environment() {
    ExperimentConfig c;

    const bool drop_u    = uslow_detail::strict_env_flag("WRF_SDIRK3_ABLATE_RU_SLOW");
    const bool drop_v    = uslow_detail::strict_env_flag("WRF_SDIRK3_ABLATE_RV_SLOW");
    const bool drop_both = uslow_detail::strict_env_flag("WRF_SDIRK3_ABLATE_UV_SLOW");
    const bool drop_pgf  = uslow_detail::strict_env_flag("WRF_SDIRK3_ABLATE_UV_PGF");
    const int n = int(drop_u) + int(drop_v) + int(drop_both) + int(drop_pgf);

    // RU+RV is named explicitly because it is not a random conflict -- it IS DropBoth
    // spelled two other ways, and reporting it under the drop-one names is exactly the
    // silent duplicate this design exists to prevent.
    TORCH_CHECK(!(drop_u && drop_v && !drop_both && !drop_pgf),
        "WRF_SDIRK3_ABLATE_RU_SLOW + ABLATE_RV_SLOW together ARE ABLATE_UV_SLOW. Use "
        "WRF_SDIRK3_ABLATE_UV_SLOW so the run is named for the experiment it performs.");
    TORCH_CHECK(n <= 1,
        "select exactly ONE u/v slow experiment; got ", n, " of {ABLATE_RU_SLOW, "
        "ABLATE_RV_SLOW, ABLATE_UV_SLOW, ABLATE_UV_PGF}.");

    // 9F.D32 (review section 2): stage1_substeps is READ HERE. It was previously
    // declared on this struct and never set, while the value actually used came from
    // a separate acoustic_schedule_options_from_env(). That split brain meant any
    // future reader of ExperimentConfig::stage1_substeps would silently get 1 no
    // matter what the operator set -- a dead authority that looks live.
    if (const char* v = std::getenv("WRF_SDIRK3_SPLIT_EXPLICIT_STAGE1_SUBSTEPS")) {
        if (v[0] != '\0') {
            const std::string sv(v);
            std::size_t consumed = 0;
            int n = 0;
            try { n = std::stoi(sv, &consumed); } catch (const std::exception&) { consumed = 0; }
            TORCH_CHECK(consumed == sv.size() && n >= 1 && n <= 4096,
                "WRF_SDIRK3_SPLIT_EXPLICIT_STAGE1_SUBSTEPS must be an integer in "
                "[1,4096] with no trailing characters; got \"", sv, "\".");
            c.stage1_substeps = n;
        }
    }

    if (drop_u)         c.uv_slow = UvSlowExperiment::DropU;
    else if (drop_v)    c.uv_slow = UvSlowExperiment::DropV;
    else if (drop_both) c.uv_slow = UvSlowExperiment::DropBoth;
    else if (drop_pgf)  c.uv_slow = UvSlowExperiment::DropPgf;

    return c;
}

inline DiagnosticsConfig DiagnosticsConfig::from_environment() {
    DiagnosticsConfig d;
    d.trace_u_terms       = uslow_detail::strict_env_flag("WRF_SDIRK3_UTERMS_TRACE");
    d.dump_advect_u_split = uslow_detail::strict_env_flag("WRF_SDIRK3_ADVECT_U_SPLIT_DUMP");
    return d;
}


}  // namespace sdirk3
}  // namespace wrf

#endif  // WRF_SDIRK3_EXPERIMENT_CONFIG_H
