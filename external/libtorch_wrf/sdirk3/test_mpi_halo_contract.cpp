// test_mpi_halo_contract.cpp (PR 7A)
//
// MPI halo-exchange operator contract. Run under mpirun -np {1,2,4}:
//   np=1: 1x1        np=2: 2x1 and 1x2        np=4: 2x2
// For every (topology x periodic_x/y x halo width 1/2 x mass/U/V/W stagger):
//   - every ghost cell owned by a neighbor patch equals the analytic value
//     of its global owner cell (periodic wrap included)
//   - ghost cells outside the domain (physical boundary, non-periodic)
//     are left untouched (sentinel preserved)
//   - corner ghosts are correct (two-phase NS->EW propagation)
//   - a second exchange is idempotent (same assertions hold)
// Plus the adjoint identity, summed over all ranks:
//   <H x, y> == <x, H^T y>
// where H = halo_exchange_3d_tensor and H^T = adjoint_mpi_exchange_3d.
//
// Production numerics are NOT touched: this only drives the existing
// operators. The analytic field f(i,j,k) = (i*64+j)*64+k is exact in FP32
// for the extents used here.

#include <mpi.h>
#include <torch/torch.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <vector>

#include "wrf_sdirk3_halo_exchange.h"
#include "wrf_sdirk3_ad_halo_exchange.h"

using namespace wrf::sdirk3;

namespace {

int g_failures = 0;
int g_checks = 0;

void fail(const char* label, const char* fmt, ...) {
    ++g_failures;
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    int rank = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    std::fprintf(stderr, "FAIL [%s] rank %d: %s\n", label, rank, msg);
}

constexpr float SENTINEL = -1.0f;

float analytic(int gi, int gj, int gk) {
    return static_cast<float>((gi * 64 + gj) * 64 + gk);
}

// WRF-style block distribution of n domain cells over p ranks.
void block_range(int n, int p, int coord, int& start, int& end) {
    int base = n / p, rem = n % p;
    start = coord * base + std::min(coord, rem);
    end = start + base + (coord < rem ? 1 : 0) - 1;  // inclusive
}

struct Case {
    int nprocx, nprocy;
    bool periodic_x, periodic_y;
    int halo_w;
    int stag;  // 0=mass 1=U(+i) 2=V(+j) 3=W(+k)
};

const char* stag_name(int s) {
    static const char* names[] = {"mass", "U", "V", "W"};
    return names[s];
}

// One full forward-contract case. Returns the initialized geometry through
// out-params so the adjoint test can reuse it before finalize.
struct Geom {
    int ids, ide, jds, jde, kds, kde;
    int ims, ime, jms, jme;
    int ips, ipe, jps, jpe;
    int nj_mem, nk, ni_mem;
    int mypx, mypy;
};

Geom setup_case(const Case& c, MPI_Comm cart) {
    // Global UNEVEN domain (13 and 11 do not divide by 2).
    Geom g{};
    g.ids = 1; g.ide = 13 + (c.stag == 1 ? 1 : 0);
    g.jds = 1; g.jde = 11 + (c.stag == 2 ? 1 : 0);
    g.kds = 1; g.kde = 5 + (c.stag == 3 ? 1 : 0);

    int coords[2], dims[2], periods[2];
    MPI_Cart_get(cart, 2, dims, periods, coords);
    g.mypy = coords[0]; g.mypx = coords[1];

    int i0, i1, j0, j1;
    block_range(g.ide - g.ids + 1, c.nprocx, g.mypx, i0, i1);
    block_range(g.jde - g.jds + 1, c.nprocy, g.mypy, j0, j1);
    g.ips = g.ids + i0; g.ipe = g.ids + i1;
    g.jps = g.jds + j0; g.jpe = g.jds + j1;

    g.ims = g.ips - c.halo_w; g.ime = g.ipe + c.halo_w;
    g.jms = g.jps - c.halo_w; g.jme = g.jpe + c.halo_w;

    g.nj_mem = g.jme - g.jms + 1;
    g.ni_mem = g.ime - g.ims + 1;
    g.nk = g.kde - g.kds + 1;

    set_wrf_communicator(MPI_Comm_c2f(cart), c.periodic_x, c.periodic_y);
    halo_exchange_init(g.ids, g.ide, g.jds, g.jde, g.kds, g.kde,
                       g.ims, g.ime, g.jms, g.jme, g.kds, g.kde,
                       g.ips, g.ipe, g.jps, g.jpe, g.kds, g.kde,
                       c.nprocx, c.nprocy, g.mypx, g.mypy, c.halo_w);
    return g;
}

torch::Tensor make_field(const Geom& g) {
    auto t = torch::full({g.nj_mem, g.nk, g.ni_mem}, SENTINEL, torch::kFloat32);
    auto acc = t.accessor<float, 3>();
    for (int j = g.jps; j <= g.jpe; ++j)
        for (int k = g.kds; k <= g.kde; ++k)
            for (int i = g.ips; i <= g.ipe; ++i)
                acc[j - g.jms][k - g.kds][i - g.ims] = analytic(i, j, k);
    return t;
}

// Global owner of a ghost coordinate, with periodic wrap. Returns false if
// the coordinate falls outside the domain (physical boundary: untouched).
bool owner_coord(int c, int ds, int de, bool periodic, int& out) {
    int n = de - ds + 1;
    if (c >= ds && c <= de) { out = c; return true; }
    if (!periodic) return false;
    out = ds + ((c - ds) % n + n) % n;
    return true;
}

void verify_field(const Case& c, const Geom& g, const torch::Tensor& t,
                  const char* label) {
    auto acc = t.accessor<float, 3>();
    auto nbrs = halo_exchange_get_neighbors();
    for (int j = g.jms; j <= g.jme; ++j) {
        for (int i = g.ims; i <= g.ime; ++i) {
            bool ghost_i = (i < g.ips || i > g.ipe);
            bool ghost_j = (j < g.jps || j > g.jpe);
            if (!ghost_i && !ghost_j) continue;  // interior: checked implicitly

            // Which neighbor would have to exist for this ghost to be filled?
            bool have_x = !ghost_i ||
                (i < g.ips ? nbrs.neighbor_west >= 0 : nbrs.neighbor_east >= 0);
            bool have_y = !ghost_j ||
                (j < g.jps ? nbrs.neighbor_south >= 0 : nbrs.neighbor_north >= 0);

            int oi, oj;
            bool in_x = owner_coord(i, g.ids, g.ide, c.periodic_x, oi);
            bool in_y = owner_coord(j, g.jds, g.jde, c.periodic_y, oj);

            for (int k = g.kds; k <= g.kde; ++k) {
                float got = acc[j - g.jms][k - g.kds][i - g.ims];
                if (in_x && in_y && have_x && have_y) {
                    float want = analytic(oi, oj, k);
                    if (got != want) {
                        fail(label, "ghost (i=%d,j=%d,k=%d) = %.1f, want %.1f "
                             "(owner %d,%d)", i, j, k, got, want, oi, oj);
                        return;  // one report per case is enough to diagnose
                    }
                } else {
                    if (got != SENTINEL) {
                        fail(label, "boundary ghost (i=%d,j=%d,k=%d) = %.1f, "
                             "expected untouched sentinel", i, j, k, got);
                        return;
                    }
                }
                ++g_checks;
            }
        }
    }
}

void run_forward_case(const Case& c, MPI_Comm cart, char* label) {
    Geom g = setup_case(c, cart);
    auto t = make_field(g);
    halo_exchange_3d_tensor(t);
    verify_field(c, g, t, label);
    // Repeated exchange must be idempotent: same assertions hold.
    halo_exchange_3d_tensor(t);
    verify_field(c, g, t, label);
    halo_exchange_finalize();
}

// <H x, y> == <x, H^T y>, summed over ranks.
void run_adjoint_case(const Case& c, MPI_Comm cart, const char* label) {
    Geom g = setup_case(c, cart);
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    torch::manual_seed(1234 + rank);
    auto x = torch::rand({g.nj_mem, g.nk, g.ni_mem}, torch::kFloat32);
    auto y = torch::rand({g.nj_mem, g.nk, g.ni_mem}, torch::kFloat32);

    torch::NoGradGuard no_grad;  // .item() below must never touch a graph
    auto Hx = x.clone();
    halo_exchange_3d_tensor(Hx);
    auto Hty = y.clone();
    adjoint_mpi_exchange_3d(Hty);

    double lhs_local = (Hx.to(torch::kFloat64) * y.to(torch::kFloat64)).sum().item<double>();
    double rhs_local = (x.to(torch::kFloat64) * Hty.to(torch::kFloat64)).sum().item<double>();
    double lhs = 0, rhs = 0;
    MPI_Allreduce(&lhs_local, &lhs, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    MPI_Allreduce(&rhs_local, &rhs, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);

    double denom = std::max({std::abs(lhs), std::abs(rhs), 1e-30});
    double rel = std::abs(lhs - rhs) / denom;
    if (rel > 1e-6) {
        fail(label, "dot-product identity broken: <Hx,y>=%.12g <x,Hty>=%.12g rel=%.3g",
             lhs, rhs, rel);
    }
    ++g_checks;
    halo_exchange_finalize();
}

}  // namespace

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int size, rank;
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    std::vector<std::pair<int, int>> topos;  // (nprocx, nprocy)
    if (size == 1) topos = {{1, 1}};
    else if (size == 2) topos = {{2, 1}, {1, 2}};
    else if (size == 4) topos = {{2, 2}};
    else {
        if (rank == 0) std::fprintf(stderr, "FAIL: run with -np 1, 2, or 4\n");
        MPI_Finalize();
        return 1;
    }

    for (auto [px, py] : topos) {
        // WRF convention: dim 0 = y, dim 1 = x, NON-periodic communicator
        // (periodic wrap is controlled by the set_wrf_communicator flags).
        int dims[2] = {py, px}, periods[2] = {0, 0};
        MPI_Comm cart;
        MPI_Cart_create(MPI_COMM_WORLD, 2, dims, periods, 0, &cart);

        for (bool per_x : {false, true})
        for (bool per_y : {false, true})
        for (int hw : {1, 2})
        for (int stag : {0, 1, 2, 3}) {
            Case c{px, py, per_x, per_y, hw, stag};
            char label[128];
            std::snprintf(label, sizeof label,
                          "fwd %dx%d per=%d%d w=%d %s",
                          px, py, per_x, per_y, hw, stag_name(stag));
            run_forward_case(c, cart, label);
        }

        for (bool per : {false, true})
        for (int hw : {1, 2}) {
            Case c{px, py, per, per, hw, 0};
            char label[128];
            std::snprintf(label, sizeof label, "adj %dx%d per=%d w=%d",
                          px, py, per, hw);
            run_adjoint_case(c, cart, label);
        }

        MPI_Comm_free(&cart);
    }

    int total_failures = 0, total_checks = 0;
    MPI_Allreduce(&g_failures, &total_failures, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
    MPI_Allreduce(&g_checks, &total_checks, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
    if (rank == 0) {
        if (total_failures == 0)
            std::printf("MPI HALO CONTRACT PASSED: %d checks, np=%d\n",
                        total_checks, size);
        else
            std::printf("MPI HALO CONTRACT FAILED: %d failure(s), np=%d\n",
                        total_failures, size);
    }
    MPI_Finalize();
    return total_failures == 0 ? 0 : 1;
}
