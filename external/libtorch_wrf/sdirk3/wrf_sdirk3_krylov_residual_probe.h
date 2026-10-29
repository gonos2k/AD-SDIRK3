#pragma once
#include <cmath>

namespace wrf::sdirk3 {
// One estimate-qualified confirmation per restart. Scheduled checks still run;
// a coincident scheduled check consumes the allowance to avoid probing again
// immediately after its true residual rejected an optimistic estimate.
class KrylovResidualProbe {
public:
    explicit KrylovResidualProbe(bool enabled) : enabled_(enabled) {}
    bool pending() const { return enabled_ && !used_; }
    bool request(bool scheduled, double relative_estimate, double tolerance) {
        if (!pending() || !std::isfinite(relative_estimate) || relative_estimate < 0.0 ||
            !(tolerance > 0.0) || !(relative_estimate < tolerance)) return false;
        used_ = true;
        return !scheduled;
    }
private:
    bool enabled_;
    bool used_ = false;
};
} // namespace wrf::sdirk3
