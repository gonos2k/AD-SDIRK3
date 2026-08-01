// 9F.D51: the vertical-metric policy, as a test rather than a comment (review section 4).
//
// WHAT WAS ACTUALLY WRONG. D48 made the SCALAR extraction path fail closed on NaN/Inf/zero
// and left SEVEN tensor sources still doing where(isfinite(x), abs(x), 1e-10): grid rdnw
// (twice -- there were two copies of the same block), grid dnw, grid rdn used as rdnw
// (twice), getRdnTensor's grid rdn, and dn. So the effective contract was "some metric
// sources fail closed, others are quietly invented", and which one you got depended on
// which branch of a five-way runtime priority chain won.
//
// WHY eps IS WORSE THAN THE NaN IT REPLACES. These are RECIPROCAL metrics: substituting
// 1e-10 asserts a layer 1e10 units thick. Invalid geometry becomes a finite, extreme,
// plausible-looking atmosphere -- and plausible-looking output is exactly what this
// campaign keeps having to disprove. A NaN at least announces itself. Two of the sites
// were worse still: they clamped |dnw| < eps UP to eps, so a genuinely thin layer became
// a 1e10-thick one on the reciprocal.
//
// This file can exist at all only because the policy moved out of an anonymous namespace
// in a 38k-line .cpp and into wrf_sdirk3_metric_policy.h. That is the point: a fail-close
// nobody can call is a fail-close nobody has checked.

#include "../wrf_sdirk3_metric_policy.h"

#include <torch/torch.h>

#include <cmath>
#include <iostream>
#include <functional>
#include <limits>
#include <vector>
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

// Did calling f() throw invalid_argument? Returns the message so the test can assert that
// the diagnostic actually names the problem -- a fail-close that says "error" is only
// half of one, since the next person to hit it has to re-derive which metric broke.
bool throws_with(const std::function<void()>& f, std::string& msg) {
    try { f(); } catch (const std::invalid_argument& e) { msg = e.what(); return true; }
    catch (...) { msg = "<wrong exception type>"; return false; }
    msg = "<did not throw>";
    return false;
}

torch::Tensor col(std::vector<float> v) {
    return torch::tensor(v, torch::TensorOptions().dtype(torch::kFloat32));
}

}  // namespace

int main() {
    using wrf::sdirk3::require_metric_magnitude;
    using wrf::sdirk3::require_metric_magnitude_tensor;

    const float kNaN = std::numeric_limits<float>::quiet_NaN();
    const float kInf = std::numeric_limits<float>::infinity();

    // --- the happy path: WRF's NEGATIVE metric becomes the magnitude C++ stores ---
    {
        auto out = require_metric_magnitude_tensor(col({-8.0f, -16.0f, -32.0f}), "rdnw");
        const bool ok = (out.min().item<float>() > 0.0f) &&
                        std::abs(out[1].item<float>() - 16.0f) < 1e-6f;
        check(ok, "WRF-signed input returns positive magnitudes unchanged in size");
    }

    // --- scalar and tensor forms must AGREE, or the sweep did not unify anything ---
    // This is the check that would have failed before D51: the two paths had different
    // policies, and nothing compared them.
    {
        const std::vector<float> vals{-8.0f, -16.0f, -32.0f, -64.0f};
        auto t = require_metric_magnitude_tensor(col(vals), "rdnw");
        bool agree = true;
        for (size_t k = 0; k < vals.size(); ++k) {
            if (require_metric_magnitude(vals[k], "rdnw") !=
                t[static_cast<int64_t>(k)].item<float>()) agree = false;
        }
        check(agree, "the scalar and tensor forms agree elementwise -- ONE policy, not two");
    }

    // --- NaN, Inf and zero must all REFUSE, and say which ---
    {
        std::string m;
        check(throws_with([&]{ require_metric_magnitude_tensor(col({-8.0f, kNaN, -32.0f}), "rdnw"); }, m) &&
              m.find("NaN/Inf") != std::string::npos,
              "a NaN anywhere throws and the message names it");
        check(throws_with([&]{ require_metric_magnitude_tensor(col({-8.0f, kInf, -32.0f}), "rdnw"); }, m) &&
              m.find("NaN/Inf") != std::string::npos,
              "an Inf anywhere throws and the message names it");
        check(throws_with([&]{ require_metric_magnitude_tensor(col({-8.0f, 0.0f, -32.0f}), "rdnw"); }, m) &&
              m.find("zero") != std::string::npos,
              "a ZERO throws too: 1/0 is not a layer thickness either");
        check(throws_with([&]{ require_metric_magnitude_tensor(col({kNaN, -8.0f}), "rdnw"); }, m) &&
              m.find("rdnw") != std::string::npos,
              "and the message names WHICH metric, so the next reader need not re-derive it");
    }

    // --- THE REGRESSION GUARD: eps must never be substituted ---
    // Stated as the property rather than as "it throws", because the failure mode being
    // prevented is a RETURN, not an exception: the old code returned 1e-10 and carried on.
    // If someone reinstates that, this catches it even if they keep the throw for NaN.
    {
        bool returned_a_value = false;
        try {
            auto out = require_metric_magnitude_tensor(col({-8.0f, kNaN, -32.0f}), "rdnw");
            returned_a_value = true;
            (void)out;
        } catch (const std::invalid_argument&) {}
        check(!returned_a_value,
              "a broken metric produces NO tensor at all -- eps=1e-10 would assert a layer "
              "1e10 units thick, i.e. invalid geometry disguised as an extreme atmosphere");
    }

    // --- the rdn/dn sentinel: k=0 is exempt, k>=1 is not ---
    // WRF's rdn(1) is undefined and this code forces it to 0 deliberately. A blanket
    // non-zero check would therefore reject every valid rdn array, which is how a
    // fail-close policy gets reverted six months later for being "too strict".
    {
        auto out = require_metric_magnitude_tensor(col({0.0f, -8.0f, -16.0f}), "rdn",
                                                   /*skip_leading=*/1);
        check(out.numel() == 3 && out[0].item<float>() == 0.0f,
              "rdn with a ZERO sentinel at k=0 is accepted when skip_leading=1");

        std::string m;
        check(throws_with([&]{ require_metric_magnitude_tensor(col({0.0f, -8.0f, 0.0f}), "rdn",
                                                               /*skip_leading=*/1); }, m),
              "but a zero at k>=1 still throws: the exemption is the sentinel, not the check");
        check(throws_with([&]{ require_metric_magnitude_tensor(col({0.0f, -8.0f}), "rdnw",
                                                               /*skip_leading=*/0); }, m),
              "and rdnw gets NO exemption -- it is defined at every level");
    }

    // --- float64 works too, since nothing here assumes float32 ---
    {
        auto d = torch::tensor({-8.0, -16.0}, torch::TensorOptions().dtype(torch::kFloat64));
        auto out = require_metric_magnitude_tensor(d, "rdnw");
        check(out.scalar_type() == torch::kFloat64 && out[0].item<double>() == 8.0,
              "dtype is preserved: float64 in, float64 out");
    }

    // --- it must NOT mutate its input ---
    // grid_info_->rdnw is a ZERO-COPY view of WRF's own array. An in-place abs_() here
    // would silently rewrite Fortran's memory, destroying the sign WRF itself needs on the
    // next step -- a corruption with no C++-side symptom at all. `.abs()` is correct and
    // `.abs_()` is a one-character disaster, so the distinction gets a test.
    {
        auto src = col({-8.0f, -16.0f});
        auto out = require_metric_magnitude_tensor(src, "rdnw");
        check(src[0].item<float>() == -8.0f && out[0].item<float>() == 8.0f,
              "the input tensor is UNCHANGED: grid metrics are zero-copy views of WRF's "
              "own memory, so abs_() in place would corrupt Fortran's array");
    }

    // --- 9F.D57: skip_leading RETURNS the skipped elements, unvalidated ---
    // This is the trap behind the review's section-3.2 finding, made explicit. A caller
    // that passes skip_leading and then USES element 0 as a metric gets whatever was
    // there. getRdnTensor overwrites it; the deleted rdn-as-rdnw fallback did not, and
    // shipped rdnw[0] = 0. Asserting the behaviour means the next person reads it here
    // instead of deriving it from a wrong answer.
    {
        const float kN = std::numeric_limits<float>::quiet_NaN();
        auto out = require_metric_magnitude_tensor(col({kN, -8.0f, -16.0f}), "rdn",
                                                   /*skip_leading=*/1);
        check(std::isnan(out[0].item<float>()),
              "skip_leading does NOT clean the skipped element -- a NaN at k=0 survives "
              "into the RETURN value, so the caller must overwrite it");
        check(out[1].item<float>() == 8.0f,
              "while the validated tail is converted normally, which is why this is a "
              "trap rather than an obvious bug");
    }

    constexpr int expected_checks = 14;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) { std::cout << "METRIC_POLICY: PASS" << std::endl; return 0; }
    std::cout << "METRIC_POLICY: FAIL (" << failures << ")" << std::endl;
    return 1;
}
