/**
 * @file wrf_sdirk3_ad_halo_exchange.cpp
 * @brief AD-aware halo exchange implementation
 *
 * Forward: 1x clone → view-based MPI (NoGradGuard) → BC (x→y, no guard)
 * Backward: BC_y^T → BC_x^T → MPI^T (all reversed)
 */

#include <cstdint>  // fixed-width ints used below; libstdc++ (Linux g++) does not provide them transitively
#include "wrf_sdirk3_ad_halo_exchange.h"
#include "wrf_sdirk3_halo_exchange.h"
#include "wrf_sdirk3_mpi_safety.h"  // PR 7B: shared always-on MPI check
#include "wrf_sdirk3_config.h"
#include <iostream>
#include <cstring>
#include <vector>

#ifdef DMPARALLEL
#include <mpi.h>
// MPI error check macro (matches wrf_sdirk3_halo_exchange.cpp)
// PR 7B: Release builds previously compiled this to (void)(call),
// discarding every MPI error code in production. Route to the single
// always-on check in wrf_sdirk3_mpi_safety.h.
#define AD_MPI_CHECK(mpi_call) SDIRK3_MPI_SAFETY_CHECK(mpi_call)
#endif

namespace wrf {
namespace sdirk3 {

// Thread safety for MPI adjoint operations
// PR 7B (3b-2): the adjoint-private mutex is retired — forward, adjoint
// and lifecycle share mpi_safety::MPIExchangeScope now.

// ============================================================================
// HaloBCConditions::fromConfig
// ============================================================================

HaloBCConditions HaloBCConditions::fromConfig(const ADHaloConfig& cfg) {
    HaloBCConditions bc{};
    // Physical boundary = neighbor==-1 && !periodic (canonical definition, v4.9)
    bool phys_west  = (cfg.neighbor_west  == -1) && !cfg.periodic_x;
    bool phys_east  = (cfg.neighbor_east  == -1) && !cfg.periodic_x;
    bool phys_south = (cfg.neighbor_south == -1) && !cfg.periodic_y;
    bool phys_north = (cfg.neighbor_north == -1) && !cfg.periodic_y;

    // Mass/Theta/MU/PH/W: symmetric copy at physical boundaries
    bc.apply_xs_sym = phys_west  && (cfg.sym_xs || cfg.open_xs);
    bc.apply_xe_sym = phys_east  && (cfg.sym_xe || cfg.open_xe);
    bc.apply_ys_sym = phys_south && (cfg.sym_ys || cfg.open_ys);
    bc.apply_ye_sym = phys_north && (cfg.sym_ye || cfg.open_ye);

    // U-staggered: antisymmetric (sym BC) or symmetric copy (open BC) in x
    bc.apply_u_xs_antisym = phys_west && cfg.sym_xs;
    bc.apply_u_xe_antisym = phys_east && cfg.sym_xe;
    bc.apply_u_xs_open    = phys_west && cfg.open_xs;
    bc.apply_u_xe_open    = phys_east && cfg.open_xe;

    // V-staggered: antisymmetric (sym BC) or symmetric copy (open BC) in y
    bc.apply_v_ys_antisym = phys_south && cfg.sym_ys;
    bc.apply_v_ye_antisym = phys_north && cfg.sym_ye;
    bc.apply_v_ys_open    = phys_south && cfg.open_ys;
    bc.apply_v_ye_open    = phys_north && cfg.open_ye;

    return bc;
}

// ============================================================================
// Forward BC helpers (in-place on clone, x→y order)
// ============================================================================

void apply_forward_bc_mass(torch::Tensor& field, const HaloBCConditions& bc,
                           int64_t ni_mem, int64_t nj_mem) {
    // X-boundaries first
    if (bc.apply_xs_sym)
        field.select(2, 0).copy_(field.select(2, 1));
    if (bc.apply_xe_sym)
        field.select(2, ni_mem - 1).copy_(field.select(2, ni_mem - 2));
    // Y-boundaries second
    if (bc.apply_ys_sym)
        field.select(0, 0).copy_(field.select(0, 1));
    if (bc.apply_ye_sym)
        field.select(0, nj_mem - 1).copy_(field.select(0, nj_mem - 2));
}

void apply_forward_bc_u(torch::Tensor& u, const HaloBCConditions& bc,
                        int64_t ni_mem, int64_t nj_mem) {
    // X-boundaries first (antisymmetric for sym BC, symmetric for open BC)
    if (bc.apply_u_xs_antisym)
        u.select(2, 0).copy_(-u.select(2, 2));
    else if (bc.apply_u_xs_open)
        u.select(2, 0).copy_(u.select(2, 1));
    if (bc.apply_u_xe_antisym)
        u.select(2, ni_mem - 1).copy_(-u.select(2, ni_mem - 3));
    else if (bc.apply_u_xe_open)
        u.select(2, ni_mem - 1).copy_(u.select(2, ni_mem - 2));
    // Y-boundaries second (symmetric for both sym and open BC)
    if (bc.apply_ys_sym)
        u.select(0, 0).copy_(u.select(0, 1));
    if (bc.apply_ye_sym)
        u.select(0, nj_mem - 1).copy_(u.select(0, nj_mem - 2));
}

void apply_forward_bc_v(torch::Tensor& v, const HaloBCConditions& bc,
                        int64_t ni_mem, int64_t nj_mem) {
    // X-boundaries first (symmetric for both sym and open BC)
    if (bc.apply_xs_sym)
        v.select(2, 0).copy_(v.select(2, 1));
    if (bc.apply_xe_sym)
        v.select(2, ni_mem - 1).copy_(v.select(2, ni_mem - 2));
    // Y-boundaries second (antisymmetric for sym BC, symmetric for open BC)
    if (bc.apply_v_ys_antisym)
        v.select(0, 0).copy_(-v.select(0, 2));
    else if (bc.apply_v_ys_open)
        v.select(0, 0).copy_(v.select(0, 1));
    if (bc.apply_v_ye_antisym)
        v.select(0, nj_mem - 1).copy_(-v.select(0, nj_mem - 3));
    else if (bc.apply_v_ye_open)
        v.select(0, nj_mem - 1).copy_(v.select(0, nj_mem - 2));
}

// ============================================================================
// Adjoint BC helpers (in-place on grad, y^T→x^T order — reverse of forward)
// ============================================================================

// Adjoint of symmetric copy: y = x[src] → grad_x[src] += grad_y; grad_y = 0
// Adjoint of antisymmetric: y = -x[src] → grad_x[src] -= grad_y; grad_y = 0

void adjoint_bc_mass(torch::Tensor& grad, const HaloBCConditions& bc,
                     int64_t ni_mem, int64_t nj_mem) {
    // Y-adj FIRST (reverse of forward y-last)
    if (bc.apply_ye_sym) {
        grad.select(0, nj_mem - 2).add_(grad.select(0, nj_mem - 1));
        grad.select(0, nj_mem - 1).zero_();
    }
    if (bc.apply_ys_sym) {
        grad.select(0, 1).add_(grad.select(0, 0));
        grad.select(0, 0).zero_();
    }
    // X-adj SECOND (reverse of forward x-first)
    if (bc.apply_xe_sym) {
        grad.select(2, ni_mem - 2).add_(grad.select(2, ni_mem - 1));
        grad.select(2, ni_mem - 1).zero_();
    }
    if (bc.apply_xs_sym) {
        grad.select(2, 1).add_(grad.select(2, 0));
        grad.select(2, 0).zero_();
    }
}

void adjoint_bc_u(torch::Tensor& grad, const HaloBCConditions& bc,
                  int64_t ni_mem, int64_t nj_mem) {
    // Y-adj FIRST (symmetric in y for U)
    if (bc.apply_ye_sym) {
        grad.select(0, nj_mem - 2).add_(grad.select(0, nj_mem - 1));
        grad.select(0, nj_mem - 1).zero_();
    }
    if (bc.apply_ys_sym) {
        grad.select(0, 1).add_(grad.select(0, 0));
        grad.select(0, 0).zero_();
    }
    // X-adj SECOND (antisymmetric or open)
    if (bc.apply_u_xe_antisym) {
        grad.select(2, ni_mem - 3).sub_(grad.select(2, ni_mem - 1));
        grad.select(2, ni_mem - 1).zero_();
    } else if (bc.apply_u_xe_open) {
        grad.select(2, ni_mem - 2).add_(grad.select(2, ni_mem - 1));
        grad.select(2, ni_mem - 1).zero_();
    }
    if (bc.apply_u_xs_antisym) {
        grad.select(2, 2).sub_(grad.select(2, 0));
        grad.select(2, 0).zero_();
    } else if (bc.apply_u_xs_open) {
        grad.select(2, 1).add_(grad.select(2, 0));
        grad.select(2, 0).zero_();
    }
}

void adjoint_bc_v(torch::Tensor& grad, const HaloBCConditions& bc,
                  int64_t ni_mem, int64_t nj_mem) {
    // Y-adj FIRST (antisymmetric or open in y for V)
    if (bc.apply_v_ye_antisym) {
        grad.select(0, nj_mem - 3).sub_(grad.select(0, nj_mem - 1));
        grad.select(0, nj_mem - 1).zero_();
    } else if (bc.apply_v_ye_open) {
        grad.select(0, nj_mem - 2).add_(grad.select(0, nj_mem - 1));
        grad.select(0, nj_mem - 1).zero_();
    }
    if (bc.apply_v_ys_antisym) {
        grad.select(0, 2).sub_(grad.select(0, 0));
        grad.select(0, 0).zero_();
    } else if (bc.apply_v_ys_open) {
        grad.select(0, 1).add_(grad.select(0, 0));
        grad.select(0, 0).zero_();
    }
    // X-adj SECOND (symmetric in x for V)
    if (bc.apply_xe_sym) {
        grad.select(2, ni_mem - 2).add_(grad.select(2, ni_mem - 1));
        grad.select(2, ni_mem - 1).zero_();
    }
    if (bc.apply_xs_sym) {
        grad.select(2, 1).add_(grad.select(2, 0));
        grad.select(2, 0).zero_();
    }
}

// ============================================================================
// Adjoint MPI exchange (Step 5)
// ============================================================================

void adjoint_mpi_exchange_3d(torch::Tensor& grad_field) {
    // PR 7B (3b-2): callers hold an MPIExchangeScope batch; this function
    // is invoked per-field inside it.
    torch::NoGradGuard no_grad;

#ifdef DMPARALLEL
    // Adjoint of forward MPI exchange.
    // Forward: send interior edge → neighbor ghost (copy/overwrite)
    // Adjoint: send ghost grad → neighbor edge (accumulate), then zero ghost grad.
    //
    // For each forward pair:
    //   Isend(my_edge → neighbor_ghost, tag_A) + Irecv(neighbor_edge → my_ghost, tag_B)
    // The adjoint is:
    //   Isend(my_ghost_grad → neighbor, tag_B) + Irecv(from neighbor into temp, tag_A)
    //   my_edge_grad += temp
    //   my_ghost_grad = 0
    //
    // Tags match the forward exactly (roles reversed: ghost↔edge).

    // PR 7B (review): is_initialized() now reads a race-free atomic, so the
    // historical uninitialized NO-OP stays a pre-scope fast path; the scope
    // then guards every actual global read (and the state is re-validated
    // by the checked accessors below).
    if (!halo_exchange_requires_exchange()) return;  // serial/uninit: no-op

    // PUBLIC primitive: carries its own FieldPrimitive scope — alone it is
    // the outer scope; under the AD AdjointBatch it nests legally.
    mpi_safety::MPIExchangeScope scope(
        mpi_safety::MPIExchangeKind::FieldPrimitive, "adjoint_mpi_exchange_3d");
    if (!halo_exchange_requires_exchange()) return;  // reconfigured in between

    MPI_Comm comm = halo_exchange_get_comm();
    int my_rank = halo_exchange_get_rank();
    auto nbrs = halo_exchange_get_neighbors();
    auto widths = halo_exchange_get_widths();
    auto nproc = halo_exchange_get_nproc();

    TORCH_CHECK(grad_field.is_cpu() && grad_field.is_contiguous() &&
                grad_field.scalar_type() == torch::kFloat32,
        "adjoint_mpi_exchange_3d: requires CPU/FP32/contiguous");

    int64_t nj = grad_field.size(0);
    int64_t nk = grad_field.size(1);
    int64_t ni = grad_field.size(2);
    float* data = grad_field.data_ptr<float>();

    int64_t halo_y = widths.halo_width_y;
    int64_t halo_x = widths.halo_width_x;

    int64_t j_edge_south = halo_y;
    int64_t j_edge_north = nj - 2 * halo_y;
    int64_t j_ghost_south = 0;
    int64_t j_ghost_north = nj - halo_y;
    int64_t i_ghost_west = 0;
    int64_t i_ghost_east = ni - halo_x;
    int64_t i_edge_west = halo_x;
    int64_t i_edge_east = ni - 2 * halo_x;

    // v10: Adjoint phased EW_adj→NS_adj (reverse of forward NS→EW)
    // Tags match forward: Isend(tag=dest_rank), Irecv(tag=my_rank)
    // Adjoint sends ghost grad → neighbor edge, receives neighbor ghost grad → our edge

    // ---- Phase 1: E/W adjoint FIRST (reverse of forward E/W second) ----
    if (nproc.nprocx > 1) {
        int64_t ew_count = halo_x * nj * nk;
        TORCH_CHECK(ew_count <= std::numeric_limits<int>::max());
        int ew_int = static_cast<int>(ew_count);

        std::vector<float> send_ghost_east(ew_count);
        std::vector<float> send_ghost_west(ew_count);
        std::vector<float> recv_from_east(ew_count, 0.0f);
        std::vector<float> recv_from_west(ew_count, 0.0f);

        if (nbrs.neighbor_east >= 0) {
            auto east_ghost_slab = grad_field.slice(2, i_ghost_east, i_ghost_east + halo_x).contiguous();
            std::memcpy(send_ghost_east.data(), east_ghost_slab.data_ptr<float>(),
                        static_cast<size_t>(ew_count) * sizeof(float));
        }
        if (nbrs.neighbor_west >= 0) {
            auto west_ghost_slab = grad_field.slice(2, i_ghost_west, i_ghost_west + halo_x).contiguous();
            std::memcpy(send_ghost_west.data(), west_ghost_slab.data_ptr<float>(),
                        static_cast<size_t>(ew_count) * sizeof(float));
        }

        MPI_Request req_ew[4]; int nreq_ew = 0;
        if (nbrs.neighbor_east >= 0) {
            AD_MPI_CHECK(MPI_Irecv(recv_from_east.data(), ew_int, MPI_FLOAT,
                     nbrs.neighbor_east, HALO_TAG_EASTWARD, comm, &req_ew[nreq_ew++]));
            AD_MPI_CHECK(MPI_Isend(send_ghost_east.data(), ew_int, MPI_FLOAT,
                     nbrs.neighbor_east, HALO_TAG_WESTWARD, comm, &req_ew[nreq_ew++]));
        }
        if (nbrs.neighbor_west >= 0) {
            AD_MPI_CHECK(MPI_Irecv(recv_from_west.data(), ew_int, MPI_FLOAT,
                     nbrs.neighbor_west, HALO_TAG_WESTWARD, comm, &req_ew[nreq_ew++]));
            AD_MPI_CHECK(MPI_Isend(send_ghost_west.data(), ew_int, MPI_FLOAT,
                     nbrs.neighbor_west, HALO_TAG_EASTWARD, comm, &req_ew[nreq_ew++]));
        }
        if (nreq_ew > 0)
            AD_MPI_CHECK(MPI_Waitall(nreq_ew, req_ew, MPI_STATUSES_IGNORE));

        // Accumulate E/W received grads into interior edges
        if (nbrs.neighbor_east >= 0) {
            auto recv_t = torch::from_blob(recv_from_east.data(),  // LINT_EXCEPTION: CPU opts explicit below
                {nj, nk, halo_x}, torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU));
            grad_field.slice(2, i_edge_east, i_edge_east + halo_x).add_(recv_t);
        }
        if (nbrs.neighbor_west >= 0) {
            auto recv_t = torch::from_blob(recv_from_west.data(),  // LINT_EXCEPTION: CPU opts explicit below
                {nj, nk, halo_x}, torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU));
            grad_field.slice(2, i_edge_west, i_edge_west + halo_x).add_(recv_t);
        }

        // Zero E/W ghost regions — ONLY where a neighbor exists (PR 7A).
        // The forward leaves a boundary rank's neighborless ghost untouched
        // (identity), so its adjoint must too; unconditional zeroing broke
        // the global dot-product identity (measured rel err 0.11-0.35).
        if (nbrs.neighbor_east >= 0)
            grad_field.slice(2, i_ghost_east, i_ghost_east + halo_x).zero_();
        if (nbrs.neighbor_west >= 0)
            grad_field.slice(2, i_ghost_west, i_ghost_west + halo_x).zero_();
    }

    // ---- Phase 2: N/S adjoint SECOND (reverse of forward N/S first) ----
    if (nproc.nprocy > 1) {
        int64_t halo_ns_count = halo_y * nk * ni;
        TORCH_CHECK(halo_ns_count <= std::numeric_limits<int>::max());
        int halo_ns_int = static_cast<int>(halo_ns_count);

        std::vector<float> recv_from_north(halo_ns_count, 0.0f);
        std::vector<float> recv_from_south(halo_ns_count, 0.0f);

        MPI_Request req_ns[4]; int nreq_ns = 0;
        if (nbrs.neighbor_north >= 0) {
            float* ghost_north_ptr = data + j_ghost_north * nk * ni;
            AD_MPI_CHECK(MPI_Irecv(recv_from_north.data(), halo_ns_int, MPI_FLOAT,
                     nbrs.neighbor_north, HALO_TAG_NORTHWARD, comm, &req_ns[nreq_ns++]));
            AD_MPI_CHECK(MPI_Isend(ghost_north_ptr, halo_ns_int, MPI_FLOAT,
                     nbrs.neighbor_north, HALO_TAG_SOUTHWARD, comm, &req_ns[nreq_ns++]));
        }
        if (nbrs.neighbor_south >= 0) {
            float* ghost_south_ptr = data + j_ghost_south * nk * ni;
            AD_MPI_CHECK(MPI_Irecv(recv_from_south.data(), halo_ns_int, MPI_FLOAT,
                     nbrs.neighbor_south, HALO_TAG_SOUTHWARD, comm, &req_ns[nreq_ns++]));
            AD_MPI_CHECK(MPI_Isend(ghost_south_ptr, halo_ns_int, MPI_FLOAT,
                     nbrs.neighbor_south, HALO_TAG_NORTHWARD, comm, &req_ns[nreq_ns++]));
        }
        if (nreq_ns > 0)
            AD_MPI_CHECK(MPI_Waitall(nreq_ns, req_ns, MPI_STATUSES_IGNORE));

        // Accumulate N/S received grads into interior edges
        if (nbrs.neighbor_north >= 0) {
            auto recv_t = torch::from_blob(recv_from_north.data(),  // LINT_EXCEPTION: CPU opts explicit below
                {halo_y, nk, ni}, torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU));
            grad_field.slice(0, j_edge_north, j_edge_north + halo_y).add_(recv_t);
        }
        if (nbrs.neighbor_south >= 0) {
            auto recv_t = torch::from_blob(recv_from_south.data(),  // LINT_EXCEPTION: CPU opts explicit below
                {halo_y, nk, ni}, torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU));
            grad_field.slice(0, j_edge_south, j_edge_south + halo_y).add_(recv_t);
        }

        // Zero N/S ghost regions — ONLY where a neighbor exists (PR 7A, as above).
        if (nbrs.neighbor_north >= 0)
            grad_field.slice(0, j_ghost_north, j_ghost_north + halo_y).zero_();
        if (nbrs.neighbor_south >= 0)
            grad_field.slice(0, j_ghost_south, j_ghost_south + halo_y).zero_();
    }

#else
    (void)grad_field;
#endif
}

// ============================================================================
// ADHaloExchangeFunction::forward
// ============================================================================

torch::Tensor ADHaloExchangeFunction::forward(
    torch::autograd::AutogradContext* ctx,
    torch::Tensor U_full_halo_packed,
    int64_t nx, int64_t ny, int64_t nz, int64_t nx_u, int64_t ny_v, int64_t nz_w,
    int64_t ni_mem, int64_t nj_mem, int64_t i_off, int64_t j_off,
    int64_t halo_size,
    bool periodic_x, bool periodic_y,
    bool sym_xs, bool sym_xe, bool sym_ys, bool sym_ye,
    bool open_xs, bool open_xe, bool open_ys, bool open_ye,
    int64_t its, int64_t ite, int64_t jts, int64_t jte,
    int64_t ids, int64_t ide, int64_t jds, int64_t jde,
    int64_t neighbor_north, int64_t neighbor_south,
    int64_t neighbor_east, int64_t neighbor_west,
    bool exchange_performed)
{
    // Dtype/device guard
    TORCH_CHECK(U_full_halo_packed.is_cpu() &&
                U_full_halo_packed.scalar_type() == torch::kFloat32 &&
                U_full_halo_packed.is_contiguous(),
        "ADHaloExchangeFunction::forward: requires CPU/FP32/contiguous input");

    // Save config fields individually in ctx->saved_data (Finding #7)
    ctx->saved_data["nx"] = nx;
    ctx->saved_data["ny"] = ny;
    ctx->saved_data["nz"] = nz;
    ctx->saved_data["nx_u"] = nx_u;
    ctx->saved_data["ny_v"] = ny_v;
    ctx->saved_data["nz_w"] = nz_w;
    ctx->saved_data["ni_mem"] = ni_mem;
    ctx->saved_data["nj_mem"] = nj_mem;
    ctx->saved_data["i_off"] = i_off;
    ctx->saved_data["j_off"] = j_off;
    ctx->saved_data["halo_size"] = halo_size;
    ctx->saved_data["periodic_x"] = periodic_x;
    ctx->saved_data["periodic_y"] = periodic_y;
    ctx->saved_data["sym_xs"] = sym_xs;
    ctx->saved_data["sym_xe"] = sym_xe;
    ctx->saved_data["sym_ys"] = sym_ys;
    ctx->saved_data["sym_ye"] = sym_ye;
    ctx->saved_data["open_xs"] = open_xs;
    ctx->saved_data["open_xe"] = open_xe;
    ctx->saved_data["open_ys"] = open_ys;
    ctx->saved_data["open_ye"] = open_ye;
    ctx->saved_data["its"] = its;
    ctx->saved_data["ite"] = ite;
    ctx->saved_data["jts"] = jts;
    ctx->saved_data["jte"] = jte;
    ctx->saved_data["ids"] = ids;
    ctx->saved_data["ide"] = ide;
    ctx->saved_data["jds"] = jds;
    ctx->saved_data["jde"] = jde;
    ctx->saved_data["neighbor_north"] = neighbor_north;
    ctx->saved_data["neighbor_south"] = neighbor_south;
    ctx->saved_data["neighbor_east"] = neighbor_east;
    ctx->saved_data["neighbor_west"] = neighbor_west;
    // P1-2 (review): EXACT equality with the authoritative state, BEFORE
    // the early return — a caller passing false while the configuration
    // requires exchange would otherwise silently return identity and DELETE
    // cross-rank tangent/adjoint transport.
    {
        const bool authoritative_exchange = halo_exchange_requires_exchange();
        TORCH_CHECK(exchange_performed == authoritative_exchange,
            "SDIRK3_MPI_EXCHANGE_STATE_MISMATCH: caller exchange_performed=",
            exchange_performed, " authoritative=", authoritative_exchange);
        exchange_performed = authoritative_exchange;
    }
    ctx->saved_data["exchange_performed"] = exchange_performed;

    if (!exchange_performed) {
        return U_full_halo_packed;
    }

    // Build BC conditions for forward
    ADHaloConfig cfg;
    cfg.nx = nx; cfg.ny = ny; cfg.nz = nz;
    cfg.nx_u = nx_u; cfg.ny_v = ny_v; cfg.nz_w = nz_w;
    cfg.ni_mem = ni_mem; cfg.nj_mem = nj_mem;
    cfg.i_off = i_off; cfg.j_off = j_off;
    cfg.halo_size = static_cast<int>(halo_size);
    cfg.periodic_x = periodic_x; cfg.periodic_y = periodic_y;
    cfg.sym_xs = sym_xs; cfg.sym_xe = sym_xe;
    cfg.sym_ys = sym_ys; cfg.sym_ye = sym_ye;
    cfg.open_xs = open_xs; cfg.open_xe = open_xe;
    cfg.open_ys = open_ys; cfg.open_ye = open_ye;
    cfg.neighbor_north = static_cast<int>(neighbor_north);
    cfg.neighbor_south = static_cast<int>(neighbor_south);
    cfg.neighbor_east = static_cast<int>(neighbor_east);
    cfg.neighbor_west = static_cast<int>(neighbor_west);

    auto bc = HaloBCConditions::fromConfig(cfg);

    // Store BC conditions for backward
    ctx->saved_data["bc_apply_xs_sym"] = bc.apply_xs_sym;
    ctx->saved_data["bc_apply_xe_sym"] = bc.apply_xe_sym;
    ctx->saved_data["bc_apply_ys_sym"] = bc.apply_ys_sym;
    ctx->saved_data["bc_apply_ye_sym"] = bc.apply_ye_sym;
    ctx->saved_data["bc_apply_u_xs_antisym"] = bc.apply_u_xs_antisym;
    ctx->saved_data["bc_apply_u_xe_antisym"] = bc.apply_u_xe_antisym;
    ctx->saved_data["bc_apply_u_xs_open"] = bc.apply_u_xs_open;
    ctx->saved_data["bc_apply_u_xe_open"] = bc.apply_u_xe_open;
    ctx->saved_data["bc_apply_v_ys_antisym"] = bc.apply_v_ys_antisym;
    ctx->saved_data["bc_apply_v_ye_antisym"] = bc.apply_v_ye_antisym;
    ctx->saved_data["bc_apply_v_ys_open"] = bc.apply_v_ys_open;
    ctx->saved_data["bc_apply_v_ye_open"] = bc.apply_v_ye_open;

    // 1x clone: preserve input for autograd
    auto U_out = U_full_halo_packed.clone();

    // View-based field extraction from clone
    auto fields = viewFullHaloFields(U_out, nj_mem, nz, ni_mem, nz_w);

    // MPI exchange under NoGradGuard (MPI is non-differentiable)
    {
        torch::NoGradGuard no_grad;
        // Defense in depth (review): a caller claiming exchange_performed
        // must match the authoritative state — driving the MPI batch against
        // a serial/no-exchange configuration is a contract violation, not a
        // silent no-op.
        TORCH_CHECK(halo_exchange_requires_exchange(),
            "ADHaloExchangeFunction: exchange_performed=true but the halo "
            "configuration does not perform MPI exchange");
        // PR 7B (3b-2): the six-field forward exchange is ONE batch; the
        // per-field calls nest as FieldPrimitive under this scope.
        mpi_safety::MPIExchangeScope batch_scope(
            mpi_safety::MPIExchangeKind::ForwardBatch, "AD forward halo batch");
        // PR 7B (3b-3 part 3): bind this autograd node to the halo
        // lifecycle it captured. Active state and epoch are read UNDER the
        // scope (single-flight excludes any Lifecycle op between this read
        // and the exchanges below); backward verifies the SAME epoch before
        // any gradient mutation or MPI call.
        TORCH_CHECK(halo_exchange_requires_exchange(),
            "ADHaloExchangeFunction: halo configuration became inactive "
            "inside the forward batch scope");
        ctx->saved_data["halo_lifecycle_epoch"] =
            static_cast<int64_t>(halo_exchange_get_epoch());
        halo_exchange_3d_tensor(fields.u,  /*force_full_halo=*/true);
        halo_exchange_3d_tensor(fields.v,  /*force_full_halo=*/true);
        halo_exchange_3d_tensor(fields.w,  /*force_full_halo=*/true);
        halo_exchange_3d_tensor(fields.ph, /*force_full_halo=*/true);
        halo_exchange_3d_tensor(fields.t,  /*force_full_halo=*/true);
        // MU is 2D (nj_mem, 1, ni_mem) — halo exchange expects 3D, this is fine
        halo_exchange_3d_tensor(fields.mu, /*force_full_halo=*/true);
    }

    // Apply BC (NO NoGradGuard — BC is part of adjoint)
    // Order: x-boundaries first, y-boundaries second (Finding #4)
    apply_forward_bc_mass(fields.t,  bc, ni_mem, nj_mem);  // Theta
    apply_forward_bc_mass(fields.w,  bc, ni_mem, nj_mem);  // W
    apply_forward_bc_mass(fields.ph, bc, ni_mem, nj_mem);  // PH
    apply_forward_bc_mass(fields.mu, bc, ni_mem, nj_mem);  // MU
    apply_forward_bc_u(fields.u, bc, ni_mem, nj_mem);       // U (staggered x)
    apply_forward_bc_v(fields.v, bc, ni_mem, nj_mem);       // V (staggered y)

    return U_out;
}

// ============================================================================
// ADHaloExchangeFunction::backward
// ============================================================================

torch::autograd::variable_list ADHaloExchangeFunction::backward(
    torch::autograd::AutogradContext* ctx,
    torch::autograd::variable_list grad_outputs)
{
    bool exchange_performed = ctx->saved_data["exchange_performed"].toBool();

    // Number of inputs to forward: 1 tensor + 34 scalar args = 35 total
    // Only the first (tensor) input gets a gradient; rest are scalars (nullptr)
    const int num_inputs = 35;

    if (!exchange_performed) {
        torch::autograd::variable_list grads(num_inputs);
        grads[0] = grad_outputs[0];
        return grads;
    }

    // PR 7B (3b-3 part 3): the adjoint transposes the EXACT forward
    // exchange — it is only valid against the halo lifecycle the forward
    // captured. Verified UNDER the AdjointBatch scope BEFORE any gradient
    // mutation or MPI call: on failure the gradients stay unproduced and
    // no rank enters the exchange (no deadlock — every rank fails the same
    // local check). The scope wraps BC^T and MPI^T entirely; the per-field
    // adjoint primitives nest as FieldPrimitive under it.
    const uint64_t saved_epoch = static_cast<uint64_t>(
        ctx->saved_data["halo_lifecycle_epoch"].toInt());
    mpi_safety::MPIExchangeScope batch_scope(
        mpi_safety::MPIExchangeKind::AdjointBatch, "AD adjoint halo batch");
    TORCH_CHECK(halo_exchange_requires_exchange(),
        "SDIRK3_MPI_HALO_EPOCH_CHANGED: adjoint requires the forward's "
        "halo lifecycle but no active exchange configuration is published");
    const uint64_t current_epoch = halo_exchange_get_epoch();
    TORCH_CHECK(current_epoch == saved_epoch,
        "SDIRK3_MPI_HALO_EPOCH_CHANGED: forward captured lifecycle epoch ",
        saved_epoch, " but the current epoch is ", current_epoch,
        " (the halo lifecycle was finalized or re-prepared between "
        "forward and backward)");

    // Reconstruct config from saved_data
    int64_t nz = ctx->saved_data["nz"].toInt();
    int64_t nz_w = ctx->saved_data["nz_w"].toInt();
    int64_t ni_mem = ctx->saved_data["ni_mem"].toInt();
    int64_t nj_mem = ctx->saved_data["nj_mem"].toInt();

    // Reconstruct BC conditions
    HaloBCConditions bc{};
    bc.apply_xs_sym = ctx->saved_data["bc_apply_xs_sym"].toBool();
    bc.apply_xe_sym = ctx->saved_data["bc_apply_xe_sym"].toBool();
    bc.apply_ys_sym = ctx->saved_data["bc_apply_ys_sym"].toBool();
    bc.apply_ye_sym = ctx->saved_data["bc_apply_ye_sym"].toBool();
    bc.apply_u_xs_antisym = ctx->saved_data["bc_apply_u_xs_antisym"].toBool();
    bc.apply_u_xe_antisym = ctx->saved_data["bc_apply_u_xe_antisym"].toBool();
    bc.apply_u_xs_open = ctx->saved_data["bc_apply_u_xs_open"].toBool();
    bc.apply_u_xe_open = ctx->saved_data["bc_apply_u_xe_open"].toBool();
    bc.apply_v_ys_antisym = ctx->saved_data["bc_apply_v_ys_antisym"].toBool();
    bc.apply_v_ye_antisym = ctx->saved_data["bc_apply_v_ye_antisym"].toBool();
    bc.apply_v_ys_open = ctx->saved_data["bc_apply_v_ys_open"].toBool();
    bc.apply_v_ye_open = ctx->saved_data["bc_apply_v_ye_open"].toBool();

    // Own the grad for in-place adjoint ops
    auto grad_packed = grad_outputs[0].clone();
    auto grad_fields = viewFullHaloFields(grad_packed, nj_mem, nz, ni_mem, nz_w);

    // Backward order: BC_y^T → BC_x^T → MPI^T (exact reverse of forward MPI→BC_x→BC_y)
    // For each field: adjoint BC first (y then x), then adjoint MPI
    adjoint_bc_mass(grad_fields.t,  bc, ni_mem, nj_mem);
    adjoint_bc_mass(grad_fields.w,  bc, ni_mem, nj_mem);
    adjoint_bc_mass(grad_fields.ph, bc, ni_mem, nj_mem);
    adjoint_bc_mass(grad_fields.mu, bc, ni_mem, nj_mem);
    adjoint_bc_u(grad_fields.u, bc, ni_mem, nj_mem);
    adjoint_bc_v(grad_fields.v, bc, ni_mem, nj_mem);

    // PR 7B (3b-2): the six-field adjoint exchange is ONE batch under the
    // SHARED single-flight scope (the old adjoint-private mutex could not
    // exclude a concurrent forward exchange). 3b-3 part 3: the scope was
    // acquired above, before the gradient clone and BC^T.
    adjoint_mpi_exchange_3d(grad_fields.u);
    adjoint_mpi_exchange_3d(grad_fields.v);
    adjoint_mpi_exchange_3d(grad_fields.w);
    adjoint_mpi_exchange_3d(grad_fields.ph);
    adjoint_mpi_exchange_3d(grad_fields.t);
    adjoint_mpi_exchange_3d(grad_fields.mu);

    // Return gradients: only first input (tensor) gets grad, rest are scalars (no grad)
    torch::autograd::variable_list grads(num_inputs);
    grads[0] = grad_packed;
    return grads;
}

// ============================================================================
// Convenience wrapper
// ============================================================================

torch::Tensor performADHaloExchange(
    const torch::Tensor& U_full_halo_packed,
    const ADHaloConfig& cfg)
{
    return ADHaloExchangeFunction::apply(
        U_full_halo_packed,
        cfg.nx, cfg.ny, cfg.nz, cfg.nx_u, cfg.ny_v, cfg.nz_w,
        cfg.ni_mem, cfg.nj_mem, cfg.i_off, cfg.j_off,
        static_cast<int64_t>(cfg.halo_size),
        cfg.periodic_x, cfg.periodic_y,
        cfg.sym_xs, cfg.sym_xe, cfg.sym_ys, cfg.sym_ye,
        cfg.open_xs, cfg.open_xe, cfg.open_ys, cfg.open_ye,
        static_cast<int64_t>(cfg.its), static_cast<int64_t>(cfg.ite),
        static_cast<int64_t>(cfg.jts), static_cast<int64_t>(cfg.jte),
        static_cast<int64_t>(cfg.ids), static_cast<int64_t>(cfg.ide),
        static_cast<int64_t>(cfg.jds), static_cast<int64_t>(cfg.jde),
        static_cast<int64_t>(cfg.neighbor_north), static_cast<int64_t>(cfg.neighbor_south),
        static_cast<int64_t>(cfg.neighbor_east), static_cast<int64_t>(cfg.neighbor_west),
        cfg.exchange_performed
    );
}

} // namespace sdirk3
} // namespace wrf
