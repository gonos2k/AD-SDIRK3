// 9F.D32: fixtures that REJECT a violated closure.
//
// WHY THIS EXISTS. The S2-A extraction silently dropped the site-coverage closure
// while the struct comment kept claiming it. Nothing failed, because nothing tested
// it -- an invariant asserted in prose and verified by nothing. That is the worst of
// the three states (checked / unclaimed / claimed-but-unchecked), and it is a
// recurring class in this campaign.
//
// So each closure gets a fixture built to VIOLATE it. A test that only demonstrates
// the happy path would not have caught the regression it exists to prevent: the
// happy path still printed, it just printed one fewer line.
//
// Deliberately standalone: no WRF, no MPI, sub-second, so it can gate in hosted CI.

#include "../wrf_sdirk3_u_slow_diagnostics.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;
    if (!ok) ++failures;
}

// Capture what the emitter writes so the assertions are about OBSERVED output, not
// about internal state the emitter might not actually report.
std::string capture(const wrf::sdirk3::USlowTerms& t) {
    std::ostringstream buf;
    auto* old = std::cerr.rdbuf(buf.rdbuf());
    wrf::sdirk3::emit_u_slow_diagnostics(t);
    std::cerr.rdbuf(old);
    return buf.str();
}

// 9F.D34 (review section 12): strtod with an end-pointer check, not atof. atof
// returns 0 for malformed text and cannot report failure, so an emitter printing
// "post_capture_tail |dR|=INVALID" would parse as 0.0 and PASS the very assertion
// meant to catch it. -2.0 marks unparseable so it can never look like a clean zero.
double number_after(const std::string& out, const char* key) {
    const auto p = out.find(key);
    if (p == std::string::npos) return -1.0;              // not reported at all
    const char* begin = out.c_str() + p + std::strlen(key);
    char* end = nullptr;
    const double v = std::strtod(begin, &end);
    if (end == begin) return -2.0;                        // present but unparseable
    return v;
}

double coverage_from(const std::string& out) {
    return number_after(out, "post_capture_tail |dR|=");
}

double adv_closure_from(const std::string& out) {
    return number_after(out, "|sum(additive)-d(adv)|=");
}

// 9F.D34 (review section 4): build through the SHARED capture helper, exactly as
// production does. Hand-building a USlowTerms tested only the emitter's formatting,
// while the regression this file exists for lived in the WIRING.
wrf::sdirk3::USlowCaptureState make_consistent() {
    using namespace wrf::sdirk3;
    const auto opt = torch::TensorOptions().dtype(torch::kFloat32);
    auto x = torch::randn({4, 3, 5}, opt);
    auto y = torch::randn({4, 3, 5}, opt);
    auto z = torch::randn({4, 3, 5}, opt);

    torch::Tensor ru = torch::zeros({4, 3, 5}, opt);
    USlowCaptureState st;
    capture_u_slow_site(st, USlowSiteKind::Entry, ru);     // entry: all zero
    ru = ru + (x + y + z);                                 // the advection site
    capture_u_slow_site(st, USlowSiteKind::Advection, ru);

    st.terms.advection = {x, y, z};
    st.terms.final_tendency = ru;
    st.terms.u = torch::randn({4, 3, 5}, opt);
    return st;
}

}  // namespace

int main() {
    using namespace wrf::sdirk3;
    torch::manual_seed(0);

    // --- positive control: a consistent decomposition reports BOTH closures at 0 ---
    {
        auto st = make_consistent();
        const auto out = capture(st.terms);
        check(coverage_from(out) == 0.0,    "consistent -> tail guard reported and 0");
        check(adv_closure_from(out) == 0.0, "consistent -> advection closure reported and 0");
    }

    // --- the regression this file exists for: a mutation AFTER the last capture ---
    {
        auto st = make_consistent();
        auto& t = st.terms;
        t.final_tendency = t.final_tendency + torch::full_like(t.final_tendency, 0.25f);
        const auto out = capture(t);
        const double cov = coverage_from(out);
        check(cov > 0.0, "unnamed post-capture mutation -> coverage closure NONZERO");
        check(cov > 0.0, "tail guard is actually REPORTED (regression guard)");
    }

    // --- advection closure must reject a dropped subterm ---
    {
        auto st = make_consistent();
        auto& t = st.terms;
        t.advection.vertical = torch::zeros_like(t.advection.vertical);  // drop adv_z
        const auto out = capture(t);
        check(adv_closure_from(out) > 0.0, "dropped adv_z -> advection closure NONZERO");
    }

    // --- the derived aggregate must never enter the additive sum ---
    {
        auto st = make_consistent();
        const auto out = capture(st.terms);
        check(out.find("derived horiz") != std::string::npos,
              "derived horizontal is reported");
        check(adv_closure_from(out) == 0.0,
              "derived horizontal is NOT double-counted in the closure");
    }

    // --- PIN the known LIMITATION (review section 3) ---
    // An unnamed mutation BETWEEN two captures is absorbed into the next site's delta,
    // so the tail guard stays 0. This is asserted deliberately: the residual is a tail
    // guard, NOT site inventory, and pinning it stops the weaker property being re-read
    // as the stronger one. If a future change makes this detectable, this test SHOULD
    // fail and be updated -- that would be an improvement, not a regression.
    {
        using namespace wrf::sdirk3;
        const auto opt = torch::TensorOptions().dtype(torch::kFloat32);
        auto x = torch::randn({4, 3, 5}, opt);
        torch::Tensor ru = torch::zeros({4, 3, 5}, opt);
        USlowCaptureState st;
        capture_u_slow_site(st, USlowSiteKind::Entry, ru);
        ru = ru + torch::full_like(ru, 7.0f);   // UNNAMED intermediate mutation
        ru = ru + x;                            // then a named site
        capture_u_slow_site(st, USlowSiteKind::Advection, ru);
        st.terms.advection = {x, torch::zeros_like(x), torch::zeros_like(x)};
        st.terms.final_tendency = ru;
        st.terms.u = torch::randn({4, 3, 5}, opt);
        const auto out = capture(st.terms);
        check(coverage_from(out) == 0.0,
              "KNOWN LIMIT: unnamed mutation BETWEEN captures is NOT detected");
    }

    if (failures == 0) {
        std::cout << "U_SLOW_CLOSURES: PASS" << std::endl;
        return 0;
    }
    std::cout << "U_SLOW_CLOSURES: FAIL (" << failures << ")" << std::endl;
    return 1;
}
