// wrf_sdirk3_experiment_config.h -- what an experiment selects, and what a
// diagnostic observes. DEPENDENCY-FREE ON PURPOSE (standard C++ only).
//
// 9F.D36 (review section 6): this claimed "TORCH-FREE" while including
// <c10/util/Exception.h> for TORCH_CHECK -- torch::Tensor-free, but not
// LibTorch-free, so the parser could not be compiled or tested standalone. It
// now throws std::invalid_argument and includes only standard headers, which
// makes the claim true and the header independently testable.
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

#include <cctype>
#include <cstdint>
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

    // 9F.D39 (review section 7): replay provenance.
    //
    // Both fields below CHANGE THE TRAJECTORY: uv_slow ablates terms from the slow
    // tendency, stage1_substeps subdivides the acoustic stage-1 step. So a checkpoint
    // or adjoint replay started under a different ExperimentConfig is integrating a
    // DIFFERENT MODEL, and nothing in the state itself records that. Making the
    // config object state (D33) fixed the lifetime problem; it did not make the
    // setting recoverable from an artifact after the fact.
    //
    // A canonical string rather than an opaque hash: it is diffable by eye, it names
    // which field differs, and it can still be hashed when a fixed-width id is needed
    // (see digest()). An opaque hash tells you THAT something changed, which is the
    // less useful half.
    std::string provenance() const;   // defined below
    std::uint64_t digest() const;     // defined below
};

// Settings that MUST NOT change the trajectory.
struct DiagnosticsConfig {
    bool trace_u_terms = false;
    bool dump_advect_u_split = false;

    static DiagnosticsConfig from_environment();  // defined below

    // Recorded in the evidence manifest but deliberately NOT part of any state digest:
    // if a diagnostics flag ever changed the trajectory that would be the bug, and
    // folding it into the state digest would hide that bug behind a "configs differ"
    // message instead of surfacing it as a numerical difference.
    std::string provenance() const;   // defined below
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
    throw std::invalid_argument(
        std::string("env flag ") + name + "=\"" + v +
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

// 9F.D39: STABLE token for provenance, deliberately separate from the display name
// above. The display strings embed environment-variable spellings -- "DropU(ABLATE_RU_SLOW)"
// -- which is useful in a log banner and WRONG in a replay digest: renaming an env var
// would change the digest without changing behavior, so every previously recorded
// checkpoint would spuriously fail the gate. A gate that fails for cosmetic reasons is
// a gate people switch off. These tokens must therefore never be edited for style.
inline const char* uv_slow_experiment_token(UvSlowExperiment m) {
    switch (m) {
        case UvSlowExperiment::DropU:    return "drop_u";
        case UvSlowExperiment::DropV:    return "drop_v";
        case UvSlowExperiment::DropBoth: return "drop_both";
        case UvSlowExperiment::DropPgf:  return "drop_pgf";
        case UvSlowExperiment::None:     break;
    }
    return "none";
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
    if (drop_u && drop_v && !drop_both && !drop_pgf) {
        throw std::invalid_argument(
            "WRF_SDIRK3_ABLATE_RU_SLOW + ABLATE_RV_SLOW together ARE ABLATE_UV_SLOW. "
            "Use WRF_SDIRK3_ABLATE_UV_SLOW so the run is named for the experiment it "
            "performs.");
    }
    if (n > 1) {
        throw std::invalid_argument(
            "select exactly ONE u/v slow experiment; got " + std::to_string(n) +
            " of {ABLATE_RU_SLOW, ABLATE_RV_SLOW, ABLATE_UV_SLOW, ABLATE_UV_PGF}.");
    }

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
            if (!(consumed == sv.size() && n >= 1 && n <= 4096)) {
                throw std::invalid_argument(
                    "WRF_SDIRK3_SPLIT_EXPLICIT_STAGE1_SUBSTEPS must be an integer in "
                    "[1,4096] with no trailing characters; got \"" + sv + "\".");
            }
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



// --- 9F.D39 provenance (review section 7) -------------------------------------
//
// Canonical form: one "key=value" per setting, fixed order, no environment text.
// Fixed order matters -- a set-like or map-ordered rendering would make two identical
// configs produce different strings on different runs, which turns the check into
// noise and trains people to ignore it.

inline std::string ExperimentConfig::provenance() const {
    std::string o = "uv_slow=";
    o += uv_slow_experiment_token(uv_slow);
    o += " stage1_substeps=";
    o += std::to_string(stage1_substeps);
    return o;
}

// FNV-1a over the canonical string. Not cryptographic and not meant to be -- this
// detects accidental divergence between two runs, not a forged artifact.
inline std::uint64_t ExperimentConfig::digest() const {
    const std::string s = provenance();
    std::uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : s) {
        h ^= static_cast<std::uint64_t>(c);
        h *= 1099511628211ULL;
    }
    return h;
}

// Replay/restart gate. FAILS CLOSED by explicit decision: a mismatch aborts rather
// than warning.
//
// The reasoning is the asymmetry of the two failure modes. If this aborts a run that
// would have been fine, the cost is a restart and an obvious message. If it merely
// warned, an adjoint replay under the wrong linearization would produce gradients
// that are WRONG BUT FINITE -- no NaN, no crash, nothing downstream that looks
// abnormal. This campaign has spent months on exactly that class of silent failure,
// and 4D-Var gradients are the worst place to add another one.
//
// Throws std::invalid_argument rather than TORCH_CHECK to keep this header
// torch-free (review section 6). Names BOTH provenance strings, because "configs
// differ" without saying which field is the message people learn to ignore.
inline void require_matching_experiment_config(const std::string& recorded_provenance,
                                               const ExperimentConfig& current) {
    if (recorded_provenance == current.provenance()) return;
    throw std::invalid_argument(
        "SDIRK3 experiment config mismatch at restart/replay -- refusing to continue.\n"
        "  recorded: " + recorded_provenance + "\n"
        "  current : " + current.provenance() + "\n"
        "Both fields change the trajectory, so the recorded artifact and this run are "
        "not the same model. Re-run with the recorded settings, or start a new "
        "experiment rather than resuming this one.");
}

inline std::string DiagnosticsConfig::provenance() const {
    std::string o = "trace_u_terms=";
    o += (trace_u_terms ? "1" : "0");
    o += " dump_advect_u_split=";
    o += (dump_advect_u_split ? "1" : "0");
    return o;
}

}  // namespace sdirk3
}  // namespace wrf

#endif  // WRF_SDIRK3_EXPERIMENT_CONFIG_H
