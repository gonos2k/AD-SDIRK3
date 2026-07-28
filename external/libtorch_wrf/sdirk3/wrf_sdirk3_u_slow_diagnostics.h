// wrf_sdirk3_u_slow_diagnostics.h -- observation of the u-momentum slow tendency.
//
// 9F.D30 (review PR S2-A). This exists to get configuration reading and diagnostic
// output OUT of computeUnifiedRHS. Deliberately minimal, per the review's own
// guidance: typed data carriers plus free functions, minimal and concrete. No
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

#include "wrf_sdirk3_diag_io.h"
#include "wrf_sdirk3_experiment_config.h"

#include <atomic>
#include <mutex>
#include <fstream>
#include <sstream>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <string>

namespace wrf {
namespace sdirk3 {

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
// delta. `kind` (NOT a string) identifies the site. NOTE the residual computed from
// these is a POST-CAPTURE TAIL guard, not site inventory -- an unnamed mutation
// between two captures is absorbed into the next site's delta. See the tail-guard
// comment in the emitter.
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

// 9F.D36 (review section 4): mutable diagnostic state belongs to the SOLVER, not the
// process. Config became per-solver in D33 while the record counter and the dump latch
// stayed function-local statics, so lifetime and ownership disagreed: a second solver
// in one process continued the first's record numbering, could never write its own
// dump, and a re-init never reset either.
struct DiagnosticsState {
    std::uint64_t uterms_record = 0;
    bool split_dump_written = false;
    bool experiment_announced = false;
    std::mutex dump_mutex;
};

// 9F.D35 (review section 5): a closure ALWAYS reports one of three states.
//
// Both closures were previously wrapped in `if (input.defined())`, so a refactor that
// stopped supplying an input produced NO line, NO error and NO marker -- the solver
// carried on and the log simply lacked a check nobody noticed was missing. That exact
// "claimed but silently absent" state has now occurred TWICE in this campaign, so the
// fix has to be structural rather than another comment.
//
// INVALID is deliberately distinct from FAIL: FAIL means the invariant was tested and
// violated; INVALID means it could not be tested at all. Collapsing them would let a
// missing input read as a passing run, which is the bug being closed.
//
// This does NOT abort the solver. Diagnostics must not alter the trajectory, so the
// status is reported and left for experiment acceptance to reject.
enum class ClosureStatus { Pass, Fail, Invalid };

inline const char* closure_status_name(ClosureStatus s) {
    switch (s) {
        case ClosureStatus::Pass:    return "PASS";
        case ClosureStatus::Fail:    return "FAIL";
        case ClosureStatus::Invalid: return "INVALID";
    }
    return "UNKNOWN";
}

// 9F.D34 (review section 4): production and the fixture must run the SAME capture
// code. The fixture previously hand-built a USlowTerms, so it verified only that the
// emitter formats a well-formed input correctly -- while the regression it was written
// for was in the WIRING (a dropped last_site_tendency assignment). A test that cannot
// see the wiring cannot guard the wiring.
struct USlowCaptureState {
    torch::Tensor previous;
    USlowTerms terms;
};

inline void capture_u_slow_site(USlowCaptureState& st,
                                USlowSiteKind kind,
                                const torch::Tensor& current) {
    if (!current.defined()) return;
    torch::NoGradGuard ng;
    auto snapshot = current.detach().clone();
    auto delta = st.previous.defined() ? (snapshot - st.previous) : snapshot;
    st.previous = snapshot;
    st.terms.sites.push_back({kind, delta});
    st.terms.last_site_tendency = snapshot;
    if (kind == USlowSiteKind::Advection) st.terms.adv_site_delta = delta;
}

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
// four times in this repo.
//
// 9F.D34 (review section 8): an earlier version of this note claimed "exactly one
// translation unit includes this". That is no longer true -- tests/test_u_slow_
// closures.cpp includes it as well -- so the "inline costs nothing" argument does not
// hold as stated. It is also true, as the review checked, that adding a .cpp costs
// ONE line in wrf_sdirk3_core_sources.txt plus the CI "22 production sources" counter,
// not the three coupled edits claimed earlier. Moving the implementation to a .cpp is
// the right next step; it is deliberately NOT bundled with the closure-semantics fix
// so that a behavioural change and a file move do not land in one commit.
// ---------------------------------------------------------------------------


inline void emit_u_slow_diagnostics(DiagnosticsState& st, const USlowTerms& t) {
    torch::NoGradGuard ng;
    // 9F.D36 (review section 10): an incomplete input is REPORTED, not skipped.
    // Returning silently is how a diagnostic ends up switched on and doing
    // nothing -- the failure this campaign keeps re-encountering.
    if (t.sites.empty() || !t.final_tendency.defined()) {
        std::ostringstream bad;
        bad << "[UTERMS] status=INVALID reason="
            << (t.sites.empty() ? "no_sites" : "missing_final_tendency") << "\n";
        emit_diag_block(bad.str());
        return;
    }
    const std::uint64_t n = ++st.uterms_record;

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

    // 9F.D33 (review section 5): the whole record is composed locally and emitted
    // ONCE. Per-line writes let two concurrent records interleave line-by-line,
    // which severs a header from the rows belonging to it -- and a decomposition
    // record read with the wrong header is worse than a missing one.
    std::ostringstream o;
    o << "[UTERMS] rhs=" << n << " sites=" << t.sites.size()
      << (have_u ? "" : " (u-shape mismatch: W suppressed)") << "\n";

    for (const auto& s : t.sites) {
        o << "[UTERMS]   " << site_name(s.kind)
                  << "  |dR|=" << l2(s.delta)
                  << "  max|dR|=" << mx(s.delta)
                  << "  W=" << work(s.delta) << "\n";
    }

    // 9F.D34 (review section 3): this is a POST-CAPTURE TAIL residual, not site
    // coverage. It was named and documented as coverage, which claims more than it
    // checks.
    //
    // What it detects: ru_tend mutating AFTER the last capture.
    // What it does NOT detect: an unnamed mutation BETWEEN two captures. If an
    // unnamed term H lands between captures j-1 and j, then
    //     T_j = T_{j-1} + H + P_j   =>   delta_j = H + P_j
    // so H is absorbed into the NEXT named site's delta and this residual is still
    // exactly 0. Proving every physical term has a named site would need each
    // ru_tend mutation to go through a capture API -- a much larger change than
    // naming the residual correctly, which is what is done here.
    //
    // The S2-A extraction dropped this check while the struct comment kept claiming
    // it -- an invariant asserted in prose and verified by nothing, which is the
    // worst of the three states (checked / unclaimed / claimed-but-unchecked).
    //
    // Compared against the last SNAPSHOT rather than a re-sum of the deltas: summing
    // float32 deltas reintroduces order-dependent roundoff, whereas snapshot-vs-final
    // is exactly zero when every mutation is captured.
    {
        ClosureStatus st = ClosureStatus::Invalid;
        const char* why = "missing_last_snapshot";
        double cov = 0.0;
        if (t.last_site_tendency.defined() && t.final_tendency.defined()) {
            cov = l2(t.final_tendency - t.last_site_tendency);
            st  = (cov == 0.0) ? ClosureStatus::Pass : ClosureStatus::Fail;
            why = "";
        }
        o << "[UTERMS]   post_capture_tail status=" << closure_status_name(st)
          << " |dR|=" << cov;
        if (*why) o << " reason=" << why;
        o << "   <- tail guard (NOT site inventory)\n";
    }

    {
        // Name WHICH input is missing: "INVALID" alone sends the reader hunting.
        const char* missing =
            !t.advection.x.defined()        ? "missing_adv_x" :
            !t.advection.y.defined()        ? "missing_adv_y" :
            !t.advection.vertical.defined() ? "missing_adv_z" :
            !t.adv_site_delta.defined()     ? "missing_adv_site_delta" : "";
        if (*missing) {
            o << "[UTERMS]     adv_closure status=INVALID reason=" << missing << "\n";
        }
    }
    if (t.advection.complete() && t.adv_site_delta.defined()) {
        struct Named { const char* n; torch::Tensor v; };
        const Named additive[] = {{"adv_x", t.advection.x},
                                  {"adv_y", t.advection.y},
                                  {"adv_z", t.advection.vertical}};
        torch::Tensor acc;
        for (const auto& e : additive) {
            acc = acc.defined() ? (acc + e.v) : e.v;
            o << "[UTERMS]     sub " << e.n
                      << "  |T|=" << l2(e.v)
                      << "  max|T|=" << mx(e.v)
                      << "  W=" << work(e.v) << "\n";
            // A term that is huge because of a boundary defect piles into one or two
            // levels; real advection is spread through the column. This is the
            // discriminator, and it is why raw magnitude must not be a verdict.
            if (e.v.dim() == 3) {
                o << "[UTERMS]       kprof " << e.n << ":";
                auto per_k = e.v.transpose(0, 1)
                                 .reshape({e.v.size(1), -1})
                                 .norm(2, 1)
                                 .to(torch::kCPU);
                auto a = per_k.accessor<float, 1>();
                for (int64_t k = 0; k < per_k.size(0); ++k) o << " " << a[k];
                o << "\n";
            }
        }
        // DERIVED: reported, never summed.
        auto hz = t.advection.horizontal();
        o << "[UTERMS]     derived horiz  |T|=" << l2(hz)
                  << "  W=" << work(hz) << "\n";
        const double den = l2(t.adv_site_delta);
        const double res = l2(acc - t.adv_site_delta);
        const ClosureStatus ast = (res == 0.0) ? ClosureStatus::Pass : ClosureStatus::Fail;
        o << "[UTERMS]     adv_closure status=" << closure_status_name(ast)
          << " |sum(additive)-d(adv)|=" << res
          << "  rel=" << (den > 0.0 ? res / den : 0.0) << "\n";
    }

    // ONE emission for the whole record. Must be the LAST statement: every branch
    // above only appends to the local stream, so an early exit here would compose a
    // record and print nothing -- which is exactly what happened when this call was
    // first placed mid-function, and what test_u_slow_closures.cpp caught.
    emit_diag_block(o.str());
}



// Notice that the port-side advect_u split dump was written. Kept here so the numeric
// function contains no diagnostic string formatting at all.
// 9F.D33 (review section 6): the DUMP ITSELF, not just its notice. Previously only
// the message moved out and the numeric function still did the enable test, the CPU
// conversion, the header construction, the ofstream and the write. Moving the string
// but leaving the I/O is not a separation.
// 9F.D36 (review section 3): FAIL-CLOSE. The previous version set the "already
// written" latch BEFORE any file operation, never checked is_open/good/flush/close,
// and emitted "port wrote ..." before the ofstream destructor ran. A failed or
// truncated write therefore produced a SUCCESS record and simultaneously blocked any
// retry -- a fabricated evidence record, which is the failure class this campaign
// exists to eliminate. The latch is now published only after a verified write.
inline bool dump_advect_u_split(DiagnosticsState& st,
                                const UAdvectionTerms& adv,
                                const std::string& path = "port_advect_u_split.bin") {
    if (!adv.complete()) return false;
    torch::NoGradGuard ng;
    // The first RHS evaluations return an all-zero u block; dumping one of those would
    // compare two zero fields and report perfect parity.
    if (adv.vertical.abs().max().to(torch::kCPU).item<double>() <= 0.0) return false;

    std::lock_guard<std::mutex> lock(st.dump_mutex);
    if (st.split_dump_written) return false;

    auto horizontal = adv.horizontal();
    // section 9: the file format is float32 and the reader assumes 3-D; convert and
    // check EXPLICITLY rather than relying on the caller's dtype happening to match.
    TORCH_CHECK(horizontal.dim() == 3 && adv.vertical.dim() == 3,
                "advect_u split dump expects 3-D fields");
    TORCH_CHECK(horizontal.sizes() == adv.vertical.sizes(),
                "advect_u split dump: horizontal/vertical shape mismatch");
    auto hh = horizontal.detach().to(torch::kCPU, torch::kFloat32).contiguous();
    auto vv = adv.vertical.detach().to(torch::kCPU, torch::kFloat32).contiguous();

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    TORCH_CHECK(f.is_open(), "advect_u split dump: cannot open ", path);
    const int64_t dims[3] = {hh.size(0), hh.size(1), hh.size(2)};
    f.write(reinterpret_cast<const char*>(dims), sizeof(dims));
    f.write(reinterpret_cast<const char*>(hh.data_ptr<float>()), hh.numel() * sizeof(float));
    f.write(reinterpret_cast<const char*>(vv.data_ptr<float>()), vv.numel() * sizeof(float));
    f.flush();
    TORCH_CHECK(f.good(), "advect_u split dump: write failed for ", path);
    f.close();
    TORCH_CHECK(f.good(), "advect_u split dump: close failed for ", path);

    st.split_dump_written = true;   // publish ONLY after a verified write

    std::ostringstream o;
    o << "[ADVECT_U_SPLIT] port wrote " << path << " shape=("
      << dims[0] << "," << dims[1] << "," << dims[2] << ")\n";
    emit_diag_block(o.str());
    return true;
}

}  // namespace sdirk3
}  // namespace wrf

#endif  // WRF_SDIRK3_U_SLOW_DIAGNOSTICS_H
