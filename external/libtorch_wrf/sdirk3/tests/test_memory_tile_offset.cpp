// R13 D1: the memory-domain -> tile-domain offset, and the owned box, as a contract.
//
// THE QUESTION THE REVIEW COULD NOT CLOSE. The global-norm probe sums "over the owned box",
// taking local index 0 to be global its_/jts_. Fortran, though, passes the MEMORY-domain base
// address -- C_LOC(grid%u_2(ims,kms,jms)), and the comment at module_implicit_sdirk3.F:1287
// says so explicitly ("u(ims,...) not u(its,...)"). Reading only that side, the helper looks
// off by the halo width. Reading only the C++ side, it looks fine. The disjunction resolves in
// the adapter: it slices a tile view at its-ims / jts-jms / kts-kms BEFORE unifiedStep sees a
// pointer, so by then local index 0 really is global its_. The helper is correct.
//
// Correct and unproven is what this file changes. A one-line edit to i_start -- or a future
// caller that hands over the patch instead of the tile -- moves every norm by a halo width,
// and a norm that is a few percent off reads as "close enough". The synthetic value
// 1e6*i + 1e3*j + k makes the failure unmistakable instead: a wrong offset does not perturb
// the number, it decodes to a different cell.
//
// The staggering half is the other trap, and it has already fired once: R12 R2's first run
// added a point at the domain edge for u and got 42x81 where the domain has 41x80 x-staggered
// points -- because WRF already carries the extra point inside ite. Mass loops
// its..min(ite, ide-1); staggered loops its..ite.

#include "../wrf_sdirk3_owned_box.h"

#include <torch/torch.h>

#include <iostream>
#include <string>

namespace {

int failures = 0;
int check_count = 0;

void check(bool ok, const std::string& what) {
    ++check_count;
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;
    if (!ok) ++failures;
}

// The decodable value. Distinct primes-of-ten so no (i,j,k) collides with another within the
// index ranges this grid uses.
double tag(int i, int j, int k) {
    return 1.0e6 * i + 1.0e3 * j + k;
}

}  // namespace

int main() {
    using wrf::sdirk3::owned_box;
    using wrf::sdirk3::Stagger;
    using wrf::sdirk3::tile_origin_in_memory;

    // em_b_wave at np=1, with a REAL halo so ims != its and jms != jts. A zero halo would let
    // an off-by-halo bug pass, which is the whole point of the contract.
    const int halo = 5;
    const int ids = 1, ide = 41, jds = 1, jde = 81, kds = 1;
    const int its = ids, ite = ide, jts = jds, jte = jde;   // one rank owns the patch
    const int kts = kds;
    const int ims = its - halo, ime = ite + halo;
    const int jms = jts - halo, jme = jte + halo;
    const int kms = kds, kme = kds + 64;                    // nz=64 mass, nz_w=65 w levels
    const int nz = 64, nz_w = 65;

    const auto origin = tile_origin_in_memory(its, ims, jts, jms, kts, kms);
    check(origin.i == halo && origin.j == halo && origin.k == 0,
          "the tile origin inside the memory domain is (its-ims, jts-jms, kts-kms) = "
          "(5, 5, 0) -- NOT zero, which is what makes this contract worth having");

    // Build the memory-domain patch in the layout the zero-copy contract fixes: Fortran
    // (i,k,j) column-major maps to C++ (j,k,i) row-major.
    const int mj = jme - jms + 1, mk = kme - kms + 1, mi = ime - ims + 1;
    auto patch = torch::empty({mj, mk, mi}, torch::kFloat64);
    {
        auto a = patch.accessor<double, 3>();
        for (int lj = 0; lj < mj; ++lj)
            for (int lk = 0; lk < mk; ++lk)
                for (int li = 0; li < mi; ++li)
                    a[lj][lk][li] = tag(ims + li, jms + lj, kms + lk);
    }

    // Slice exactly as the adapter does.
    auto slice_tile = [&](int ni, int nj, int nk) {
        return patch.slice(0, origin.j, origin.j + nj)
                    .slice(1, origin.k, origin.k + nk)
                    .slice(2, origin.i, origin.i + ni);
    };

    struct Case { const char* name; Stagger s; };
    const Case cases[5] = {{"u", Stagger::X}, {"v", Stagger::Y}, {"w", Stagger::Z},
                           {"t", Stagger::Mass}, {"mu", Stagger::Column}};

    for (const auto& c : cases) {
        const auto b = owned_box(c.s, its, ite, jts, jte, ids, ide, jds, jde, kds, nz, nz_w);
        const int ni = b.i1 - b.i0 + 1, nj = b.j1 - b.j0 + 1, nk = b.k1 - b.k0 + 1;
        auto tile = slice_tile(ni, nj, nk);
        auto a = tile.accessor<double, 3>();

        // The origin, and the FAR corner. The origin alone cannot catch a wrong extent, and
        // the extent alone cannot catch a wrong origin.
        const bool origin_ok = a[0][0][0] == tag(b.i0, b.j0, b.k0);
        const bool corner_ok = a[nj - 1][nk - 1][ni - 1] == tag(b.i1, b.j1, b.k1);
        check(origin_ok && corner_ok,
              std::string(c.name) + ": the tile view's corners decode to the owned box "
              "[" + std::to_string(b.i0) + ".." + std::to_string(b.i1) + "] x [" +
              std::to_string(b.j0) + ".." + std::to_string(b.j1) + "] x [" +
              std::to_string(b.k0) + ".." + std::to_string(b.k1) + "]");
    }

    // The staggering rule itself, against the counts R12 R2 verified against the domain.
    const long long want[5] = {209920, 207360, 208000, 204800, 3200};
    const Case counted[5] = {{"u", Stagger::X}, {"v", Stagger::Y}, {"w", Stagger::Z},
                             {"t", Stagger::Mass}, {"mu", Stagger::Column}};
    for (int c = 0; c < 5; ++c) {
        const auto b = owned_box(counted[c].s, its, ite, jts, jte, ids, ide, jds, jde,
                                 kds, nz, nz_w);
        check(b.count() == want[c],
              std::string(counted[c].name) + ": owned cells = " + std::to_string(b.count()) +
              " (the domain's degrees of freedom, verified in R12 R2)");
    }

    // The specific error R12 R2 made: treating the staggered extra point as something to add
    // at the domain edge. It is already inside ite, so u spans ONE more point than t in i and
    // not two, and v is staggered in j while u is not.
    {
        const auto bu = owned_box(Stagger::X, its, ite, jts, jte, ids, ide, jds, jde,
                                  kds, nz, nz_w);
        const auto bt = owned_box(Stagger::Mass, its, ite, jts, jte, ids, ide, jds, jde,
                                  kds, nz, nz_w);
        const auto bv = owned_box(Stagger::Y, its, ite, jts, jte, ids, ide, jds, jde,
                                  kds, nz, nz_w);
        check(bu.i1 == bt.i1 + 1 && bu.j1 == bt.j1,
              "u is staggered in i ONLY: one more i-point than mass, the same j-extent");
        check(bv.j1 == bt.j1 + 1 && bv.i1 == bt.i1,
              "v is staggered in j ONLY: one more j-point than mass, the same i-extent");
    }

    // A two-rank split must partition exactly: no shared cell, no dropped cell. Count equality
    // alone does not say this -- m overlapped and m dropped cells sum correctly.
    {
        const int mid = (jds + jde) / 2;
        const auto lo = owned_box(Stagger::Mass, its, ite, jds, mid, ids, ide, jds, jde,
                                  kds, nz, nz_w);
        const auto hi = owned_box(Stagger::Mass, its, ite, mid + 1, jde, ids, ide, jds, jde,
                                  kds, nz, nz_w);
        const auto whole = owned_box(Stagger::Mass, its, ite, jts, jte, ids, ide, jds, jde,
                                     kds, nz, nz_w);
        const bool disjoint = lo.j1 < hi.j0;
        const bool gapless = (hi.j0 == lo.j1 + 1);
        check(disjoint && gapless && lo.count() + hi.count() == whole.count(),
              "a j-split into two ranks is an exact partition: disjoint, gapless, and the "
              "counts add to the whole");
    }

    constexpr int expected_checks = 14;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) {
        std::cout << "MEMORY_TILE_OFFSET_CONTRACT: PASS" << std::endl;
        return 0;
    }
    std::cout << "MEMORY_TILE_OFFSET_CONTRACT: FAIL (" << failures << ")" << std::endl;
    return 1;
}
