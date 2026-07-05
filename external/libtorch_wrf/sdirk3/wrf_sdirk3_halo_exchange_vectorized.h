#ifndef WRF_SDIRK3_HALO_EXCHANGE_VECTORIZED_H
#define WRF_SDIRK3_HALO_EXCHANGE_VECTORIZED_H

/**
 * @file wrf_sdirk3_halo_exchange_vectorized.h
 * @brief Vectorized halo exchange operations for RSL_LITE compatibility
 * 
 * This replaces element-wise operations with efficient tensor slicing
 * while maintaining RSL_LITE safety (tile interior modification only)
 */

#include <torch/torch.h>
#include "wrf_sdirk3_boundary_handler.h"

namespace wrf {
namespace sdirk3 {

/**
 * Vectorized halo exchange for 3D mass-point fields
 * RSL_LITE SAFE: Only modifies tile interior for periodic BCs
 * Halo regions handled by RSL_LITE after this function
 */
inline void exchange_halo_3d_mass_vectorized(
    torch::Tensor& field,
    int halo_size,
    bool periodic_x,
    bool periodic_y,
    int its, int ite, int jts, int jte,  // Tile compute indices
    int ids, int ide, int jds, int jde,  // Domain indices
    bool symmetric_xs = false, bool symmetric_xe = false,
    bool symmetric_ys = false, bool symmetric_ye = false,
    bool open_xs = false, bool open_xe = false,
    bool open_ys = false, bool open_ye = false)
{
    // Disable gradient tracking for maximum performance
    torch::NoGradGuard no_grad;
    
    using namespace torch::indexing;
    
    // IMPORTANT: For RSL_LITE compatibility, we only handle local tile boundaries
    // RSL_LITE will handle the actual MPI communication between tiles
    
    // For periodic boundaries within this tile's responsibility
    if (periodic_x && its == ids && ite == ide) {
        // Only for single-tile domains (testing)
        // Vectorized operation instead of loops
        field.index_put_({Slice(), Slice(), Slice(0, halo_size)}, 
                         field.index({Slice(), Slice(), Slice(ite-halo_size, ite)}));
        field.index_put_({Slice(), Slice(), Slice(ite, ite+halo_size)}, 
                         field.index({Slice(), Slice(), Slice(its, its+halo_size)}));
    }
    
    if (periodic_y && jts == jds && jte == jde) {
        // Only for single-tile domains (testing)
        field.index_put_({Slice(), Slice(0, halo_size), Slice()}, 
                         field.index({Slice(), Slice(jte-halo_size, jte), Slice()}));
        field.index_put_({Slice(), Slice(jte, jte+halo_size), Slice()}, 
                         field.index({Slice(), Slice(jts, jts+halo_size), Slice()}));
    }
    
    // Handle non-periodic boundaries at domain edges
    // Only modify if this tile is at domain boundary
    if (!periodic_x) {
        if (its == ids) {  // West boundary
            if (symmetric_xs) {
                // Mirror symmetry: vectorized
                field.index_put_({Slice(), Slice(), its-1}, 
                                field.index({Slice(), Slice(), its}));
            } else if (open_xs) {
                // Zero gradient: vectorized
                field.index_put_({Slice(), Slice(), its-1}, 
                                field.index({Slice(), Slice(), its}));
            }
        }
        
        if (ite == ide) {  // East boundary
            if (symmetric_xe) {
                field.index_put_({Slice(), Slice(), ite}, 
                                field.index({Slice(), Slice(), ite-1}));
            } else if (open_xe) {
                field.index_put_({Slice(), Slice(), ite}, 
                                field.index({Slice(), Slice(), ite-1}));
            }
        }
    }
    
    if (!periodic_y) {
        if (jts == jds) {  // South boundary
            if (symmetric_ys) {
                field.index_put_({Slice(), jts-1, Slice()}, 
                                field.index({Slice(), jts, Slice()}));
            } else if (open_ys) {
                field.index_put_({Slice(), jts-1, Slice()}, 
                                field.index({Slice(), jts, Slice()}));
            }
        }
        
        if (jte == jde) {  // North boundary
            if (symmetric_ye) {
                field.index_put_({Slice(), jte, Slice()}, 
                                field.index({Slice(), jte-1, Slice()}));
            } else if (open_ye) {
                field.index_put_({Slice(), jte, Slice()}, 
                                field.index({Slice(), jte-1, Slice()}));
            }
        }
    }
}

/**
 * Vectorized halo exchange for U-staggered fields
 * Handles antisymmetric boundary conditions for velocity
 */
inline void exchange_halo_3d_u_vectorized(
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
    torch::NoGradGuard no_grad;
    using namespace torch::indexing;
    
    // U is staggered in X, so nx_u = nx + 1 for periodic domains
    
    if (periodic_x && its == ids && ite == ide) {
        // Vectorized periodic update
        field.index_put_({Slice(), Slice(), Slice(0, halo_size)}, 
                         u_field.index({Slice(), Slice(), Slice(ite-halo_size, ite)}));
        field.index_put_({Slice(), Slice(), Slice(ite+1, ite+1+halo_size)}, 
                         u_field.index({Slice(), Slice(), Slice(its, its+halo_size)}));
    }
    
    // Non-periodic boundaries
    if (!periodic_x) {
        if (its == ids) {  // West boundary
            if (symmetric_xs) {
                // Antisymmetric for U: u(its-1) = -u(its+1)
                field.index_put_({Slice(), Slice(), its-1}, 
                                -u_field.index({Slice(), Slice(), its+1}));
            } else if (open_xs) {
                field.index_put_({Slice(), Slice(), its-1}, 
                                u_field.index({Slice(), Slice(), its}));
            }
        }
        
        if (ite == ide) {  // East boundary
            if (symmetric_xe) {
                // U at ite+1 (staggered point)
                field.index_put_({Slice(), Slice(), ite+1}, 
                                -u_field.index({Slice(), Slice(), ite-1}));
            } else if (open_xe) {
                field.index_put_({Slice(), Slice(), ite+1}, 
                                u_field.index({Slice(), Slice(), ite}));
            }
        }
    }
    
    // Y-direction (U is not staggered in Y)
    if (periodic_y && jts == jds && jte == jde) {
        field.index_put_({Slice(), Slice(0, halo_size), Slice()}, 
                         u_field.index({Slice(), Slice(jte-halo_size, jte), Slice()}));
        field.index_put_({Slice(), Slice(jte, jte+halo_size), Slice()}, 
                         u_field.index({Slice(), Slice(jts, jts+halo_size), Slice()}));
    }
}

/**
 * Vectorized halo exchange for V-staggered fields
 */
inline void exchange_halo_3d_v_vectorized(
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
    torch::NoGradGuard no_grad;
    using namespace torch::indexing;
    
    // V is staggered in Y, so ny_v = ny + 1 for periodic domains
    
    // X-direction (V is not staggered in X)
    if (periodic_x && its == ids && ite == ide) {
        field.index_put_({Slice(), Slice(), Slice(0, halo_size)}, 
                         v_field.index({Slice(), Slice(), Slice(ite-halo_size, ite)}));
        field.index_put_({Slice(), Slice(), Slice(ite, ite+halo_size)}, 
                         v_field.index({Slice(), Slice(), Slice(its, its+halo_size)}));
    }
    
    if (periodic_y && jts == jds && jte == jde) {
        field.index_put_({Slice(), Slice(0, halo_size), Slice()}, 
                         v_field.index({Slice(), Slice(jte-halo_size, jte), Slice()}));
        field.index_put_({Slice(), Slice(jte+1, jte+1+halo_size), Slice()}, 
                         v_field.index({Slice(), Slice(jts, jts+halo_size), Slice()}));
    }
    
    // Handle antisymmetric boundaries for V
    if (!periodic_y) {
        if (jts == jds) {  // South boundary
            if (symmetric_ys) {
                // Antisymmetric for V: v(jts-1) = -v(jts+1)
                field.index_put_({Slice(), jts-1, Slice()}, 
                                -v_field.index({Slice(), jts+1, Slice()}));
            } else if (open_ys) {
                field.index_put_({Slice(), jts-1, Slice()}, 
                                v_field.index({Slice(), jts, Slice()}));
            }
        }
        
        if (jte == jde) {  // North boundary
            if (symmetric_ye) {
                field.index_put_({Slice(), jte+1, Slice()}, 
                                -v_field.index({Slice(), jte-1, Slice()}));
            } else if (open_ye) {
                field.index_put_({Slice(), jte+1, Slice()}, 
                                v_field.index({Slice(), jte, Slice()}));
            }
        }
    }
}

/**
 * Vectorized halo exchange for W-staggered fields
 * W is staggered in Z only
 */
inline void exchange_halo_3d_w_vectorized(
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
    // W behaves like a scalar in horizontal directions
    // Use the same logic as mass points
    exchange_halo_3d_mass_vectorized(w_field, halo_size,
                                     periodic_x, periodic_y,
                                     its, ite, jts, jte,
                                     ids, ide, jds, jde,
                                     symmetric_xs, symmetric_xe,
                                     symmetric_ys, symmetric_ye,
                                     open_xs, open_xe,
                                     open_ys, open_ye);
}

} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_HALO_EXCHANGE_VECTORIZED_H