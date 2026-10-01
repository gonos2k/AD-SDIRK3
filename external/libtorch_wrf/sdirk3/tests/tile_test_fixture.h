#pragma once

#include "../wrf_sdirk3_tile_unified.h"
#include "../wrf_sdirk3_config.h"
#include "../wrf_sdirk3_hydrostatic_balance.h"
#include "../wrf_hydrostatic_pressure.h"
#include "../wrf_sdirk3_acoustic_substep.h"
#include "../wrf_sdirk3_boundary_ad.h"
#include "../wrf_sdirk3_autograd_utils.h"
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

    explicit TileCase(float grid_spacing = 100000.0f, float coriolis = 0.0f,
                      bool native_trajectory = false)
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
        solver.advanceZeroCopy(setup, native_trajectory ? 1 : 2, 0.1f);
        if (native_trajectory) {
            // The native curvature RHS consumes these W-level profiles.  Setup-only
            // rk_step=2 deliberately leaves them unpublished in legacy fixtures.
            auto grid = solver.getGridInfo();
            TORCH_CHECK(grid, "trajectory fixture has no GridInfo");
            grid->fzm = torch::from_blob(half.data(), {nw}, torch::kFloat32).clone();
            grid->fzp = torch::from_blob(half.data(), {nw}, torch::kFloat32).clone();
            solver.setTerrainSlopes(torch::full({ny, nw, nx}, 1.0e-4f, torch::kFloat32),
                                    torch::full({ny, nw, nx}, -2.0e-4f, torch::kFloat32));
        }
    }

    std::vector<std::vector<float>*> fields() { return {&u,&v,&w,&ph,&theta,&mu}; }
    std::vector<std::vector<float>*> tendencies() { return {&ru,&rv,&rw,&rph,&rt,&rm}; }
    void set(const torch::Tensor& packed) {
        const auto data = packed.to(torch::kFloat32).contiguous();
        const auto* pointer = data.data_ptr<float>();
        for (auto* field : fields()) {
            std::copy(pointer, pointer+field->size(), field->begin());
            pointer += field->size();
        }
    }
    torch::Tensor terrainSlopeX() const { return solver.zx_; }
    torch::Tensor terrainSlopeY() const { return solver.zy_; }
    torch::Tensor moistureCorrectionU() const { return solver.cqu_; }
    torch::Tensor moistureCorrectionV() const { return solver.cqv_; }
    torch::Tensor moistureCorrectionW() const { return solver.cqw_; }
    torch::Tensor coriolisF() const { return solver.f_; }
    torch::Tensor coriolisE() const { return solver.e_; }
    void checkFixedInputs() const { solver.checkFixedTrajectoryFingerprint(); }
    void useDoubleGridMetrics() {
        solver.rdnw_.clear();
        solver.rdn_.clear();
        auto grid = solver.getGridInfo();
        TORCH_CHECK(grid, "trajectory fixture has no GridInfo");
        grid->rdnw = torch::full({nw}, 1.0, torch::TensorOptions().dtype(torch::kFloat64).device(torch::kCPU));
        grid->rdn = torch::full({nw}, 1.0, torch::TensorOptions().dtype(torch::kFloat64).device(torch::kCPU));
    }

    torch::Tensor state() {
        std::vector<torch::Tensor> blocks;
        for (auto* field : fields())
            blocks.push_back(torch::from_blob(field->data(), {static_cast<int64_t>(field->size())},
                                              torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU)).clone());
        return torch::cat(blocks);
    }
    torch::Tensor projectControlState(const torch::Tensor& state) {
        return solver.projectStateBoundaries(state);
    }
    void checkPhysicalState(const torch::Tensor& packed, const char* where) {
        TORCH_CHECK(packed.defined() && packed.numel() == total &&
                    wrf::sdirk3::guarded_item<bool>(torch::isfinite(packed).all()),
                    where, ": packed state is malformed or non-finite");
        TORCH_CHECK(solver.p_base_.defined() && solver.ph_base_.defined() &&
                    solver.mu_base_.defined(), where, ": fixture base state is not initialized");
        const auto state = packed.to(torch::kFloat64);
        const auto theta = state.slice(0, su + sv + 2 * sw, su + sv + 2 * sw + st)
            .view({ny, nz, nx});
        const auto mu = state.slice(0, total - sm, total).view({ny, nx});
        const auto ph = state.slice(0, su + sv + sw, su + sv + 2 * sw)
            .view({ny, nw, nx});
        const auto theta_full = 300.0 + theta;
        const auto mu_full = mu + solver.mu_base_.to(torch::kFloat64);
        const auto ph_full = ph + solver.ph_base_.to(torch::kFloat64);
        const auto dphi = ph_full.slice(1, 1, nz + 1) - ph_full.slice(1, 0, nz);
        const auto dz_geometric = dphi / solver.grid_info_->g;
        // These are the exact fixture buffers supplied on each unifiedStep.
        // The setup-only rk_step=2 call need not populate the solver's cached
        // c1h/c2h or vertical-metric tensors yet.
        const auto rdnw = torch::tensor(metric, torch::kFloat64).abs().slice(0, 0, nz);
        const auto c1h = torch::tensor(one, torch::kFloat64).slice(0, 0, nz);
        const auto c2h = torch::tensor(zero, torch::kFloat64).slice(0, 0, nz);
        const auto eta_layer_thickness = rdnw.abs().reciprocal();
        const auto hybrid_layer_pressure =
            c1h.view({1, nz, 1}) * mu_full.unsqueeze(1) + c2h.view({1, nz, 1});
        TORCH_CHECK(wrf::sdirk3::guarded_item<bool>(torch::isfinite(theta_full).all()) &&
                    wrf::sdirk3::guarded_item<double>(theta_full.min()) > 0.0 &&
                    wrf::sdirk3::guarded_item<bool>(torch::isfinite(mu_full).all()) &&
                    wrf::sdirk3::guarded_item<double>(mu_full.min()) > 0.0 &&
                    wrf::sdirk3::guarded_item<bool>(torch::isfinite(dz_geometric).all()) &&
                    wrf::sdirk3::guarded_item<double>(dz_geometric.min()) > 0.0 &&
                    wrf::sdirk3::guarded_item<bool>(torch::isfinite(eta_layer_thickness).all()) &&
                    wrf::sdirk3::guarded_item<double>(eta_layer_thickness.min()) > 0.0 &&
                    wrf::sdirk3::guarded_item<bool>(torch::isfinite(hybrid_layer_pressure).all()) &&
                    wrf::sdirk3::guarded_item<double>(hybrid_layer_pressure.min()) > 0.0,
                    where, ": non-positive theta, mass, or layer geometry");
        const auto diagnostics = wrf::sdirk3::acoustic::diag_p_al(
            ph, solver.ph_base_.to(torch::kFloat64), theta,
            solver.p_base_.to(torch::kFloat64), mu_full,
            solver.mu_base_.to(torch::kFloat64), c1h, c2h, rdnw,
            100000.0f, 287.0f, 1004.5f / 717.5f, 300.0f);
        const auto p_pert = std::get<0>(diagnostics);
        const auto alpha_full = std::get<2>(diagnostics);
        const auto eos = 287.0 * theta_full / (100000.0 * alpha_full);
        const auto pressure_full = solver.p_base_.to(torch::kFloat64) + p_pert;
        const auto density = alpha_full.reciprocal();
        TORCH_CHECK(wrf::sdirk3::guarded_item<bool>(torch::isfinite(eos).all()) &&
                    wrf::sdirk3::guarded_item<double>(eos.min()) > 1e-20 &&
                    wrf::sdirk3::guarded_item<bool>(torch::isfinite(pressure_full).all()) &&
                    wrf::sdirk3::guarded_item<double>(pressure_full.min()) > 0.0 &&
                    wrf::sdirk3::guarded_item<bool>(torch::isfinite(density).all()) &&
                    wrf::sdirk3::guarded_item<double>(density.min()) > 0.0,
                    where, ": diagnosed EOS, pressure, or density is non-positive");
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
    void stepWithInternalState(float dt, const torch::Tensor& state) {
        solver.next_fp64_state_for_test_ = state.detach();
        step(dt);
    }
    torch::Tensor rhsAt(const torch::Tensor& state, wrf::sdirk3::RhsMode mode,
                        float dt, const torch::Tensor& reference = {}) {
        solver.dt_stage_ = dt;
        solver.U_ref_stage_ = (reference.defined() ? reference : state).detach().clone();
        return solver.computeUnifiedRHS(state, mode).detach();
    }
    void replaceImportedDiagnosticsForTest(float pressure, float alpha) {
        TORCH_CHECK(solver.p_pert_.defined() && solver.al_.defined(),
                    "imported diagnostics must be initialized before this probe");
        solver.p_pert_ = torch::full_like(solver.p_pert_, pressure);
        solver.al_ = torch::full_like(solver.al_, alpha);
    }
};

} // namespace wrf::sdirk3::test
