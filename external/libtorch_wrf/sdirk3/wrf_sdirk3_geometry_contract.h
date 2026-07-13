// wrf_sdirk3_geometry_contract.h
//
// The SINGLE geometry contract for the SDIRK3 solver (external review
// rounds 3g/3h/3i/3j — promoted to a shared, testable predicate in the
// post-approval geometry-CI increment). Deliberately torch-free AND
// throw-free so the regression matrix (test_geometry_contract.cpp) compiles
// and runs standalone in seconds, and so production and test exercise the
// EXACT same predicate (production wraps it with a log + throw).
//
// CONTRACT — a per-call geometry is accepted iff ALL of:
//   (1) call_sane      : every call dimension is in (0, kGeomMaxDim]
//   (2) call_stagger_ok: each staggered extent is a valid C-grid shape of its
//                        base — collocated (==base) OR +1 (base+1). This is
//                        the MEMORY-SAFETY layer; it deliberately accepts
//                        collocated. The split-explicit FEATURE separately
//                        requires the +1 whole-domain shape (se_stagger_ok in
//                        the split guard) — a different layer, a different
//                        invariant.
//   (3) init_sane      : the solver's INITIALIZED geometry is itself valid —
//                        NO "uninitialized => allow" escape (round 3j: a
//                        zero/negative base member previously DISABLED the
//                        equality check entirely).
//   (4) matches_init   : the call extents EQUAL the initialized geometry (all
//                        solver tensors are sized from init; a mid-run
//                        geometry change is unsupported on every path).
//
// All arithmetic is 64-bit: the old int expressions (config.nx + 1 etc.) were
// computed BEFORE the range check and overflowed (UB) near INT_MAX. base + 1
// below is safe because base is first proven <= kGeomMaxDim.

#ifndef WRF_SDIRK3_GEOMETRY_CONTRACT_H
#define WRF_SDIRK3_GEOMETRY_CONTRACT_H

namespace wrf {
namespace sdirk3 {

// Upper bound for any single grid dimension in the per-call contract. (The
// constructor / setStaggeredDimensions keep their own, stricter 10000 bound;
// this is the ABI-boundary contract limit and must not be conflated with them.)
constexpr long long kGeomMaxDim = 100000;

inline bool geom_dim_in_range(long long d) {
    return d > 0 && d <= kGeomMaxDim;
}

// A staggered extent is either collocated (== base) or one larger (base + 1),
// the only two C-grid possibilities. Requires both operands in range first,
// so base + 1 cannot overflow.
inline bool geom_stagger_shape_ok(long long base, long long stag) {
    return geom_dim_in_range(base) && geom_dim_in_range(stag) &&
           stag >= base && stag <= base + 1;
}

struct GeometryCheck {
    bool call_sane = false;
    bool call_stagger_ok = false;
    bool init_sane = false;
    bool matches_init = false;
    bool ok() const {
        return call_sane && call_stagger_ok && init_sane && matches_init;
    }
};

// Evaluate the full per-call contract against the solver's initialized
// geometry. Pure — no logging, no throwing (production wraps this).
inline GeometryCheck check_call_geometry(
    long long nx, long long ny, long long nz,
    long long nx_u, long long ny_v, long long nz_w,
    long long i_nx, long long i_ny, long long i_nz,
    long long i_nx_u, long long i_ny_v, long long i_nz_w) {
    GeometryCheck r;
    r.call_sane =
        geom_dim_in_range(nx) && geom_dim_in_range(ny) && geom_dim_in_range(nz) &&
        geom_dim_in_range(nx_u) && geom_dim_in_range(ny_v) && geom_dim_in_range(nz_w);
    r.call_stagger_ok = r.call_sane &&
        geom_stagger_shape_ok(nx, nx_u) &&
        geom_stagger_shape_ok(ny, ny_v) &&
        geom_stagger_shape_ok(nz, nz_w);
    r.init_sane =
        geom_dim_in_range(i_nx) && geom_dim_in_range(i_ny) && geom_dim_in_range(i_nz) &&
        geom_dim_in_range(i_nx_u) && geom_dim_in_range(i_ny_v) && geom_dim_in_range(i_nz_w);
    r.matches_init = r.init_sane &&
        nx == i_nx && ny == i_ny && nz == i_nz &&
        nx_u == i_nx_u && ny_v == i_ny_v && nz_w == i_nz_w;
    return r;
}

}  // namespace sdirk3
}  // namespace wrf

#endif  // WRF_SDIRK3_GEOMETRY_CONTRACT_H
