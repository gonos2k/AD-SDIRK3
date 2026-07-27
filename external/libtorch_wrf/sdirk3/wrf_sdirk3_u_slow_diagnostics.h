// wrf_sdirk3_u_slow_diagnostics.h -- observation of the u-momentum slow tendency.
//
// 9F.D30 (review PR S2-A). This exists to get configuration reading and diagnostic
// output OUT of computeUnifiedRHS. Deliberately minimal, per the review's own
// guidance: two config structs, one typed data carrier, one free function. No
// observer hierarchy, no virtual interfaces, no registry -- those would be
// over-design for a single diagnostic.
//
// THE ONE INVARIANT THAT MATTERS HERE:
//
//     ExperimentConfig  MAY change the trajectory.
//     DiagnosticsConfig MUST NOT change the trajectory.
//
// They are separate types precisely so that boundary cannot blur. This campaign has
// repeatedly been misled in both directions -- a "diagnostic" that silently changed
// an operand, and an "experiment" that turned out to be a no-op -- and a single
// merged config makes both mistakes easy to write and hard to see.

#ifndef WRF_SDIRK3_U_SLOW_DIAGNOSTICS_H
#define WRF_SDIRK3_U_SLOW_DIAGNOSTICS_H

#include <torch/torch.h>

#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>
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

// The additive decomposition of the u advective tendency. `horizontal` and `total`
// are DERIVED -- computed from the additive fields, never stored -- so they cannot be
// double-counted in a closure sum. That fact used to live in a string comparison
// against the label "horiz", where renaming the label silently changed the closure's
// meaning.
struct UAdvectionTerms {
    torch::Tensor x, y, vertical;

    bool complete() const {
        return x.defined() && y.defined() && vertical.defined();
    }
    torch::Tensor horizontal() const { return x + y; }
    torch::Tensor total() const { return x + y + vertical; }
};

// One accumulation site's contribution to ru_tend, captured as a telescoping snapshot
// delta. `label` names the site; the deltas sum to the final tendency by construction,
// which makes their residual a COVERAGE check (is a site unnamed?) rather than a
// correctness check.
// 9F.D32 (review section 4): site IDENTITY is an enum; the string is display only.
// `if (label == "adv")` was the same defect as the earlier `if (label != "horiz")` --
// renaming a display string still compiles and silently disables the closure that
// depends on it. Physics meaning must not live in text.
enum class USlowSiteKind {
    Entry, Advection, PressureGradient, Coriolis, Curvature,
    HorizontalDiffusion, VerticalDiffusion, Final
};

inline const char* site_name(USlowSiteKind k) {
    switch (k) {
        case USlowSiteKind::Entry:               return "entry";
        case USlowSiteKind::Advection:           return "adv";
        case USlowSiteKind::PressureGradient:    return "pgf";
        case USlowSiteKind::Coriolis:            return "coriolis";
        case USlowSiteKind::Curvature:           return "curvature";
        case USlowSiteKind::HorizontalDiffusion: return "hdiff";
        case USlowSiteKind::VerticalDiffusion:   return "vdiff";
        case USlowSiteKind::Final:               return "final";
    }
    return "unknown";
}

struct USlowSite {
    USlowSiteKind kind;
    torch::Tensor delta;
};

// Everything the u-slow diagnostic observes. Plain data: no policy, no I/O.
struct USlowTerms {
    UAdvectionTerms advection;
    std::vector<USlowSite> sites;   // in accumulation order
    torch::Tensor adv_site_delta;       // the advection site, for the advection closure
    torch::Tensor last_site_tendency;   // ru_tend AS CAPTURED at the last site
    torch::Tensor final_tendency;       // ru_tend at its last mutation
    torch::Tensor u;                // for the work projection sum(u * dR)
};

// Emits the u-slow decomposition. Pure observation: takes tensors, writes lines.
// Never mutates its inputs and never influences a solver decision.
//
// `work` here is sum(u * dR) where dR is the COUPLED d(mu*u)/dt, so it is NOT exactly
// dKE/dt (it equals mu*d(u^2/2)/dt + u^2*dmu/dt). Only its SIGN is meaningful -- does
// the term reinforce the existing u field or oppose it -- which is the thing a
// magnitude cannot tell you.


// ---------------------------------------------------------------------------
// Header-only ON PURPOSE. A separate .cpp would have to be added to
// wrf_sdirk3_core_sources.txt, to CMakeLists, and to the CI exact-set install
// contract -- three coupled edits, and the exact-set ratchet has silently rotted
// four times in this repo. Exactly one translation unit includes this, so inline
// costs nothing and the build system does not move at all.
// ---------------------------------------------------------------------------

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

inline void emit_u_slow_diagnostics(const USlowTerms& t) {
    if (t.sites.empty() || !t.final_tendency.defined()) return;

    torch::NoGradGuard ng;
    static std::atomic<long> call_no{0};
    const long n = call_no.fetch_add(1) + 1;

    auto l2 = [](const torch::Tensor& x) {
        return x.norm().to(torch::kCPU).item<double>();
    };
    auto mx = [](const torch::Tensor& x) {
        return x.abs().max().to(torch::kCPU).item<double>();
    };
    const bool have_u = t.u.defined() && t.u.sizes() == t.final_tendency.sizes();
    auto work = [&](const torch::Tensor& d) {
        return have_u ? (t.u * d).sum().to(torch::kCPU).item<double>() : 0.0;
    };

    std::cerr << "[UTERMS] rhs=" << n << " sites=" << t.sites.size()
              << (have_u ? "" : " (u-shape mismatch: W suppressed)") << std::endl;

    for (const auto& s : t.sites) {
        std::cerr << "[UTERMS]   " << site_name(s.kind)
                  << "  |dR|=" << l2(s.delta)
                  << "  max|dR|=" << mx(s.delta)
                  << "  W=" << work(s.delta) << std::endl;
    }

    // 9F.D32 (review section 1): COVERAGE closure, restored.
    //
    // The S2-A extraction dropped this check while the struct comment kept claiming
    // it -- an invariant asserted in prose and verified by nothing, which is the
    // worst of the three states (checked / unclaimed / claimed-but-unchecked).
    //
    // Compared against the last SNAPSHOT rather than a re-sum of the deltas: summing
    // float32 deltas reintroduces order-dependent roundoff, whereas snapshot-vs-final
    // is exactly zero when every mutation is captured. Nonzero here means a site
    // mutates ru_tend AFTER the last capture, i.e. this probe does not name it.
    if (t.last_site_tendency.defined()) {
        const double cov = l2(t.final_tendency - t.last_site_tendency);
        std::cerr << "[UTERMS]   unattributed |dR|=" << cov
                  << "   <- coverage check, must be 0" << std::endl;
    }

    if (t.advection.complete() && t.adv_site_delta.defined()) {
        struct Named { const char* n; torch::Tensor v; };
        const Named additive[] = {{"adv_x", t.advection.x},
                                  {"adv_y", t.advection.y},
                                  {"adv_z", t.advection.vertical}};
        torch::Tensor acc;
        for (const auto& e : additive) {
            acc = acc.defined() ? (acc + e.v) : e.v;
            std::cerr << "[UTERMS]     sub " << e.n
                      << "  |T|=" << l2(e.v)
                      << "  max|T|=" << mx(e.v)
                      << "  W=" << work(e.v) << std::endl;
            // A term that is huge because of a boundary defect piles into one or two
            // levels; real advection is spread through the column. This is the
            // discriminator, and it is why raw magnitude must not be a verdict.
            if (e.v.dim() == 3) {
                std::cerr << "[UTERMS]       kprof " << e.n << ":";
                auto per_k = e.v.transpose(0, 1)
                                 .reshape({e.v.size(1), -1})
                                 .norm(2, 1)
                                 .to(torch::kCPU);
                auto a = per_k.accessor<float, 1>();
                for (int64_t k = 0; k < per_k.size(0); ++k) std::cerr << " " << a[k];
                std::cerr << std::endl;
            }
        }
        // DERIVED: reported, never summed.
        auto hz = t.advection.horizontal();
        std::cerr << "[UTERMS]     derived horiz  |T|=" << l2(hz)
                  << "  W=" << work(hz) << std::endl;
        const double den = l2(t.adv_site_delta);
        const double res = l2(acc - t.adv_site_delta);
        std::cerr << "[UTERMS]     adv closure |sum(additive)-d(adv)|=" << res
                  << "  rel=" << (den > 0.0 ? res / den : 0.0) << std::endl;
    }
}



// Notice that the port-side advect_u split dump was written. Kept here so the numeric
// function contains no diagnostic string formatting at all.
inline void emit_split_dump_notice(int64_t nj, int64_t nk, int64_t ni) {
    std::cerr << "[ADVECT_U_SPLIT] port wrote port_advect_u_split.bin shape=("
              << nj << "," << nk << "," << ni << ")" << std::endl;
}

}  // namespace sdirk3
}  // namespace wrf

#endif  // WRF_SDIRK3_U_SLOW_DIAGNOSTICS_H
