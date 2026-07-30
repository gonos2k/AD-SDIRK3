// wrf_sdirk3_hydrostatic_balance.h -- the discrete hydrostatic residual, as one function.
//
// 9F.D48 (review section 3). WRF constructs its base state so that the MODEL's discrete
// vertical operator is in exact balance, not so that a continuous relation holds. The
// residual that expresses that is
//
//     R_H,k = rdnw_k * (phb_{k+1} - phb_k) + alb_k * (c1h_k * mub + c2h_k)
//
// and WRF's ideal_init_method=2 integrates phb directly from alb to make it vanish
// (dyn_em/start_em.F). MEASURED on em_b_wave/wrfinput_d01 with WRF's own fields:
//
//     WRF signed rdnw (-201.3 .. -28.1):  mean|R| / mean|term| = 9.5e-07   <- float32 eps
//     |rdnw| (magnitude convention):      mean|R| / mean|term| = 2.000     <- terms ADD
//
// The 2.000 is the point. Getting the eta orientation wrong does not degrade the balance
// slightly; it makes the two terms add instead of cancel, so the residual becomes exactly
// twice the term magnitude. That is a signature worth recognising: a hydrostatic residual
// of ~2 relative means a sign, not a discretisation error.
//
// This is also why the vertical-metric contract matters (see the authority block in
// wrf_sdirk3_tile_unified_impl.cpp): C++ stores |rdnw| and the OPERATOR must supply the
// orientation. A stale comment in this repo once claimed "all vertical derivatives use
// positive rdnw/rdn directly" -- believing it produces exactly the 2.000 failure above.
//
// Torch-free and templated so a contract test can run it in double (to separate a real
// imbalance from float32 accumulation) and in float (to match what the model computes).

#ifndef WRF_SDIRK3_HYDROSTATIC_BALANCE_H
#define WRF_SDIRK3_HYDROSTATIC_BALANCE_H

#include <cmath>
#include <cstddef>
#include <vector>

namespace wrf {
namespace sdirk3 {

template <typename T>
struct HydrostaticResidual {
    std::vector<T> per_level;     // R_H at each mass level
    T max_abs      = T(0);
    T rms          = T(0);
    T mean_abs     = T(0);
    T mean_term    = T(0);        // mean |alb*(c1h*mub + c2h)|, the natural scale
    // RELATIVE measure. Use this, not max_abs: phb is integrated upward from the
    // surface, so roundoff accumulates with height and the absolute residual grows even
    // for a perfectly balanced state (measured: 0 at k=0 rising to ~0.7 at k=63, against
    // terms growing 7.3e4 -> 5.4e5). An absolute bound would spuriously fail at the top.
    T relative     = T(0);
};

// rdnw must be passed with WRF's OWN SIGN (negative). Passing |rdnw| is what the
// contract test uses as its built-in negative control.
template <typename T>
HydrostaticResidual<T> hydrostatic_residual(
    const std::vector<T>& rdnw,    // [nk]   WRF-signed
    const std::vector<T>& phb,     // [nk+1] base geopotential on w-levels
    const std::vector<T>& alb,     // [nk]   base inverse density
    const std::vector<T>& c1h,     // [nk]
    const std::vector<T>& c2h,     // [nk]
    T mub)                         // base column mass at this column
{
    const std::size_t nk = alb.size();
    HydrostaticResidual<T> out;
    out.per_level.resize(nk);

    T sum_sq = T(0), sum_abs = T(0), sum_term = T(0);
    for (std::size_t k = 0; k < nk; ++k) {
        const T term = alb[k] * (c1h[k] * mub + c2h[k]);
        const T r    = rdnw[k] * (phb[k + 1] - phb[k]) + term;
        out.per_level[k] = r;
        const T a = std::abs(r);
        if (a > out.max_abs) out.max_abs = a;
        sum_sq   += r * r;
        sum_abs  += a;
        sum_term += std::abs(term);
    }
    const T n = static_cast<T>(nk);
    out.rms       = std::sqrt(sum_sq / n);
    out.mean_abs  = sum_abs / n;
    out.mean_term = sum_term / n;
    out.relative  = (out.mean_term > T(0)) ? out.mean_abs / out.mean_term : T(0);
    return out;
}

// Build a base state that is in EXACT discrete balance, by inverting R_H = 0:
//     phb_{k+1} = phb_k - alb_k*(c1h_k*mub + c2h_k) / rdnw_k
// This is what WRF's ideal_init_method=2 does. Note rdnw < 0 and alb > 0, so the
// increment is POSITIVE -- geopotential increases upward, as it must.
template <typename T>
std::vector<T> integrate_phb_hydrostatic(
    const std::vector<T>& rdnw, const std::vector<T>& alb,
    const std::vector<T>& c1h,  const std::vector<T>& c2h,
    T mub, T phb_surface)
{
    const std::size_t nk = alb.size();
    std::vector<T> phb(nk + 1);
    phb[0] = phb_surface;
    for (std::size_t k = 0; k < nk; ++k) {
        phb[k + 1] = phb[k] - alb[k] * (c1h[k] * mub + c2h[k]) / rdnw[k];
    }
    return phb;
}

}  // namespace sdirk3
}  // namespace wrf

#endif  // WRF_SDIRK3_HYDROSTATIC_BALANCE_H
