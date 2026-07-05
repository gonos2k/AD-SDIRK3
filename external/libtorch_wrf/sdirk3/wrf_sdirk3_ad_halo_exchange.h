#ifndef WRF_SDIRK3_AD_HALO_EXCHANGE_H
#define WRF_SDIRK3_AD_HALO_EXCHANGE_H

/**
 * @file wrf_sdirk3_ad_halo_exchange.h
 * @brief AD-aware halo exchange for DA mode (split_mode>=2, slow_in_tangent=true)
 *
 * Implements a custom autograd Function that performs MPI halo exchange + BC
 * in the forward pass and adjoint operations in the backward pass,
 * preserving the computational graph for cross-tile tangent propagation.
 *
 * Architecture: padInteriorToFullHaloState → ADHaloExchangeFunction::apply → full-halo state
 * Forward: 1x clone → view-based MPI → BC (x→y order)
 * Backward: BC_y^T → BC_x^T → MPI^T (exact reverse)
 */

#include <torch/torch.h>
#include <mutex>
#include "wrf_sdirk3_ad_halo_utils.h"

namespace wrf {
namespace sdirk3 {

// ============================================================================
// ADHaloConfig — configuration for AD halo exchange
// ============================================================================

struct ADHaloConfig {
    int64_t nx, ny, nz, nx_u, ny_v, nz_w;
    int64_t ni_mem, nj_mem;
    int64_t i_off, j_off;
    int halo_size;
    bool periodic_x, periodic_y;
    bool sym_xs, sym_xe, sym_ys, sym_ye;
    bool open_xs, open_xe, open_ys, open_ye;
    int its, ite, jts, jte, ids, ide, jds, jde;
    int neighbor_north, neighbor_south, neighbor_east, neighbor_west;
    bool exchange_performed;  // false → identity (single-tile or no init)
};

// ============================================================================
// HaloBCConditions — shared BC condition struct (Finding #13)
// ============================================================================

// Precomputed from ADHaloConfig. Same struct used by forward BC and adjoint BC.
// Two-layer detection (Finding #35):
//   Layer 1: "Is this a physical boundary?" → neighbor_X == -1 && !periodic_X
//   Layer 2: "What BC type?" → sym_X / open_X flags
//
// Finding #72: Periodic BC removed — multi-tile periodic blocked (#69),
// single-tile doesn't use AD halo. No periodic fields needed.
struct HaloBCConditions {
    // Physical boundary BC: symmetric/open at non-periodic domain edges
    bool apply_xs_sym;  // neighbor_west==-1 && !periodic_x && (sym_xs || open_xs)
    bool apply_xe_sym;  // neighbor_east==-1 && !periodic_x && (sym_xe || open_xe)
    bool apply_ys_sym;  // neighbor_south==-1 && !periodic_y && (sym_ys || open_ys)
    bool apply_ye_sym;  // neighbor_north==-1 && !periodic_y && (sym_ye || open_ye)

    // U-staggered x-boundaries: antisymmetric (sym) or symmetric copy (open)
    bool apply_u_xs_antisym;  // neighbor_west==-1 && !periodic_x && sym_xs
    bool apply_u_xe_antisym;  // neighbor_east==-1 && !periodic_x && sym_xe
    bool apply_u_xs_open;     // neighbor_west==-1 && !periodic_x && open_xs
    bool apply_u_xe_open;     // neighbor_east==-1 && !periodic_x && open_xe

    // V-staggered y-boundaries: antisymmetric (sym) or symmetric copy (open)
    bool apply_v_ys_antisym;  // neighbor_south==-1 && !periodic_y && sym_ys
    bool apply_v_ye_antisym;  // neighbor_north==-1 && !periodic_y && sym_ye
    bool apply_v_ys_open;     // neighbor_south==-1 && !periodic_y && open_ys
    bool apply_v_ye_open;     // neighbor_north==-1 && !periodic_y && open_ye

    static HaloBCConditions fromConfig(const ADHaloConfig& cfg);
};

// ============================================================================
// Forward BC helpers (in-place on clone, x→y order)
// ============================================================================

void apply_forward_bc_mass(torch::Tensor& field, const HaloBCConditions& bc,
                           int64_t ni_mem, int64_t nj_mem);
void apply_forward_bc_u(torch::Tensor& u, const HaloBCConditions& bc,
                        int64_t ni_mem, int64_t nj_mem);
void apply_forward_bc_v(torch::Tensor& v, const HaloBCConditions& bc,
                        int64_t ni_mem, int64_t nj_mem);

// ============================================================================
// Adjoint BC helpers (in-place on grad, y^T→x^T order)
// ============================================================================

void adjoint_bc_mass(torch::Tensor& grad, const HaloBCConditions& bc,
                     int64_t ni_mem, int64_t nj_mem);
void adjoint_bc_u(torch::Tensor& grad, const HaloBCConditions& bc,
                  int64_t ni_mem, int64_t nj_mem);
void adjoint_bc_v(torch::Tensor& grad, const HaloBCConditions& bc,
                  int64_t ni_mem, int64_t nj_mem);

// ============================================================================
// Adjoint MPI exchange
// ============================================================================

void adjoint_mpi_exchange_3d(torch::Tensor& grad_field);

// ============================================================================
// ADHaloExchangeFunction — custom autograd
// ============================================================================

class ADHaloExchangeFunction : public torch::autograd::Function<ADHaloExchangeFunction> {
public:
    static torch::Tensor forward(
        torch::autograd::AutogradContext* ctx,
        torch::Tensor U_full_halo_packed,
        // Config passed as individual tensors/scalars to avoid IValue struct issues.
        // The caller builds ADHaloConfig and we store fields in saved_data.
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
        bool exchange_performed);

    static torch::autograd::variable_list backward(
        torch::autograd::AutogradContext* ctx,
        torch::autograd::variable_list grad_outputs);
};

// Convenience wrapper to call apply with ADHaloConfig
torch::Tensor performADHaloExchange(
    const torch::Tensor& U_full_halo_packed,
    const ADHaloConfig& cfg);

} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_AD_HALO_EXCHANGE_H
