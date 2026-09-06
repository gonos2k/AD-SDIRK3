#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>

#include <torch/torch.h>

#include "wrf_sdirk3_config.h"
#include "wrf_sdirk3_types.h"
#include "wrf_sdirk3_unified_preconditioner.h"
#include "wrf_sdirk3_unified_rhs.h"

using wrf::sdirk3::PhysicsConfig;
using wrf::sdirk3::UnifiedPreconditioner;
using wrf::sdirk3::WRFGridInfo;

int main() {
    try {
        auto& cfg = wrf::sdirk3::g_sdirk3_config;
        // This is the unchanged production no-preconditioner default used by
        // PC0.  The normal build contains the raw implementation, but the
        // existing selector must leave it inactive when type 0 is selected.
        cfg.precond_type = 0;
        cfg.mass_coordinate_mode = static_cast<int>(
            wrf::sdirk3::SDIRK3Config::MassCoordinateMode::Legacy);

        auto grid = std::make_shared<WRFGridInfo>();
        grid->nx = 1; grid->ny = 1; grid->nz = 3;
        grid->nx_u = 1; grid->ny_v = 1; grid->nz_w = 4;
        grid->p_base = torch::full({1, 3, 1}, 100000.0f);
        grid->alb = torch::full({1, 3, 1}, 1.0f);
        grid->mu_base = torch::ones({1, 1});
        grid->c1h = torch::ones({3});
        grid->c2h = torch::zeros({3});
        grid->c1f = torch::ones({4});
        grid->c2f = torch::zeros({4});
        grid->rdnw = torch::ones({4});
        grid->rdn = torch::ones({4});
        grid->msfty = torch::ones({1, 1});

        auto physics = std::make_shared<PhysicsConfig>();
        UnifiedPreconditioner precond(grid, physics, 1.0f, 0.5f);
        if (precond.raw_principal_enabled()) {
            throw std::runtime_error("precond_type=0 unexpectedly selected raw principal");
        }
        std::cout << "raw-principal selector default-off (precond_type=0): PASS\n";
        return EXIT_SUCCESS;
    } catch (const c10::Error& e) {
        std::cerr << "raw-principal selector default-off: FAIL c10: " << e.what() << "\n";
        return EXIT_FAILURE;
    } catch (const std::exception& e) {
        std::cerr << "raw-principal selector default-off: FAIL: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
}
