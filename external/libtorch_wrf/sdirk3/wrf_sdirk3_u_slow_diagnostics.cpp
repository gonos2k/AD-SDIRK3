// wrf_sdirk3_u_slow_diagnostics.cpp -- implementation of the u-slow decomposition
// diagnostic.
//
// 9F.D38 (review section 8). These bodies lived in the header. That header had grown
// to ~390 lines carrying typed carriers, capture wiring, tensor reductions, a work
// projection, a k-profile, output formatting, a record counter, binary file I/O and a
// dump-once policy -- it was becoming a second God file, one layer down from the one
// this campaign is dismantling.
//
// The header now declares; this file implements. Registration cost is one line in
// wrf_sdirk3_core_sources.txt (read by BOTH the CMake and Make builds) plus the CI
// source-count marker, which is why keeping it header-only was no longer worth it.

#include "wrf_sdirk3_u_slow_diagnostics.h"

#include "wrf_sdirk3_diag_io.h"

#include <iomanip>
#include <fstream>
#include <sstream>

namespace wrf {
namespace sdirk3 {

const char* site_name(USlowSiteKind k) {
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

const char* closure_status_name(ClosureStatus s) {
    switch (s) {
        case ClosureStatus::Pass:    return "PASS";
        case ClosureStatus::Fail:    return "FAIL";
        case ClosureStatus::Invalid: return "INVALID";
    }
    return "UNKNOWN";
}

void capture_u_slow_site(USlowCaptureState& st,
                                USlowSiteKind kind,
                                const torch::Tensor& current) {
    // 9F.D38 (review section 10): FAIL-CLOSE. A silent return meant a wiring
    // regression that left the tendency undefined produced no record, no INVALID
    // marker and no error -- the log simply lacked a site, which reads exactly like
    // a probe that never fired. That is the failure mode this whole fixture suite
    // exists to prevent, so it must not be reachable in the capture path itself.
    // Safe to throw: the diagnostic is opt-in and default-off, so the production
    // numerical path never reaches this.
    TORCH_CHECK(current.defined(),
                "U-slow diagnostic capture received an undefined tendency at site ",
                static_cast<int>(kind));
    torch::NoGradGuard ng;
    auto snapshot = current.detach().clone();
    auto delta = st.previous.defined() ? (snapshot - st.previous) : snapshot;
    st.previous = snapshot;
    st.terms.sites.push_back({kind, delta});
    st.terms.last_site_tendency = snapshot;
    if (kind == USlowSiteKind::Advection) st.terms.adv_site_delta = delta;
}

void emit_u_slow_diagnostics(DiagnosticsState& st, const USlowTerms& t) {
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
                // 9F.D38 (review section 9): convert to a KNOWN dtype. `.to(kCPU)`
                // preserves the source dtype, so accessor<float,1> was an unchecked
                // bet that the state is float32. Under a float64 fixture or an AD
                // verification state it throws -- a failure that appears ONLY when
                // the diagnostic is enabled, which is the worst time to discover it.
                auto per_k = e.v.transpose(0, 1)
                                 .reshape({e.v.size(1), -1})
                                 .norm(2, 1)
                                 .to(torch::kCPU, torch::kFloat64);
                auto a = per_k.accessor<double, 1>();
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

bool dump_advect_u_split(DiagnosticsState& st,
                         const UAdvectionTerms& adv,
                         const std::string& path) {
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
