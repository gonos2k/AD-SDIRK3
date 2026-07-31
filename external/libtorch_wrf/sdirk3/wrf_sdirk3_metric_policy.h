// wrf_sdirk3_metric_policy.h -- ONE policy for the vertical eta metric, scalar and tensor.
//
// 9F.D51 (review section 4). D48 made the scalar extraction path fail closed and left the
// tensor sources repairing silently, so the real contract was "some metric sources fail
// closed, others are quietly invented" -- a guarantee that depended on which branch of a
// five-way runtime priority chain happened to win. Seven sites did
// where(isfinite(x), abs(x), 1e-10): grid rdnw (twice), grid dnw, grid rdn used as rdnw
// (twice), getRdnTensor's grid rdn, and dn.
//
// WHY eps IS THE DANGEROUS PART, not the inconsistency. These are RECIPROCAL metrics. A
// substituted 1e-10 asserts a layer 1e10 units thick, so invalid geometry is disguised as
// a finite, extreme, plausible-looking atmosphere. Two of the dnw/dn sites were worse than
// the NaN substitution: they also clamped |dnw| < eps UP to eps, so a genuinely near-zero
// layer became a 1e10-thick one on the reciprocal. This campaign has spent months
// separating plausible-looking output from correct output; manufacturing more of the
// former is the last thing the solver needs.
//
// SIGN CONVENTION. WRF stores rdnw/rdn NEGATIVE (eta decreases with k); this code stores
// the magnitude and the OPERATORS re-apply the orientation. D50 found the hydrostatic
// pressure integrator had never taken that obligation up. Keeping the conversion in one
// named place is the cheap half of not repeating that.
//
// This lives in a header so a contract test can call the real policy. A policy asserted
// only in comments is not a policy.

#ifndef WRF_SDIRK3_METRIC_POLICY_H
#define WRF_SDIRK3_METRIC_POLICY_H

#include <torch/torch.h>

#include <cmath>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>

namespace wrf {
namespace sdirk3 {

// Scalar form: converts one WRF-signed metric to the magnitude C++ stores, or throws.
inline float require_metric_magnitude(float signed_value, const char* what) {
    if (!std::isfinite(signed_value) || signed_value == 0.0f) {
        std::ostringstream oss;
        oss << "SDIRK3 vertical metric " << what << " is not usable: got "
            << signed_value << " (expected a finite non-zero reciprocal metric; WRF "
            << "stores these NEGATIVE and C++ keeps the magnitude)";
        throw std::invalid_argument(oss.str());
    }
    return std::abs(signed_value);
}

// Tensor form: same policy, same message shape.
//
// COST. This validates on a REDUCTION, which syncs. It is affordable precisely because the
// eta metric is STATIC grid data: getRdnwTensor/getRdnTensor cache, so this runs on a cache
// rebuild, not per RHS. That is also the argument for the reviewer's immutable-snapshot
// design -- this function is the validation half of it, landed where the cache already is.
//
// NoGradGuard is mandatory: .item() breaks the autograd graph. Grid metrics are never
// grad-tracked, so the guard is correct as well as required.
//
// skip_leading exists for rdn/dn: WRF's rdn(1) is undefined, so k=0 is an explicit boundary
// sentinel that this code sets to 0 on purpose and must not judge as a zero metric. rdnw
// and dnw are defined at every level and pass skip_leading = 0.
//
// 9F.D57 -- READ THIS BEFORE USING skip_leading. The skipped elements are NOT validated
// AND ARE STILL RETURNED, as |x|. So a caller passing skip_leading = 1 gets back a tensor
// whose element 0 may be zero, NaN or garbage, and is responsible for overwriting it.
// getRdnTensor does exactly that (index_put_({0}, 0.0f) a few lines after each call).
//
// This asymmetry caused a real defect. The rdn-as-rdnw fallback called with
// skip_leading = 1 and used the result directly as rdnw, so WRF's undefined rdn(1) --
// deliberately zeroed here -- became rdnw[0] = 0, a zero reciprocal metric in a slot rdnw
// requires to be non-zero. The fallback is deleted (D57), but the trap is a property of
// this signature, so it is named here and asserted in Metric_Policy_Contract rather than
// left for the next caller to rediscover.
inline torch::Tensor require_metric_magnitude_tensor(const torch::Tensor& metric,
                                                     const char* what,
                                                     int64_t skip_leading = 0) {
    bool finite = true, nonzero = true;
    {
        torch::NoGradGuard no_grad;   // .item() below; metrics are static, never grad-tracked
        const auto checked = (skip_leading > 0 && metric.numel() > skip_leading)
                                 ? metric.slice(0, skip_leading)
                                 : metric;
        finite  = torch::isfinite(checked).all().item<bool>();
        nonzero = (checked != 0).all().item<bool>();
    }
    if (!finite || !nonzero) {
        std::ostringstream oss;
        oss << "SDIRK3 vertical metric " << what << " is not usable: "
            << (finite ? "" : "contains NaN/Inf; ")
            << (nonzero ? "" : "contains zero; ")
            << "expected finite non-zero reciprocal metrics (WRF stores these NEGATIVE "
            << "and C++ keeps the magnitude). Refusing to substitute eps=1e-10, which "
            << "would assert a layer 1e10 units thick.";
        throw std::invalid_argument(oss.str());
    }
    return metric.abs();
}

}  // namespace sdirk3
}  // namespace wrf

#endif  // WRF_SDIRK3_METRIC_POLICY_H
