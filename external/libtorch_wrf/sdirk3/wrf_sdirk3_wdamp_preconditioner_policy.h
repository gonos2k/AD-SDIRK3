#pragma once
// PR 9D: the authoritative W-damping operator/preconditioner policy.
//
// Scientific contract (proven by WDamp_Tangent_Contract's pure-w case):
// the WRF-parity W-damping term's smooth-region tangent flows through ww and
// mu (calc_ww_cp -> rw tendency); the physical w enters ONLY through the hard
// SIGN, whose derivative is 0 away from w == 0. So ∂term/∂w = 0 in the smooth
// region and the term has NO direct W-diagonal Jacobian there. The 1-D
// W-diagonal preconditioner therefore MUST NOT add a scalar w_damp_alpha to
// the W diagonal to "mirror" an enabled RHS W-damping term: that diagonal
// models a physics tangent that does not exist. (The real smooth Jacobian is
// the u/v/mu -> rw cross-block, which a 1-D W diagonal cannot represent; a
// scalar alpha on the W diagonal is not a valid approximation of it.)
//
// Consequences enforced here:
//   physical_w_diagonal           == 0  ALWAYS (never mirrors the RHS config)
//   extra_regularization_alpha    == w_damp_alpha  IFF precond_extra_wdamp
//
// precond_extra_wdamp is an EXPLICIT, deliberately non-operator
// regularization. It does not mirror the WRF W-damping Jacobian. It is the
// ONLY gate that puts a scalar on the W diagonal.
#include <cmath>
#include <stdexcept>
#include "wrf_sdirk3_config.h"

namespace wrf {
namespace sdirk3 {

// Config-level activation of the RHS W-damping COMPUTE PATH. This is NOT a
// pointwise activation: even when config-enabled, zero cells may pass the CFL
// gate (that dynamic fact is `active_count`, tracked separately — never
// collapse the two into one "active" predicate). This is the single
// authority for "the RHS is configured to compute W-damping".
inline bool wdamp_rhs_config_enabled(const SDIRK3Config& config) noexcept {
    return config.wrf_w_damping == 1 && config.implicit_wdamp &&
           config.wrf_w_crit_cfl > 0.0f;
}

struct WdampPreconditionerPolicy {
    // The RHS compute path is configured on (telemetry / signature only —
    // it does NOT justify any W diagonal).
    bool rhs_config_enabled = false;
    // WRF-parity smooth tangent has no direct W diagonal: this is ALWAYS 0.
    float physical_w_diagonal = 0.0f;
    // Explicit, non-operator regularization (precond_extra_wdamp).
    bool extra_regularization = false;
    float extra_regularization_alpha = 0.0f;
};

// Resolve the policy from config. Throws a stable marker if an explicit
// regularization requests a non-finite or negative alpha (a negative diagonal
// would be destabilizing, not a regularization).
inline WdampPreconditionerPolicy resolve_wdamp_preconditioner_policy(
    const SDIRK3Config& config) {
    WdampPreconditionerPolicy p;
    p.rhs_config_enabled = wdamp_rhs_config_enabled(config);
    p.physical_w_diagonal = 0.0f;  // never mirror the RHS config
    p.extra_regularization = config.precond_extra_wdamp;
    if (config.precond_extra_wdamp) {
        const float a = config.w_damp_alpha;
        if (!std::isfinite(a) || a < 0.0f) {
            throw std::invalid_argument(
                "SDIRK3_WDAMP_PRECOND_INVALID_ALPHA: precond_extra_wdamp "
                "regularization requires a finite, non-negative w_damp_alpha");
        }
        p.extra_regularization_alpha = a;
    } else {
        p.extra_regularization_alpha = 0.0f;
    }
    return p;
}

// The coefficient-cache signature: any change here MUST rebuild the
// preconditioner coefficients. normalized_extra_alpha is 0 when the extra
// regularization is off, so a w_damp_alpha change with extra OFF leaves the
// coefficients untouched (it is not consumed).
struct WdampPreconditionerSignature {
    bool rhs_config_enabled = false;
    bool extra_regularization = false;
    float normalized_extra_alpha = 0.0f;

    bool operator==(const WdampPreconditionerSignature& o) const {
        return rhs_config_enabled == o.rhs_config_enabled &&
               extra_regularization == o.extra_regularization &&
               normalized_extra_alpha == o.normalized_extra_alpha;
    }
    bool operator!=(const WdampPreconditionerSignature& o) const {
        return !(*this == o);
    }
};

inline WdampPreconditionerSignature wdamp_preconditioner_signature(
    const WdampPreconditionerPolicy& p) {
    WdampPreconditionerSignature s;
    s.rhs_config_enabled = p.rhs_config_enabled;
    s.extra_regularization = p.extra_regularization;
    s.normalized_extra_alpha = p.extra_regularization_alpha;  // 0 unless extra
    return s;
}

}  // namespace sdirk3
}  // namespace wrf
