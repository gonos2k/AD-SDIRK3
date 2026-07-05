#ifndef WRF_SDIRK3_BOUNDARY_HANDLER_H
#define WRF_SDIRK3_BOUNDARY_HANDLER_H

#include <algorithm>

namespace wrf {
namespace sdirk3 {

// WRF Index Convention Handler
// WRF uses Fortran 1-based indexing with the following conventions:
// - ids:ide, jds:jde, kds:kde : Domain indices (full domain)
// - ims:ime, jms:jme, kms:kme : Memory indices (includes halo regions)
// - its:ite, jts:jte, kts:kte : Tile indices (computational region)
//
// This class converts between WRF's Fortran indices and C++ 0-based indices
// while respecting boundary conditions and stencil requirements

struct BoundaryConfig {
    bool periodic_x = false;
    bool periodic_y = false;
    bool symmetric_xs = false;
    bool symmetric_xe = false;
    bool symmetric_ys = false;
    bool symmetric_ye = false;
    bool open_xs = false;
    bool open_xe = false;
    bool open_ys = false;
    bool open_ye = false;
    bool specified = false;
    bool nested = false;
    bool polar = false;
};

class BoundaryHandler {
private:
    // WRF tile indices (1-based, passed from Fortran)
    int its_, ite_, jts_, jte_;
    [[maybe_unused]] int kts_, kte_;  // kept for future vertical boundary handling
    
    // WRF memory indices (1-based, includes halo)
    [[maybe_unused]] int ims_, ime_, jms_, jme_, kms_, kme_;  // kept for future halo exchange
    
    // WRF domain indices (1-based, full domain)
    int ids_, ide_, jds_, jde_;
    [[maybe_unused]] int kds_, kde_;  // kept for future vertical domain handling
    
    // C++ array dimensions (0-based)
    int nx_, ny_;
    [[maybe_unused]] int nz_;  // kept for future 3D boundary handling
    
    // Boundary configuration
    BoundaryConfig bc_;
    
    // Stencil sizes for different operations
    static constexpr int ADVECT_STENCIL_5TH = 3;  // 5th order needs 3 points on each side
    static constexpr int ADVECT_STENCIL_3RD = 2;  // 3rd order needs 2 points on each side
    static constexpr int PRESSURE_STENCIL = 1;    // Pressure gradient needs 1 point
    
public:
    BoundaryHandler(int its, int ite, int jts, int jte, int kts, int kte,
                    int ims, int ime, int jms, int jme, int kms, int kme,
                    int ids, int ide, int jds, int jde, int kds, int kde,
                    int nx, int ny, int nz, const BoundaryConfig& bc)
        : its_(its), ite_(ite), jts_(jts), jte_(jte), kts_(kts), kte_(kte),
          ims_(ims), ime_(ime), jms_(jms), jme_(jme), kms_(kms), kme_(kme),
          ids_(ids), ide_(ide), jds_(jds), jde_(jde), kds_(kds), kde_(kde),
          nx_(nx), ny_(ny), nz_(nz), bc_(bc) {}
    
    // Get loop bounds for U-momentum advection (5th order)
    void get_u_advection_bounds(int& i_start, int& i_end, int& j_start, int& j_end) const {
        // For U-staggered variable with 5th order advection
        
        // X-direction: U is staggered, so we have nx+1 points
        // Need 3 points on each side for 5th order
        i_start = ADVECT_STENCIL_5TH;
        i_end = nx_ - ADVECT_STENCIL_5TH + 1;  // +1 because U is staggered
        
        // Adjust for boundary conditions
        if (bc_.periodic_x) {
            i_start = 0;
            i_end = nx_ + 1;
        } else if (bc_.open_xs || bc_.specified) {
            i_start = std::max(1, i_start);  // Can't use i=0 at open boundary
        }
        
        // Y-direction: U is not staggered in y
        j_start = ADVECT_STENCIL_5TH;
        j_end = ny_ - ADVECT_STENCIL_5TH;
        
        if (bc_.periodic_y) {
            j_start = 0;
            j_end = ny_;
        } else if (bc_.open_ys || bc_.specified) {
            j_start = std::max(1, j_start);
        }
        
        // Degrade to lower order near boundaries if needed
        if (!bc_.periodic_x && !bc_.symmetric_xs && (its_ <= ids_ + 3)) {
            i_start = std::max(1, i_start - 1);  // Degrade to 3rd order
        }
        if (!bc_.periodic_x && !bc_.symmetric_xe && (ite_ >= ide_ - 3)) {
            i_end = std::min(nx_, i_end + 1);
        }
    }
    
    // Get loop bounds for V-momentum advection (5th order)
    void get_v_advection_bounds(int& i_start, int& i_end, int& j_start, int& j_end) const {
        // For V-staggered variable with 5th order advection
        
        // X-direction: V is not staggered in x
        i_start = ADVECT_STENCIL_5TH;
        i_end = nx_ - ADVECT_STENCIL_5TH;
        
        if (bc_.periodic_x) {
            i_start = 0;
            i_end = nx_;
        }
        
        // Y-direction: V is staggered, so we have ny+1 points
        j_start = ADVECT_STENCIL_5TH;
        j_end = ny_ - ADVECT_STENCIL_5TH + 1;  // +1 because V is staggered
        
        if (bc_.periodic_y) {
            j_start = 0;
            j_end = ny_ + 1;
        } else if (bc_.open_ys || bc_.specified) {
            j_start = std::max(1, j_start);
        }
    }
    
    // Get loop bounds for W-momentum advection (5th order)
    void get_w_advection_bounds(int& i_start, int& i_end, int& j_start, int& j_end) const {
        // For W-staggered variable (staggered in z only)
        
        // Both x and y are not staggered for W
        i_start = ADVECT_STENCIL_5TH;
        i_end = nx_ - ADVECT_STENCIL_5TH;
        j_start = ADVECT_STENCIL_5TH;
        j_end = ny_ - ADVECT_STENCIL_5TH;
        
        if (bc_.periodic_x) {
            i_start = 0;
            i_end = nx_;
        }
        if (bc_.periodic_y) {
            j_start = 0;
            j_end = ny_;
        }
    }
    
    // Get loop bounds for pressure gradient calculations
    void get_pressure_gradient_bounds_u(int& i_start, int& i_end, int& j_start, int& j_end) const {
        // Pressure gradient for U needs only 1 point stencil
        i_start = 1;  // Can't compute at i=0 (need i-1)
        i_end = nx_;  // U is staggered, goes to nx+1
        
        j_start = 0;
        j_end = ny_;
        
        // Adjust for specified/nested boundaries
        if ((bc_.open_xs || bc_.specified) && its_ == ids_) {
            i_start = 2;  // Skip first point at open boundary
        }
        if ((bc_.open_xe || bc_.specified) && ite_ == ide_) {
            i_end = nx_ - 1;  // Skip last point at open boundary
        }
    }
    
    // Get loop bounds for pressure gradient calculations
    void get_pressure_gradient_bounds_v(int& i_start, int& i_end, int& j_start, int& j_end) const {
        // Pressure gradient for V needs only 1 point stencil
        i_start = 0;
        i_end = nx_;
        
        j_start = 1;  // Can't compute at j=0 (need j-1)
        j_end = ny_;  // V is staggered, goes to ny+1
        
        // Adjust for specified/nested boundaries
        if ((bc_.open_ys || bc_.specified) && jts_ == jds_) {
            j_start = 2;  // Skip first point at open boundary
        }
        if ((bc_.open_ye || bc_.specified) && jte_ == jde_) {
            j_end = ny_ - 1;  // Skip last point at open boundary
        }
    }
    
    // Get loop bounds for scalar advection (e.g., theta, for 5th order)
    void get_scalar_advection_bounds(int& i_start, int& i_end, int& j_start, int& j_end) const {
        // Scalars are at mass points (not staggered)
        i_start = ADVECT_STENCIL_5TH;
        i_end = nx_ - ADVECT_STENCIL_5TH;
        j_start = ADVECT_STENCIL_5TH;
        j_end = ny_ - ADVECT_STENCIL_5TH;
        
        if (bc_.periodic_x) {
            i_start = 0;
            i_end = nx_;
        }
        if (bc_.periodic_y) {
            j_start = 0;
            j_end = ny_;
        }
    }
    
    // Get loop bounds for mass continuity equation
    void get_mass_continuity_bounds(int& i_start, int& i_end, int& j_start, int& j_end) const {
        // Mass continuity needs flux divergence, so 1 point on each side
        i_start = 1;
        i_end = nx_ - 1;
        j_start = 1;
        j_end = ny_ - 1;
        
        // At open boundaries, we may need special treatment
        if ((bc_.open_xs || bc_.specified) && its_ == ids_) {
            i_start = 2;
        }
        if ((bc_.open_xe || bc_.specified) && ite_ == ide_) {
            i_end = nx_ - 2;
        }
        if ((bc_.open_ys || bc_.specified) && jts_ == jds_) {
            j_start = 2;
        }
        if ((bc_.open_ye || bc_.specified) && jte_ == jde_) {
            j_end = ny_ - 2;
        }
    }
    
    // Check if we need to apply boundary conditions at a specific location
    bool needs_boundary_condition(int i, int j, int var_type) const {
        // var_type: 0=U, 1=V, 2=W, 3=scalar
        
        // Check if we're at a boundary that needs special treatment
        bool at_xs = (i <= 2) && !bc_.periodic_x;
        bool at_xe = (i >= nx_ - 2) && !bc_.periodic_x;
        bool at_ys = (j <= 2) && !bc_.periodic_y;
        bool at_ye = (j >= ny_ - 2) && !bc_.periodic_y;
        
        return (at_xs || at_xe || at_ys || at_ye) && 
               (bc_.open_xs || bc_.open_xe || bc_.open_ys || bc_.open_ye || 
                bc_.specified || bc_.nested);
    }
    
    // Get the order of advection to use at a specific location
    int get_advection_order(int i, int j, bool is_horizontal) const {
        // Check if we need to degrade order near boundaries
        if (is_horizontal) {
            bool near_x_boundary = (i < ADVECT_STENCIL_5TH || i > nx_ - ADVECT_STENCIL_5TH);
            bool near_y_boundary = (j < ADVECT_STENCIL_5TH || j > ny_ - ADVECT_STENCIL_5TH);
            
            if ((near_x_boundary && !bc_.periodic_x && !bc_.symmetric_xs && !bc_.symmetric_xe) ||
                (near_y_boundary && !bc_.periodic_y && !bc_.symmetric_ys && !bc_.symmetric_ye)) {
                return 3;  // Degrade to 3rd order
            }
        }
        return 5;  // Use 5th order
    }
};

} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_BOUNDARY_HANDLER_H