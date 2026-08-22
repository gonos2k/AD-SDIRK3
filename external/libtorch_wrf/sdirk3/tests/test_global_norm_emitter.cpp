// Emits REAL SDIRK3_GLOBAL_NORM records from the production emitter, for the offline
// comparator to parse.
//
// WHY: test_global_ownership.py builds its records by hand. Every case it runs is therefore a
// test of the comparator against a string I wrote, not against the string the solver writes.
// Rename a field in emitGlobalNormRecord -- phase, state_published, i0 -- and every Python
// case still passes while every real log becomes unparseable. This repository has already
// been bitten by exactly that shape (a CI self-test counting its own fixtures rather than the
// live gates), so the loop is closed here: this binary drives the production emitter, and the
// comparator reads what it produces.
//
// It is not a ctest of its own. test_global_ownership.py invokes it, so the emitter and the
// parser can only pass together.

#include "../wrf_sdirk3_tile_unified.h"

#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    // A small domain, decomposed or not, at a chosen phase. Deliberately NOT em_b_wave's
    // size: a comparator that only works at one grid is not a comparator.
    const std::string phase = (argc > 1) ? argv[1] : "output";
    const bool published = (phase == "output");
    // Optional j-split, so the caller can produce a genuine two-rank partition.
    const int ranks = (argc > 2) ? std::atoi(argv[2]) : 1;

    const int ids = 1, ide = 9, jds = 1, jde = 7, kds = 1, kde = 5;
    const int nz = kde - kds, nz_w = nz + 1;
    const int halo = 3;

    const int nx_mass = ide - ids;      // 8
    const int nj_total = jde - jds;     // 6

    for (int r = 0; r < ranks; ++r) {
        const int jts = jds + (nj_total / ranks) * r;
        const int jte = (r == ranks - 1) ? jde : jds + (nj_total / ranks) * (r + 1) - 1;
        const int its = ids, ite = ide;

        // Mass rows this rank owns. The staggered extra row is already inside jte.
        const int ny = std::min(jte, jde - 1) - jts + 1;
        const int nx = nx_mass;
        const int nx_u = nx + 1;
        const int ny_v = ny + ((jte == jde) ? 1 : 0);

        std::vector<float> rdx(1, 1.0f), rdy(1, 1.0f), rdnw(nz, 1.0f);
        TileSDIRK3UnifiedSolver solver(nx, ny, nz, 1000.0f, 1000.0f,
                                                    rdx, rdy, rdnw, /*tile_id=*/r);
        solver.setWRFIndices(its, ite, jts, jte, kds, kde - 1,
                             ids, ide, jds, jde, kds, kde,
                             its - halo, ite + halo, jts - halo, jte + halo, kds, kde);

        // Tile-shaped buffers in the (j,k,i) layout the zero-copy contract fixes. Values are
        // deterministic and rank-dependent so a partition error changes the combined norm.
        auto fill = [&](std::vector<float>& a, int nj, int nk, int ni) {
            a.assign(static_cast<size_t>(nj) * nk * ni, 0.0f);
            for (int j = 0; j < nj; ++j)
                for (int k = 0; k < nk; ++k)
                    for (int i = 0; i < ni; ++i) {
                        a[(static_cast<size_t>(j) * nk + k) * ni + i] =
                            static_cast<float>(0.5 + 0.25 * ((i + j + k) % 4));
                    }
        };
        std::vector<float> u, v, w, ph, t, mu;
        fill(u,  ny,   nz,   nx_u);
        fill(v,  ny_v, nz,   nx);
        fill(w,  ny,   nz_w, nx);
        fill(ph, ny,   nz_w, nx);
        fill(t,  ny,   nz,   nx);
        fill(mu, ny,   1,    nx);

        solver.emitGlobalNormRecord(phase.c_str(), published, /*outcome_code=*/0,
                                    /*rk_step=*/1,
                                    u.data(), v.data(), w.data(), ph.data(), t.data(),
                                    mu.data(), nx, ny, nz, nx_u, ny_v, nz_w);
    }
    return 0;
}
