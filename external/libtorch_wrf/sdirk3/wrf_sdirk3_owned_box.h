#ifndef WRF_SDIRK3_OWNED_BOX_H
#define WRF_SDIRK3_OWNED_BOX_H

// WHICH cells this rank owns, and where the tile view begins inside the memory domain.
//
// Two conventions the campaign has already been bitten by, written down once so they can be
// tested rather than re-derived:
//
// 1. THE TILE ORIGIN. Fortran hands the C++ side the MEMORY-domain base address --
//    C_LOC(grid%u_2(ims,kms,jms)), commented "u(ims,...) not u(its,...)" at
//    module_implicit_sdirk3.F:1287. The adapter then slices a tile view starting at
//    its-ims / jts-jms / kts-kms before unifiedStep ever sees a pointer, so by the time any
//    probe runs, local index 0 IS global its_/jts_. That is correct and it was an ASSUMPTION
//    stated in a comment -- the difference between the two is Memory_Tile_Offset_Contract.
//
// 2. THE STAGGERED EXTRA POINT IS ALREADY INSIDE ite/jte. A mass variable loops
//    its..min(ite, ide-1); a staggered one loops its..ite. Adding one for the domain edge
//    double-counts, which is exactly what happened on R12 R2's first run: u came back
//    42x81 where the domain has 41x80 x-staggered points. A wrong box shifts a norm by a few
//    percent and reads as "close enough"; a wrong COUNT is unmistakable, which is why the
//    count is emitted as the validity field.
//
// The global-norm probe DERIVES its loop extents from here and prints the same box, so the
// number and the box it claims to cover cannot drift apart -- and the offline combiner reads
// the box instead of reimplementing this rule in Python, because a reimplementation hides a
// disagreement between the two rather than surfacing it.

#include <algorithm>

namespace wrf {
namespace sdirk3 {

enum class Stagger {
    Mass,    // t, and the mass-point footprint generally
    X,       // u
    Y,       // v
    Z,       // w, ph
    Column   // mu -- one value per column
};

inline const char* stagger_name(Stagger s) {
    switch (s) {
        case Stagger::X:      return "x";
        case Stagger::Y:      return "y";
        case Stagger::Z:      return "z";
        case Stagger::Column: return "column";
        default:              return "mass";
    }
}

// Local index of the tile origin within the memory domain the Fortran pointer starts at.
struct TileOrigin {
    int i = 0, j = 0, k = 0;
};

inline TileOrigin tile_origin_in_memory(int its, int ims, int jts, int jms, int kts, int kms) {
    return TileOrigin{its - ims, jts - jms, kts - kms};
}

// Owned cells in GLOBAL indices, inclusive on both ends.
struct OwnedBox {
    int i0 = 0, i1 = -1, j0 = 0, j1 = -1, k0 = 0, k1 = -1;
    long long count() const {
        const long long ni = static_cast<long long>(i1) - i0 + 1;
        const long long nj = static_cast<long long>(j1) - j0 + 1;
        const long long nk = static_cast<long long>(k1) - k0 + 1;
        return (ni > 0 && nj > 0 && nk > 0) ? ni * nj * nk : 0;
    }
};

inline OwnedBox owned_box(Stagger s,
                          int its, int ite, int jts, int jte,
                          int ids, int ide, int jds, int jde,
                          int kds, int nz, int nz_w) {
    OwnedBox b;
    b.i0 = its;
    b.i1 = (s == Stagger::X) ? ite : std::min(ite, ide - 1);
    b.j0 = jts;
    b.j1 = (s == Stagger::Y) ? jte : std::min(jte, jde - 1);
    b.k0 = kds;
    switch (s) {
        case Stagger::Column: b.k1 = kds;              break;
        case Stagger::Z:      b.k1 = kds + nz_w - 1;   break;
        default:              b.k1 = kds + nz    - 1;  break;
    }
    return b;
}

}  // namespace sdirk3
}  // namespace wrf

#endif  // WRF_SDIRK3_OWNED_BOX_H
