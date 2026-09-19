#include <wrf_sdirk3/wrf_sdirk3_interface.h>

int main(void)
{
    void *solver = 0;
    float dt = 0.1f;
    float lambda_terminal = 0.0f;
    float lambda_initial = 0.0f;

    (void)sdirk3_tile_solver_begin_fixed_trajectory_zerocopy(
        solver, 1, &dt, 1);
    (void)sdirk3_tile_solver_pullback_fixed_trajectory_zerocopy(
        solver, &lambda_terminal, 1, &lambda_initial);
    return sdirk3_tile_solver_close_fixed_trajectory_zerocopy(solver);
}
