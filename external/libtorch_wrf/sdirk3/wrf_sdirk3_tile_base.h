#ifndef WRF_SDIRK3_TILE_BASE_H
#define WRF_SDIRK3_TILE_BASE_H

#include <memory>
#include "wrf_sdirk3_torch_wrapper.h"

namespace wrf {
namespace sdirk3 {

// Base class for tile-based SDIRK3 solvers
class TileSDIRK3Solver {
public:
    TileSDIRK3Solver(int nx, int ny, int nz, 
                     double dx, double dy, double dt,
                     int tile_id)
        : nx_(nx), ny_(ny), nz_(nz),
          dx_(dx), dy_(dy), dt_(dt),
          tile_id_(tile_id) {}
    
    virtual ~TileSDIRK3Solver() = default;
    
    // Pure virtual methods to be implemented by derived classes
    virtual int solve(double* state_new, const double* state_old, 
                     const double* forcing, int stage_num) = 0;
    
    virtual void update_config(double dt, double dx, double dy) {
        dt_ = dt;
        dx_ = dx;
        dy_ = dy;
    }
    
protected:
    int nx_, ny_, nz_;
    double dx_, dy_, dt_;
    int tile_id_;
};

} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_TILE_BASE_H