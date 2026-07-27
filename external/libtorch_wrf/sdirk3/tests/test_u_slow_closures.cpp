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

double coverage_from(const std::string& out) {
    const auto p = out.find("unattributed |dR|=");
    if (p == std::string::npos) return -1.0;   // not reported at all
    return std::atof(out.c_str() + p + std::string("unattributed |dR|=").size());
}

double adv_closure_from(const std::string& out) {
    const auto p = out.find("|sum(additive)-d(adv)|=");
    if (p == std::string::npos) return -1.0;
    return std::atof(out.c_str() + p + std::string("|sum(additive)-d(adv)|=").size());
}

wrf::sdirk3::USlowTerms make_consistent() {
    using namespace wrf::sdirk3;
    const auto opt = torch::TensorOptions().dtype(torch::kFloat32);
    auto x = torch::randn({4, 3, 5}, opt);
    auto y = torch::randn({4, 3, 5}, opt);
    auto z = torch::randn({4, 3, 5}, opt);
    auto adv = x + y + z;

    USlowTerms t;
    t.advection = {x, y, z};
    t.adv_site_delta = adv;
    t.sites.push_back({USlowSiteKind::Entry, torch::zeros({4, 3, 5}, opt)});
    t.sites.push_back({USlowSiteKind::Advection, adv});
    t.last_site_tendency = adv;      // every mutation captured
    t.final_tendency = adv;
    t.u = torch::randn({4, 3, 5}, opt);
    return t;
}

}  // namespace

int main() {
    using namespace wrf::sdirk3;
    torch::manual_seed(0);

    // --- positive control: a consistent decomposition reports BOTH closures at 0 ---
    {
        auto t = make_consistent();
        const auto out = capture(t);
        check(coverage_from(out) == 0.0,   "consistent -> coverage closure reported and 0");
        check(adv_closure_from(out) == 0.0, "consistent -> advection closure reported and 0");
    }

    // --- the regression this file exists for: a mutation AFTER the last capture ---
    {
        auto t = make_consistent();
        t.final_tendency = t.final_tendency + torch::full_like(t.final_tendency, 0.25f);
        const auto out = capture(t);
        const double cov = coverage_from(out);
        check(cov > 0.0, "unnamed post-capture mutation -> coverage closure NONZERO");
        check(cov >= 0.0, "coverage closure is actually REPORTED (regression guard)");
    }

    // --- advection closure must reject a dropped subterm ---
    {
        auto t = make_consistent();
        t.advection.vertical = torch::zeros_like(t.advection.vertical);  // drop adv_z
        const auto out = capture(t);
        check(adv_closure_from(out) > 0.0, "dropped adv_z -> advection closure NONZERO");
    }

    // --- the derived aggregate must never enter the additive sum ---
    {
        auto t = make_consistent();
        const auto out = capture(t);
        check(out.find("derived horiz") != std::string::npos,
              "derived horizontal is reported");
        check(adv_closure_from(out) == 0.0,
              "derived horizontal is NOT double-counted in the closure");
    }

    if (failures == 0) {
        std::cout << "U_SLOW_CLOSURES: PASS" << std::endl;
        return 0;
    }
    std::cout << "U_SLOW_CLOSURES: FAIL (" << failures << ")" << std::endl;
    return 1;
}
