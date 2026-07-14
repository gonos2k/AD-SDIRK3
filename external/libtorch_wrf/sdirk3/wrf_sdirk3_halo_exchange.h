#ifndef WRF_SDIRK3_HALO_EXCHANGE_H
#define WRF_SDIRK3_HALO_EXCHANGE_H

// Build-system compatibility:
// Fortran side uses DM_PARALLEL while C++ code traditionally checks DMPARALLEL.
// Accept either macro so mixed config profiles do not silently disable MPI paths.
#if defined(DM_PARALLEL) && !defined(DMPARALLEL)
#define DMPARALLEL 1
#endif

#include <torch/torch.h>
#include <cstdint>
#ifdef DMPARALLEL
#include <mpi.h>
#endif
#include "wrf_sdirk3_boundary_handler.h"

namespace wrf {
namespace sdirk3 {

/**
 * Halo exchange and boundary condition implementation for WRF SDIRK3
 *
 * This implements the missing halo exchange functionality that was identified
 * in the boundary/halo analysis. It handles:
 * 1. Periodic boundary conditions
 * 2. Open boundary radiation conditions
 * 3. Specified boundary relaxation
 * 4. Halo region updates for parallel communication
 *
 * ═══════════════════════════════════════════════════════════════════════════
 * FIX 2025-01-25: HALO OWNERSHIP POLICY
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * CRITICAL POLICY: WRF handles ALL halo exchanges and boundary conditions
 * at the MPI level. SDIRK3 C++ code should:
 *
 * 1. NEVER apply periodic BC - WRF MPI handles this
 * 2. ONLY operate on interior points [its:ite, jts:jte, kts:kte]
 * 3. Use halo data READ-ONLY (filled by WRF before C++ call)
 *
 * Rationale: Double-applying boundary conditions causes:
 * - Periodic BC: Values applied twice, corrupting data
 * - Symmetric BC: Mirror applied twice = no change (but wastes cycles)
 * - Open BC: Extrapolation applied twice, causing drift
 *
 * The exchange_halo_3d_* functions below are kept for:
 * - MPI halo exchange (when SDIRK3 manages its own MPI comms)
 * - Backward compatibility with standalone tests
 *
 * For normal WRF integration, set skip_periodic_bc=true or use
 * the WRF_SDIRK3_WRF_HANDLES_BC compile flag.
 * ═══════════════════════════════════════════════════════════════════════════
 */

// Compile-time flag: When defined, C++ skips periodic BC (WRF handles it)
#ifndef WRF_SDIRK3_WRF_HANDLES_BC
#define WRF_SDIRK3_WRF_HANDLES_BC 1  // Default: WRF handles BC
#endif

// =============================================================================
// Halo Exchange Functions
// =============================================================================

// =============================================================================
// MPI-based Halo Exchange Functions (implemented in wrf_sdirk3_halo_exchange.cpp)
// =============================================================================

// Returns MPI neighbor ranks from the initialized halo exchange module.
// -1 = no neighbor (physical boundary in Cartesian grid).
// Must be called after halo_exchange_init().
// Direction-disambiguating message tags (PR 7A). The previous scheme
// (Isend tag=dest_rank, Irecv tag=my_rank) collides when the SAME peer is
// both neighbors — 2 ranks with periodic wrap in the decomposed direction —
// and the east/west (or north/south) messages cross over (measured:
// 2x1 periodic_x delivered the peer's WEST edge into the west ghost).
// A message is tagged by its direction of travel; that is unambiguous for
// any peer multiplicity. The adjoint reuses the tag of the forward message
// it reverses.
enum HaloMsgTag {
    HALO_TAG_NORTHWARD = 71,
    HALO_TAG_SOUTHWARD = 72,
    HALO_TAG_EASTWARD  = 73,
    HALO_TAG_WESTWARD  = 74,
};

struct HaloNeighborInfo {
    int neighbor_north, neighbor_south, neighbor_east, neighbor_west;
};
HaloNeighborInfo halo_exchange_get_neighbors();

// Returns halo widths (x and y) from the initialized halo exchange module.
struct HaloWidthInfo {
    int halo_width_x, halo_width_y;
};
HaloWidthInfo halo_exchange_get_widths();

// Returns whether the halo exchange module is initialized and has MPI support.
bool halo_exchange_is_initialized();

// True only when the current configuration performs real MPI halo
// communication (multi-rank Cartesian); a published single-rank serial
// state is initialized but does NOT require exchange (PR 7B review P1).
bool halo_exchange_requires_exchange() noexcept;

// Initialize halo exchange system
void halo_exchange_init(int ids, int ide, int jds, int jde, int kds, int kde,
                       int ims, int ime, int jms, int jme, int kms, int kme,
                       int ips, int ipe, int jps, int jpe, int kps, int kpe,
                       int nprocx, int nprocy, int mypx, int mypy,
                       int halo_width);

// Finalize halo exchange system
void halo_exchange_finalize();

// Epoch counter — incremented on every comm change or finalize event.
// Used by TileSDIRK3UnifiedSolver to detect stale halo state.
uint64_t halo_exchange_get_epoch();

#ifdef DMPARALLEL
// Pass WRF's domain-specific Cartesian communicator (local_communicator)
// plus per-direction periodic flags. Epoch incremented on change.
// NOTE: -fdefault-integer-8 / -i8 builds NOT supported.
// static_assert(sizeof(MPI_Fint)==sizeof(int)) enforces this at compile time.
void set_wrf_communicator(MPI_Fint fortran_comm, bool periodic_x, bool periodic_y);
bool is_wrf_communicator_set();
MPI_Comm halo_exchange_get_comm();
int halo_exchange_get_rank();
#else
inline void set_wrf_communicator(int, bool, bool) {}
inline bool is_wrf_communicator_set() { return false; }
#endif

// Process grid info from initialized halo exchange (Cart or fallback).
struct HaloProcInfo { int nprocx, nprocy, mypx, mypy; };
HaloProcInfo halo_exchange_get_nproc();

// Perform halo exchange for a single 3D tensor
// force_full_halo: when true (AD path), relaxes storage_offset==0 check for views
//                  into cloned packed state. Uses is_contiguous() + shape match instead.
//                  Default false preserves original strict check for all non-AD paths.
void halo_exchange_3d_tensor(torch::Tensor& tensor, bool force_full_halo = false);

// Perform halo exchange for multiple 3D tensors
void halo_exchange_multiple(std::vector<torch::Tensor>& tensors, bool force_full_halo = false);

/**
 * Exchange halo regions for 3D fields on mass points
 * Handles all boundary condition types following WRF's approach
 * Input tensor shape: [nz, ny, nx] with halo regions included
 * 
 * WRF-CONSISTENT: Handle all boundary condition types for scalar fields
 */
inline void exchange_halo_3d_mass(
    torch::Tensor& field,
    int halo_size,
    bool periodic_x,
    bool periodic_y,
    int its, int ite, int jts, int jte,  // WRF tile indices
    int ids, int ide, int jds, int jde,  // WRF domain indices
    bool symmetric_xs = false, bool symmetric_xe = false,
    bool symmetric_ys = false, bool symmetric_ye = false,
    bool open_xs = false, bool open_xe = false,
    bool open_ys = false, bool open_ye = false)
{
    // Disable gradient tracking for halo exchange operations
    torch::NoGradGuard no_grad;
    
    // First, perform MPI halo exchange for distributed memory
    halo_exchange_3d_tensor(field);
    
    // Then apply boundary conditions based on WRF's priority order
    // Priority: periodic > symmetric > open > specified
    
    using namespace torch::indexing;
    // CRITICAL FIX: Correct dimension extraction for (j,k,i) memory layout
    auto ny = field.size(0);  // j dimension (south-north)
    auto nz = field.size(1);  // k dimension (vertical)
    (void)nz;                 // documented for layout clarity; not used in boundary ops
    auto nx = field.size(2);  // i dimension (west-east)
    
    // West-East boundaries
    if (periodic_x) {
#if WRF_SDIRK3_WRF_HANDLES_BC
        // FIX 2025-01-25: WRF handles periodic BC via MPI - skip C++ application
        // This prevents double-application which corrupts boundary values.
        // Set WRF_SDIRK3_WRF_HANDLES_BC=0 only for standalone C++ tests.
        (void)halo_size;  // Suppress unused warning
#else
        // WRF 1:1 COMPATIBLE: Periodic boundary conditions
        // Interior domain: [halo_size : nx-halo_size)
        // West halo: [0 : halo_size), East halo: [nx-halo_size : nx)
        int i_start = halo_size;         // First interior point
        int i_end = nx - halo_size;      // Last interior point + 1

        // FIX 2026-01-31 Tier3: Vectorized periodic halo — slice copy replaces j-k-h triple loop.
        field.slice(2, 0, halo_size).copy_(field.slice(2, i_end - halo_size, i_end));
        field.slice(2, i_end, i_end + halo_size).copy_(field.slice(2, i_start, i_start + halo_size));
#endif  // WRF_SDIRK3_WRF_HANDLES_BC
    } else {
        // FIX 2026-01-31 Tier3: Vectorized boundary copy — select replaces j-k double loop.
        if ((symmetric_xs || open_xs) && its == ids) {
            field.select(2, 0).copy_(field.select(2, 1));
        }
        if ((symmetric_xe || open_xe) && ite == ide) {
            field.select(2, nx - 1).copy_(field.select(2, nx - 2));
        }
    }

    // South-North boundaries
    if (periodic_y) {
#if WRF_SDIRK3_WRF_HANDLES_BC
        // FIX 2025-01-25: WRF handles periodic BC via MPI - skip C++ application
        (void)0;  // No-op when WRF handles BC
#else
        // FIX 2026-01-31 Tier3: Vectorized periodic halo — slice copy replaces k-i-h triple loop.
        int j_start = halo_size;
        int j_end = ny - halo_size;
        field.slice(0, 0, halo_size).copy_(field.slice(0, j_end - halo_size, j_end));
        field.slice(0, j_end, j_end + halo_size).copy_(field.slice(0, j_start, j_start + halo_size));
#endif  // WRF_SDIRK3_WRF_HANDLES_BC
    } else {
        // FIX 2026-01-31 Tier3: Vectorized boundary copy — select replaces k-i double loop.
        if ((symmetric_ys || open_ys) && jts == jds) {
            field.select(0, 0).copy_(field.select(0, 1));
        }
        if ((symmetric_ye || open_ye) && jte == jde) {
            field.select(0, ny - 1).copy_(field.select(0, ny - 2));
        }
    }
}

/**
 * Exchange halo regions for U-staggered fields
 * U is staggered in x-direction: [nz, ny, nx+1]
 * 
 * WRF-CONSISTENT: Handle all boundary condition types following WRF's approach
 */
inline void exchange_halo_3d_u(
    torch::Tensor& u_field,
    int halo_size,
    bool periodic_x,
    bool periodic_y,
    int its, int ite, int jts, int jte,
    int ids, int ide, int jds, int jde,
    bool symmetric_xs = false, bool symmetric_xe = false,
    bool symmetric_ys = false, bool symmetric_ye = false,
    bool open_xs = false, bool open_xe = false,
    bool open_ys = false, bool open_ye = false)
{
    // Disable gradient tracking for halo exchange operations
    torch::NoGradGuard no_grad;
    
    // First, perform MPI halo exchange for distributed memory
    halo_exchange_3d_tensor(u_field);
    
    // Then apply boundary conditions based on WRF's priority order
    // Priority: periodic > symmetric > open > specified
    
    using namespace torch::indexing;
    // CRITICAL FIX: Correct dimension extraction for U-staggered (j,k,i) layout
    auto ny = u_field.size(0);    // j dimension
    auto nz = u_field.size(1);    // k dimension
    (void)nz;                     // documented for layout clarity; not used in boundary ops
    auto nx_u = u_field.size(2);  // i dimension (staggered, nx+1)
    
    // West-East boundaries
    if (periodic_x) {
#if WRF_SDIRK3_WRF_HANDLES_BC
        // FIX 2025-01-25: WRF handles periodic BC via MPI - skip C++ application
        (void)halo_size;  // Suppress unused warning
#else
        // WRF 1:1 COMPATIBLE: Periodic boundary conditions for U-staggered
        int i_start = halo_size;           // First interior point
        int i_end = nx_u - halo_size;      // Last interior point + 1

        // FIX 2026-01-31 Tier3: Vectorized periodic halo — slice copy replaces j-k-h triple loop.
        u_field.slice(2, 0, halo_size).copy_(u_field.slice(2, i_end - halo_size, i_end));
        u_field.slice(2, i_end, i_end + halo_size).copy_(u_field.slice(2, i_start, i_start + halo_size));
#endif  // WRF_SDIRK3_WRF_HANDLES_BC
    } else {
        // FIX 2026-01-31 Tier3: Vectorized U boundary — select replaces j-k double loop.
        if (symmetric_xs && its == ids) {
            u_field.select(2, 0).copy_(-u_field.select(2, 2));  // antisymmetric
        } else if (open_xs && its == ids) {
            u_field.select(2, 0).copy_(u_field.select(2, 1));
        }
        if (symmetric_xe && ite == ide) {
            u_field.select(2, nx_u - 1).copy_(-u_field.select(2, nx_u - 3));  // antisymmetric
        } else if (open_xe && ite == ide) {
            u_field.select(2, nx_u - 1).copy_(u_field.select(2, nx_u - 2));
        }
    }

    // South-North boundaries (U is not staggered in y)
    if (periodic_y) {
#if WRF_SDIRK3_WRF_HANDLES_BC
        // FIX 2025-01-25: WRF handles periodic BC via MPI - skip C++ application
        (void)0;  // No-op when WRF handles BC
#else
        // FIX 2026-01-31 Tier3: Vectorized periodic halo — slice copy replaces k-i-h triple loop.
        int j_start = halo_size;
        int j_end = ny - halo_size;
        u_field.slice(0, 0, halo_size).copy_(u_field.slice(0, j_end - halo_size, j_end));
        u_field.slice(0, j_end, j_end + halo_size).copy_(u_field.slice(0, j_start, j_start + halo_size));
#endif  // WRF_SDIRK3_WRF_HANDLES_BC
    } else {
        // FIX 2026-01-31 Tier3: Vectorized U Y-boundary — select replaces k-i double loop.
        if ((symmetric_ys || open_ys) && jts == jds) {
            u_field.select(0, 0).copy_(u_field.select(0, 1));
        }
        if ((symmetric_ye || open_ye) && jte == jde) {
            u_field.select(0, ny - 1).copy_(u_field.select(0, ny - 2));
        }
    }
}

/**
 * Exchange halo regions for V-staggered fields
 * V is staggered in y-direction: [nz, ny+1, nx]
 * 
 * WRF-CONSISTENT: Handle all boundary condition types following WRF's approach
 */
inline void exchange_halo_3d_v(
    torch::Tensor& v_field,
    int halo_size,
    bool periodic_x,
    bool periodic_y,
    int its, int ite, int jts, int jte,
    int ids, int ide, int jds, int jde,
    bool symmetric_xs = false, bool symmetric_xe = false,
    bool symmetric_ys = false, bool symmetric_ye = false,
    bool open_xs = false, bool open_xe = false,
    bool open_ys = false, bool open_ye = false)
{
    // Disable gradient tracking for halo exchange operations
    torch::NoGradGuard no_grad;
    
    // First, perform MPI halo exchange for distributed memory
    halo_exchange_3d_tensor(v_field);
    
    // Then apply boundary conditions based on WRF's priority order
    // Priority: periodic > symmetric > open > specified
    
    using namespace torch::indexing;
    // CRITICAL FIX: Correct dimension extraction for V-staggered (j,k,i) layout
    auto ny_v = v_field.size(0);  // j dimension (staggered, ny+1)
    auto nz = v_field.size(1);    // k dimension
    (void)nz;                     // documented for layout clarity; not used in boundary ops
    auto nx = v_field.size(2);    // i dimension
    
    // West-East boundaries (V is not staggered in x)
    if (periodic_x) {
#if WRF_SDIRK3_WRF_HANDLES_BC
        // FIX 2025-01-25: WRF handles periodic BC via MPI - skip C++ application
        (void)halo_size;  // Suppress unused warning
#else
        // WRF 1:1 COMPATIBLE: Periodic boundary conditions
        int i_start = halo_size;         // First interior point
        int i_end = nx - halo_size;      // Last interior point + 1

        // FIX 2026-01-31 Tier3: Vectorized periodic halo — slice copy replaces j-k-h triple loop.
        v_field.slice(2, 0, halo_size).copy_(v_field.slice(2, i_end - halo_size, i_end));
        v_field.slice(2, i_end, i_end + halo_size).copy_(v_field.slice(2, i_start, i_start + halo_size));
#endif  // WRF_SDIRK3_WRF_HANDLES_BC
    } else {
        // FIX 2026-01-31 Tier3: Vectorized V X-boundary — select replaces j-k double loop.
        if ((symmetric_xs || open_xs) && its == ids) {
            v_field.select(2, 0).copy_(v_field.select(2, 1));
        }
        if ((symmetric_xe || open_xe) && ite == ide) {
            v_field.select(2, nx - 1).copy_(v_field.select(2, nx - 2));
        }
    }

    // South-North boundaries (V is staggered in y)
    if (periodic_y) {
#if WRF_SDIRK3_WRF_HANDLES_BC
        // FIX 2025-01-25: WRF handles periodic BC via MPI - skip C++ application
        (void)0;  // No-op when WRF handles BC
#else
        // FIX 2026-01-31 Tier3: Vectorized periodic halo — slice copy replaces k-i-h triple loop.
        int j_start = halo_size;
        int j_end = ny_v - halo_size;
        v_field.slice(0, 0, halo_size).copy_(v_field.slice(0, j_end - halo_size, j_end));
        v_field.slice(0, j_end, j_end + halo_size).copy_(v_field.slice(0, j_start, j_start + halo_size));
#endif  // WRF_SDIRK3_WRF_HANDLES_BC
    } else {
        // FIX 2026-01-31 Tier3: Vectorized V Y-boundary — select replaces k-i double loop.
        if (symmetric_ys && jts == jds) {
            v_field.select(0, 0).copy_(-v_field.select(0, 2));  // antisymmetric
        } else if (open_ys && jts == jds) {
            v_field.select(0, 0).copy_(v_field.select(0, 1));
        }
        if (symmetric_ye && jte == jde) {
            v_field.select(0, ny_v - 1).copy_(-v_field.select(0, ny_v - 3));  // antisymmetric
        } else if (open_ye && jte == jde) {
            v_field.select(0, ny_v - 1).copy_(v_field.select(0, ny_v - 2));
        }
    }
}

/**
 * Exchange halo regions for W-staggered fields
 * W is staggered in z-direction: [nz+1, ny, nx]
 * 
 * WRF-CONSISTENT: W is only staggered in vertical, so horizontal boundary 
 * conditions are the same as mass points (scalar fields)
 */
inline void exchange_halo_3d_w(
    torch::Tensor& w_field,
    int halo_size,
    bool periodic_x,
    bool periodic_y,
    int its, int ite, int jts, int jte,
    int ids, int ide, int jds, int jde,
    bool symmetric_xs = false, bool symmetric_xe = false,
    bool symmetric_ys = false, bool symmetric_ye = false,
    bool open_xs = false, bool open_xe = false,
    bool open_ys = false, bool open_ye = false)
{
    // Disable gradient tracking for halo exchange operations
    torch::NoGradGuard no_grad;
    
    // W is only staggered in vertical, so horizontal halo exchange
    // is the same as mass points
    exchange_halo_3d_mass(w_field, halo_size, periodic_x, periodic_y,
                          its, ite, jts, jte, ids, ide, jds, jde,
                          symmetric_xs, symmetric_xe, symmetric_ys, symmetric_ye,
                          open_xs, open_xe, open_ys, open_ye);
}

// =============================================================================
// Boundary Condition Application
// =============================================================================

/**
 * Apply open boundary radiation conditions
 * Based on WRF's module_bc.F radiation boundary conditions
 */
inline void apply_radiation_bc(
    torch::Tensor& field,
    const torch::Tensor& field_old,
    const torch::Tensor& phase_speed,
    float dt,
    bool open_xs, bool open_xe,
    bool open_ys, bool open_ye,
    int its, int ite, int jts, int jte,
    int ids, int ide, int jds, int jde)
{
    // Disable gradient tracking for boundary operations
    torch::NoGradGuard no_grad;
    
    using namespace torch::indexing;
    
    // Radiation condition: ∂ϕ/∂t + c*∂ϕ/∂n = 0
    // where c is the phase speed and n is outward normal
    
    // FIX 2025-12-27: Use index_put_ instead of index()= (no-op in C++ PyTorch)
    if (open_xs && its == ids) {
        // Western boundary
        auto c_west = phase_speed.index({Slice(), Slice(), 0});
        auto grad_n = (field.index({Slice(), Slice(), 1}) -
                      field.index({Slice(), Slice(), 0})) / dt;
        field.index_put_({Slice(), Slice(), 0},
            field_old.index({Slice(), Slice(), 0}) - dt * c_west * grad_n);
    }

    if (open_xe && ite == ide) {
        // Eastern boundary
        int nx = field.size(2);
        auto c_east = phase_speed.index({Slice(), Slice(), nx-1});
        auto grad_n = (field.index({Slice(), Slice(), nx-1}) -
                      field.index({Slice(), Slice(), nx-2})) / dt;
        field.index_put_({Slice(), Slice(), nx-1},
            field_old.index({Slice(), Slice(), nx-1}) - dt * c_east * grad_n);
    }
    
    // FIX 2025-12-27: Use index_put_ instead of index()= (no-op in C++ PyTorch)
    if (open_ys && jts == jds) {
        // Southern boundary
        auto c_south = phase_speed.index({0, Slice(), Slice()});
        auto grad_n = (field.index({1, Slice(), Slice()}) -
                      field.index({0, Slice(), Slice()})) / dt;
        field.index_put_({0, Slice(), Slice()},
            field_old.index({0, Slice(), Slice()}) - dt * c_south * grad_n);
    }

    if (open_ye && jte == jde) {
        // Northern boundary
        int ny = field.size(0);
        auto c_north = phase_speed.index({ny-1, Slice(), Slice()});
        auto grad_n = (field.index({ny-1, Slice(), Slice()}) -
                      field.index({ny-2, Slice(), Slice()})) / dt;
        field.index_put_({ny-1, Slice(), Slice()},
            field_old.index({ny-1, Slice(), Slice()}) - dt * c_north * grad_n);
    }
}

/**
 * Apply specified boundary relaxation (nudging)
 * Based on WRF's specified boundary condition with fcx/gcx factors
 */
inline void apply_relaxation_bc(
    torch::Tensor& field,
    const torch::Tensor& field_specified,
    const torch::Tensor& fcx,  // Relaxation factor in x
    const torch::Tensor& gcx,  // Relaxation factor in x (complement)
    const torch::Tensor& fcy,  // Relaxation factor in y
    const torch::Tensor& gcy,  // Relaxation factor in y (complement)
    int spec_bdy_width)
{
    // Disable gradient tracking for boundary operations
    torch::NoGradGuard no_grad;
    
    using namespace torch::indexing;
    
    // Apply relaxation: field = fcx * field_specified + gcx * field
    // fcx = 1 at boundary, 0 in interior
    // gcx = 0 at boundary, 1 in interior
    
    int ny = field.size(0);
    int nz = field.size(1);
    int nx = field.size(2);
    
    // FIX 2025-12-27: Use index_put_ instead of index()= (no-op in C++ PyTorch)
    // Western boundary zone
    if (spec_bdy_width > 0) {
        auto zone = Slice(0, spec_bdy_width);
        auto fcx_3d = fcx.index({zone}).unsqueeze(0).unsqueeze(1).expand({ny, nz, spec_bdy_width});
        auto gcx_3d = gcx.index({zone}).unsqueeze(0).unsqueeze(1).expand({ny, nz, spec_bdy_width});

        field.index_put_({Slice(), Slice(), zone},
            fcx_3d * field_specified.index({Slice(), Slice(), zone}) +
            gcx_3d * field.index({Slice(), Slice(), zone}));
    }

    // Eastern boundary zone
    if (spec_bdy_width > 0) {
        auto zone = Slice(nx - spec_bdy_width, nx);
        auto fcx_3d = fcx.index({zone}).unsqueeze(0).unsqueeze(1).expand({ny, nz, spec_bdy_width});
        auto gcx_3d = gcx.index({zone}).unsqueeze(0).unsqueeze(1).expand({ny, nz, spec_bdy_width});

        field.index_put_({Slice(), Slice(), zone},
            fcx_3d * field_specified.index({Slice(), Slice(), zone}) +
            gcx_3d * field.index({Slice(), Slice(), zone}));
    }

    // Similar for y-boundaries
    // Southern boundary zone
    if (spec_bdy_width > 0) {
        auto zone = Slice(0, spec_bdy_width);
        auto fcy_3d = fcy.index({zone}).unsqueeze(1).unsqueeze(2).expand({spec_bdy_width, nz, nx});
        auto gcy_3d = gcy.index({zone}).unsqueeze(1).unsqueeze(2).expand({spec_bdy_width, nz, nx});

        field.index_put_({zone, Slice(), Slice()},
            fcy_3d * field_specified.index({zone, Slice(), Slice()}) +
            gcy_3d * field.index({zone, Slice(), Slice()}));
    }

    // Northern boundary zone
    if (spec_bdy_width > 0) {
        auto zone = Slice(ny - spec_bdy_width, ny);
        auto fcy_3d = fcy.index({zone}).unsqueeze(1).unsqueeze(2).expand({spec_bdy_width, nz, nx});
        auto gcy_3d = gcy.index({zone}).unsqueeze(1).unsqueeze(2).expand({spec_bdy_width, nz, nx});

        field.index_put_({zone, Slice(), Slice()},
            fcy_3d * field_specified.index({zone, Slice(), Slice()}) +
            gcy_3d * field.index({zone, Slice(), Slice()}));
    }
}

/**
 * Apply all boundary conditions based on configuration
 */
inline void apply_boundary_conditions(
    torch::Tensor& field,
    const torch::Tensor& field_old,
    const torch::Tensor& field_specified,
    const BoundaryConfig& bc,
    const torch::Tensor& phase_speed,
    const torch::Tensor& fcx,
    const torch::Tensor& gcx,
    const torch::Tensor& fcy,
    const torch::Tensor& gcy,
    float dt,
    int spec_bdy_width,
    int its, int ite, int jts, int jte,
    int ids, int ide, int jds, int jde)
{
    // Disable gradient tracking for boundary operations
    torch::NoGradGuard no_grad;
    
    // Apply open boundary radiation conditions
    if (bc.open_xs || bc.open_xe || bc.open_ys || bc.open_ye) {
        apply_radiation_bc(field, field_old, phase_speed, dt,
                          bc.open_xs, bc.open_xe, bc.open_ys, bc.open_ye,
                          its, ite, jts, jte, ids, ide, jds, jde);
    }
    
    // Apply specified boundary relaxation
    if (bc.specified && field_specified.defined()) {
        apply_relaxation_bc(field, field_specified, fcx, gcx, fcy, gcy, spec_bdy_width);
    }
    
    // FUTURE: Nested boundary interpolation (bc.nested) - WRF nested domain support
    //         Requires: parent domain field, weight tensors, feedback logic
    //         Reference: WRF share/module_bc.F (bdy_interp1, bdy_interp2)

    // FUTURE: Symmetric boundary conditions (bc.symmetric) - channel flow/idealized cases
    //         Requires: reflection logic for u/v/w with sign handling
    //         Reference: WRF share/module_bc.F (set_physical_bc2d SYMMETRIC case)
}

} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_HALO_EXCHANGE_H
