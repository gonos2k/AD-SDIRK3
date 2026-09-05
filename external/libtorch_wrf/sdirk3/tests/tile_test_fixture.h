#pragma once

#include "../wrf_sdirk3_tile_unified.h"
#include "../wrf_sdirk3_config.h"
#include "../wrf_sdirk3_hydrostatic_balance.h"
#include "../wrf_hydrostatic_pressure.h"
#include "../wrf_sdirk3_acoustic_substep.h"
#include "../wrf_sdirk3_boundary_ad.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>

namespace wrf::sdirk3::test {
constexpr int nx = 8, ny = 6, nz = 4, nu = nx + 1, nv = ny + 1, nw = nz + 1;
constexpr int su = ny*nz*nu, sv = nv*nz*nx, sw = ny*nw*nx, st = ny*nz*nx, sm = ny*nx;
constexpr int total = su + sv + 2*sw + st + sm;

struct TileCase {
    std::vector<float> u = std::vector<float>(su), v = std::vector<float>(sv);
    std::vector<float> w = std::vector<float>(sw), ph = std::vector<float>(sw);
    std::vector<float> theta = std::vector<float>(st), mu = std::vector<float>(sm);
    std::vector<float> ru = std::vector<float>(su), rv = std::vector<float>(sv);
    std::vector<float> rw = std::vector<float>(sw), rph = std::vector<float>(sw);
    std::vector<float> rt = std::vector<float>(st), rm = std::vector<float>(sm);
    std::vector<float> metric = std::vector<float>(nw, -nz);
    std::vector<float> mass_map = std::vector<float>(sm, 1.0f);
    std::vector<float> u_map = std::vector<float>(ny*nu, 1.0f);
    std::vector<float> v_map = std::vector<float>(nv*nx, 1.0f);
    std::vector<float> one = std::vector<float>(nw, 1.0f);
    std::vector<float> zero = std::vector<float>(nw, 0.0f);
    std::vector<float> half = std::vector<float>(nw, 0.5f);
    std::vector<float> setup_state = std::vector<float>(nv*nw*nu, 0.0f);
    std::vector<float> setup_density = std::vector<float>(nv*nw*nu, 1.0f);
    std::vector<float> setup_f = std::vector<float>((nv+1)*(nu+1), 0.0f);
    std::vector<float> setup_zero = std::vector<float>((nv+1)*(nu+1), 0.0f);
    std::vector<float> setup_map = std::vector<float>((nv+1)*(nu+1), 1.0f);
    float spacing;
    TileSDIRK3UnifiedSolver solver;

    explicit TileCase(float grid_spacing = 100000.0f, float coriolis = 0.0f)
        : spacing(grid_spacing), solver(nx, ny, nz, spacing, spacing,
                       {1.0f/spacing}, {1.0f/spacing}, std::vector<float>(nz, nz), 0) {
        solver.setWRFIndices(1, nx+1, 1, ny+1, 1, nz,
                             1, nx+1, 1, ny+1, 1, nz+1,
                             -2, nx+4, -2, ny+4, 1, nz+1);
        solver.setBoundaryConditions(true, false, false, false, true, true,
                                     false, false, false, false);
        // Use the model's discrete hydrostatic balance, not its continuum limit.
        std::vector<float> pressure(st), t_initial(st, 0), phi(sw), mass(sm, 80000.0f);
        const auto p_column = torch::linspace(90000.0f, 30000.0f, nz, torch::kFloat32);
        const auto alpha_column = wrf::sdirk3::compute_inverse_density(
            torch::full_like(p_column, 300.0f), p_column, 287.0f, 717.5f, 1004.5f, 100000.0f);
        const std::vector<float> alpha(alpha_column.data_ptr<float>(), alpha_column.data_ptr<float>()+nz);
        const auto phi_column = wrf::sdirk3::integrate_phb_hydrostatic(
            std::vector<float>(nz,-nz), alpha, std::vector<float>(nz,1),
            std::vector<float>(nz,0), 80000.0f, 0.0f);
        for (int j=0; j<ny; ++j) {
            for (int k=0; k<nz; ++k)
                for (int i=0; i<nx; ++i)
                    pressure[(j*nz+k)*nx+i] = 100000.0f - (k+0.5f)*80000.0f/nz;
            for (int k=0; k<nw; ++k)
                for (int i=0; i<nx; ++i) {
                    phi[(j*nw+k)*nx+i] = phi_column[k];
                }
        }
        for (int j=0; j<nv; ++j)
            for (int k=0; k<nw; ++k)
                for (int i=0; i<nu; ++i)
                    setup_density[(j*nw+k)*nu+i] = alpha[std::min(k,nz-1)];
        solver.setBaseState(pressure.data(), t_initial.data(), phi.data(), mass.data());
        // Initialize the same dry auxiliary fields as the WRF zero-copy caller.
        // rk_step=2 performs setup but deliberately skips time integration.
        wrf::sdirk3::ZeroCopyConfig setup{};
        setup.nx=nx; setup.ny=ny; setup.nz=nz;
        setup.nx_u=nu; setup.ny_v=nv; setup.nz_w=nw;
        setup.ids=setup.its=setup.ims=1; setup.ide=setup.ite=setup.ime=nu;
        setup.jds=setup.jts=setup.jms=1; setup.jde=setup.jte=setup.jme=nv;
        setup.kds=setup.kts=setup.kms=1; setup.kde=setup.kme=nw; setup.kte=nz;
        setup.u_ptr=setup.v_ptr=setup.w_ptr=setup.ph_ptr=setup.t_ptr=setup.p_ptr=setup_state.data();
        setup.mu_ptr=setup_state.data(); setup.al_ptr=setup_density.data();
        setup.rdx=setup.rdy=1.0f/spacing; setup.rdnw_ptr=setup.rdn_ptr=metric.data();
        setup.msftx_ptr=setup.msfty_ptr=setup.msfux_ptr=setup.msfuy_ptr=
            setup.msfvx_ptr=setup.msfvy_ptr=setup_map.data();
        setup.c1f_ptr=setup.c1h_ptr=one.data(); setup.c2f_ptr=setup.c2h_ptr=zero.data();
        setup.fnm_ptr=setup.fnp_ptr=half.data();
        std::fill(setup_f.begin(),setup_f.end(),coriolis);
        setup.f_ptr=setup_f.data(); setup.e_ptr=setup.sina_ptr=setup_zero.data();
        setup.cosa_ptr=setup_map.data();
        solver.advanceZeroCopy(setup, 2, 0.1f);
    }

    std::vector<std::vector<float>*> fields() { return {&u,&v,&w,&ph,&theta,&mu}; }
    void set(const torch::Tensor& packed) {
        const auto data = packed.to(torch::kFloat32).contiguous();
        const auto* pointer = data.data_ptr<float>();
        for (auto* field : fields()) {
            std::copy(pointer, pointer+field->size(), field->begin());
            pointer += field->size();
        }
    }
    torch::Tensor state() {
        std::vector<torch::Tensor> blocks;
        for (auto* field : fields())
            blocks.push_back(torch::from_blob(field->data(), {static_cast<int64_t>(field->size())},
                                              torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU)).clone());
        return torch::cat(blocks);
    }
    void step(float dt) {
        solver.unifiedStep(u.data(),v.data(),w.data(),ph.data(),theta.data(),mu.data(),
            ru.data(),rv.data(),rw.data(),rph.data(),rt.data(),rm.data(),
            1.0f/spacing,1.0f/spacing,metric.data(),metric.data(),mass_map.data(),mass_map.data(),
            u_map.data(),u_map.data(),v_map.data(),v_map.data(),
            one.data(),zero.data(),one.data(),zero.data(),half.data(),half.data(),
            1,dt,nx,ny,nz,nu,nv,nw);
        TORCH_CHECK(solver.getLastStepOutcomeCode() == 0, "tile step did not complete");
    }
};

} // namespace wrf::sdirk3::test
