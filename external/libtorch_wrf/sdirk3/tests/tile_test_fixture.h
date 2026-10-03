#pragma once

#include "../wrf_sdirk3_tile_unified.h"
#include "../wrf_sdirk3_config.h"
#include "../wrf_sdirk3_hydrostatic_balance.h"
#include "../wrf_hydrostatic_pressure.h"
#include "../wrf_sdirk3_acoustic_substep.h"
#include "../wrf_sdirk3_boundary_ad.h"
#include "../wrf_sdirk3_autograd_utils.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace wrf::sdirk3::test {
// Only these explicit numerical trial failures may trigger optimizer backtracking.
struct PhysicalTrialRejection : std::runtime_error {
    using std::runtime_error::runtime_error;
};
struct NonConvergedTrialRejection : std::runtime_error {
    using std::runtime_error::runtime_error;
};
inline void requireAcceptedTrialStep(int outcome) {
    if (outcome == static_cast<int>(StepOutcomeCode::HARD_STAGE_ABORT) ||
        outcome == static_cast<int>(StepOutcomeCode::SOFT_NO_PROGRESS))
        throw NonConvergedTrialRejection("forward trial did not advance");
    TORCH_CHECK(outcome == static_cast<int>(StepOutcomeCode::OK_ADVANCED),
                "forward trial returned a fatal/unsupported outcome: ", outcome);
}
constexpr int nx = 8, ny = 6, nz = 4, nu = nx + 1, nv = ny + 1, nw = nz + 1;
constexpr int su = ny*nz*nu, sv = nv*nz*nx, sw = ny*nw*nx, st = ny*nz*nx, sm = ny*nx;
constexpr int total = su + sv + 2*sw + st + sm;
struct PerturbationSize {
    double max_w, mass_fraction, layer_fraction;
};
struct DryBudgetReport {
    double initial_mass=0.0,initial_theta_integral=0.0;
    double max_mass_drift=0.0,mass_allowance=0.0;
    double max_theta_drift=0.0,theta_allowance=0.0;
    bool constant_theta=false;
};

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
    bool packedPeriodicLayout() const { return solver.isPackedPeriodicDomain(); }
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
    torch::Tensor nonzeroHydrostaticBackground(double* native_pressure_floor = nullptr,
                                              double theta_step = 0.0) {
        constexpr double theta_perturbation=10.0;
        constexpr double mass_perturbation=4000.0;
        constexpr double p_top=20000.0;
        TORCH_CHECK(std::isfinite(theta_step) && theta_step>=0.0,
                    "hydrostatic theta-level increment must be finite and nonnegative");
        const auto pbase=solver.p_base_.to(torch::kFloat64);
        const auto thbase=solver.th_base_.to(torch::kFloat64);
        const auto mubase=solver.mu_base_.to(torch::kFloat64);
        TORCH_CHECK(pbase.defined() && thbase.defined() && mubase.defined(),
                    "nonzero hydrostatic background requires fixture base arrays");
        TORCH_CHECK(torch::equal(thbase,torch::full_like(thbase,300.0)) &&
                    std::all_of(metric.begin(),metric.end(),[](float x){return x==-nz;}) &&
                    std::all_of(one.begin(),one.end(),[](float x){return x==1.0f;}) &&
                    std::all_of(zero.begin(),zero.end(),[](float x){return x==0.0f;}),
                    "balanced fixture requires theta_base=300, uniform signed rdn/rdnw and sigma coefficients");
        const auto alpha_base=wrf::sdirk3::compute_inverse_density(
            thbase,pbase,287.0f,717.5f,1004.5f,100000.0f).to(torch::kFloat64);
        std::vector<double> eta_mid(nz);
        for(int k=0;k<nz;++k) eta_mid[k]=1.0-(k+0.5)/nz;
        const auto eta=torch::tensor(eta_mid,torch::kFloat64).view({1,nz,1});
        const auto expected_base_pressure=p_top+eta*mubase.unsqueeze(1);
        const double base_pressure_error=(pbase-expected_base_pressure).abs().max().item<double>();
        const double base_mass_error=(mubase-80000.0).abs().max().item<double>();
        const double base_pressure_check=8.0*std::numeric_limits<float>::epsilon()*
            std::max(1.0,pbase.abs().max().item<double>());
        TORCH_CHECK(base_pressure_error<=base_pressure_check && base_mass_error<=1e-3,
                    "fixture base is not ptop=20kPa/MUB=80kPa sigma column: dpbase=",
                    base_pressure_error," dpbase_floor=",base_pressure_check," dmub=",base_mass_error);
        const auto p_perturbation=eta*mass_perturbation;
        const auto theta_increment=theta_perturbation+
            theta_step*torch::arange(nz,torch::kFloat64).view({1,nz,1});
        const auto theta_full=thbase+theta_increment;
        const auto alpha_target=wrf::sdirk3::compute_inverse_density(
            theta_full,pbase+p_perturbation,287.0f,717.5f,1004.5f,100000.0f).to(torch::kFloat64);
        const auto c1h=torch::tensor(one,torch::kFloat64).slice(0,0,nz).view({1,nz,1});
        const auto c2h=torch::tensor(zero,torch::kFloat64).slice(0,0,nz).view({1,nz,1});
        const auto base_layer_mass=c1h*mubase.unsqueeze(1)+c2h;
        const auto full_layer_mass=c1h*(mubase+mass_perturbation).unsqueeze(1)+c2h;
        const auto rdnw_abs=torch::tensor(metric,torch::kFloat64).slice(0,0,nz).abs().view({nz});
        // Invert the authoritative perturbation alpha relation. The imported
        // PH base is rounded FP32; replacing analytic alpha_base with its
        // geometric difference would define a different native EOS state.
        const auto dphi=(alpha_target*full_layer_mass-alpha_base*base_layer_mass)/
            rdnw_abs.view({1,nz,1});

        auto packed=torch::zeros({total},torch::kFloat64);
        const int64_t ph0=su+sv+sw, theta0=su+sv+2*sw, mu0=total-sm;
        packed.slice(0,theta0,theta0+st).view({ny,nz,nx})
            .copy_(theta_increment.expand({ny,nz,nx}));
        packed.slice(0,mu0,total).fill_(mass_perturbation);
        auto ph=packed.slice(0,ph0,ph0+sw).view({ny,nw,nx});
        for(int k=0;k<nz;++k)
            ph.select(1,k+1).copy_(ph.select(1,k)+dphi.select(1,k));

        const auto native=wrf::sdirk3::calc_p_rho_wrf(
            ph,packed.slice(0,theta0,theta0+st).view({ny,nz,nx}),
            packed.slice(0,mu0,total).view({ny,nx}),mubase,alpha_base,pbase,
            rdnw_abs,c1h.view({nz}),c2h.view({nz}),
            287.0f,717.5f,1004.5f,100000.0f,300.0f);
        const double pressure_error=(native.p_pert-p_perturbation).abs().max().item<double>();
        const double pressure_floor=4096.0*std::numeric_limits<double>::epsilon()*
            std::max(1.0,pbase.abs().max().item<double>());
        const double alpha_error=(native.alt-alpha_target).abs().max().item<double>()/
            alpha_target.abs().max().item<double>();
        const double alpha_floor=512.0*std::numeric_limits<double>::epsilon();
        TORCH_CHECK(pressure_error<=pressure_floor && alpha_error<=alpha_floor,
                    "native calc_p_rho_wrf does not reproduce target p/alpha: dp=",pressure_error,
                    " dp_floor=",pressure_floor," dalpha_rel=",alpha_error,
                    " dalpha_floor=",alpha_floor);
        const auto ph_full=ph+solver.ph_base_.to(torch::kFloat64);
        const auto geometric_alpha=rdnw_abs.view({1,nz,1})*
            (ph_full.slice(1,1,nw)-ph_full.slice(1,0,nz))/full_layer_mass;
        const double geometric_error=((geometric_alpha-alpha_target)/alpha_target)
            .abs().max().item<double>();
        const double geometric_floor=16.0*std::numeric_limits<float>::epsilon();
        TORCH_CHECK(geometric_error<=geometric_floor,
                    "full-PH hydrostatic geometry exceeds imported FP32 base precision: ",geometric_error);
        std::cout<<"BALANCED_NATIVE_EOS ptop="<<p_top
                 <<" theta_full_min=310 theta_step="<<theta_step<<" total_mu=84000"
                 <<" base_pressure_error="<<base_pressure_error
                 <<" expected_ppert_max="<<p_perturbation.abs().max().item<double>()
                 <<" pressure_error="<<pressure_error<<" pressure_floor="<<pressure_floor
                 <<" alpha_relative_error="<<alpha_error<<" alpha_floor="<<alpha_floor
                 <<" geometric_alpha_relative_error="<<geometric_error
                 <<" geometric_alpha_floor="<<geometric_floor<<"\n";
        if(native_pressure_floor) *native_pressure_floor=pressure_floor;
        checkPhysicalState(packed,"nonzero hydrostatic background");
        return packed;
    }
    torch::Tensor dryLayerGeometryFactors() const {
        const auto maps=torch::tensor(mass_map,torch::kFloat64).view({ny,nx});
        const double grid_spacing=spacing;
        const auto area=(grid_spacing*grid_spacing)/maps.square();
        const auto deta=torch::tensor(metric,torch::kFloat64).slice(0,0,nz).abs().reciprocal();
        return area.unsqueeze(1)/solver.grid_info_->g*deta.view({1,nz,1});
    }
    torch::Tensor dryLayerWeights(const torch::Tensor& packed) const {
        TORCH_CHECK(packed.scalar_type()==torch::kFloat64 && packed.numel()==total,
                    "dry-layer budget requires a packed FP64 state");
        const auto mu_full=packed.slice(0,total-sm,total).view({ny,nx})+
            solver.mu_base_.to(torch::kFloat64);
        const auto c1=torch::tensor(one,torch::kFloat64).slice(0,0,nz);
        const auto c2=torch::tensor(zero,torch::kFloat64).slice(0,0,nz);
        return dryLayerGeometryFactors()*
            (c1.view({1,nz,1})*mu_full.unsqueeze(1)+c2.view({1,nz,1}));
    }
    torch::Tensor dryLayerMassSensitivity() const {
        return dryLayerGeometryFactors()*
            torch::tensor(one,torch::kFloat64).slice(0,0,nz).view({1,nz,1});
    }
    torch::Tensor fullMassCenterHeights(const torch::Tensor& packed) const {
        const auto phi=packed.slice(0,su+sv+sw,su+sv+2*sw).view({ny,nw,nx})+
            solver.ph_base_.to(torch::kFloat64);
        return 0.5*(phi.slice(1,1,nw)+phi.slice(1,0,nz))/solver.grid_info_->g;
    }
    DryBudgetReport dryTransportBudget(const torch::Tensor& initial,
                                      const std::vector<torch::Tensor>& checkpoints) const {
        // Observation sites are a subset, not automatically the owned domain.
        const bool packed=solver.isPackedPeriodicDomain();
        const int owned_ny=ny-(packed?1:0),owned_nx=nx-(packed?1:0);
        const auto core=[=](const torch::Tensor& q) {
            return q.slice(0,0,owned_ny).slice(2,0,owned_nx);
        };
        const auto mu=[](const torch::Tensor& q) {
            return q.slice(0,total-sm,total).view({ny,1,nx});
        };
        const auto theta=[](const torch::Tensor& q) {
            return q.slice(0,su+sv+2*sw,total-sm).view({ny,nz,nx});
        };
        const auto sensitivity=core(dryLayerMassSensitivity());
        const auto initial_weights=core(dryLayerWeights(initial));
        const auto initial_theta=core(theta(initial));
        DryBudgetReport report;
        report.initial_mass=initial_weights.sum().item<double>();
        report.initial_theta_integral=(initial_weights*(300.0+initial_theta)).sum().item<double>();
        const double eps=std::numeric_limits<double>::epsilon();
        report.constant_theta=(initial_theta.max()-initial_theta.min()).item<double>()<=
            128.0*eps*std::max(1.0,initial_theta.abs().max().item<double>());
        auto previous=initial;
        double mass_change=0.0,anomaly_change=0.0,anomaly_allowance=0.0;
        for(size_t step=0;step<checkpoints.size();++step) {
            const auto& current=checkpoints[step];
            const auto weights0=core(dryLayerWeights(previous));
            const auto weights1=core(dryLayerWeights(current));
            const auto theta0=core(theta(previous)),theta1=core(theta(current));
            const auto dtheta=theta1-theta0;
            const auto dmass=sensitivity*core(mu(current)-mu(previous));
            // Factored endpoint product; no subtraction of giant base integrals.
            mass_change+=dmass.sum().item<double>();
            anomaly_change+=(weights0*dtheta+dmass*theta0+dmass*dtheta).sum().item<double>();
            report.mass_allowance+=256.0*eps*(sensitivity*
                core(mu(previous).abs()+mu(current).abs())).sum().item<double>();
            anomaly_allowance+=512.0*eps*((weights0*theta0).abs()+
                                          (weights1*theta1).abs()).sum().item<double>();
            report.theta_allowance=anomaly_allowance+300.0*report.mass_allowance;
            const double theta_change=anomaly_change+300.0*mass_change;
            report.max_mass_drift=std::max(report.max_mass_drift,std::abs(mass_change));
            report.max_theta_drift=std::max(report.max_theta_drift,std::abs(theta_change));
            TORCH_CHECK(std::abs(mass_change)<=report.mass_allowance,
                        "dry mass budget failed at step ",step+1," drift=",mass_change,
                        " allowance=",report.mass_allowance);
            // Uniform theta is a linear invariant; nonuniform theta makes Q
            // quadratic and may have an RK defect. The scoped weak-twin gate
            // bounds the measured drift; it does not assert exact conservation
            // or give a rigorous time-error bound for arbitrary profiles.
            TORCH_CHECK(std::abs(theta_change)<=report.theta_allowance,
                        "dry mass-weighted theta budget failed at step ",step+1," drift=",theta_change,
                        " allowance=",report.theta_allowance);
            previous=current;
        }
        return report;
    }
    PerturbationSize perturbationSize(const torch::Tensor& background,
                                      const torch::Tensor& increment) {
        checkPhysicalState(background, "amplitude reference");
        TORCH_CHECK(increment.dim() == 1 && increment.numel() == total &&
                    torch::isfinite(increment).all().item<bool>(),
                    "amplitude increment has an invalid shape/value");
        const auto w = increment.slice(0, su+sv, su+sv+sw);
        const auto mass = background.slice(0, total-sm, total).view({ny,nx}) +
                          solver.mu_base_.to(torch::kFloat64);
        const auto dmass = increment.slice(0, total-sm, total).view({ny,nx});
        const auto phi = background.slice(0, su+sv+sw, su+sv+2*sw).view({ny,nw,nx}) +
                         solver.ph_base_.to(torch::kFloat64);
        const auto dphi = increment.slice(0, su+sv+sw, su+sv+2*sw).view({ny,nw,nx});
        const auto layers = phi.slice(1,1,nw) - phi.slice(1,0,nw-1);
        const auto dlayers = dphi.slice(1,1,nw) - dphi.slice(1,0,nw-1);
        return {w.abs().max().item<double>(), (dmass/mass).abs().max().item<double>(),
                (dlayers/layers).abs().max().item<double>()};
    }
    void checkPhysicalState(const torch::Tensor& packed, const char* where) {
        TORCH_CHECK(packed.defined() && packed.dim() == 1 && packed.numel() == total,
                    where, ": packed state is malformed");
        if (!wrf::sdirk3::guarded_item<bool>(torch::isfinite(packed).all()))
            throw PhysicalTrialRejection(std::string(where)+": non-finite physical state");
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
        if (!(wrf::sdirk3::guarded_item<bool>(torch::isfinite(theta_full).all()) &&
                    wrf::sdirk3::guarded_item<double>(theta_full.min()) > 0.0 &&
                    wrf::sdirk3::guarded_item<bool>(torch::isfinite(mu_full).all()) &&
                    wrf::sdirk3::guarded_item<double>(mu_full.min()) > 0.0 &&
                    wrf::sdirk3::guarded_item<bool>(torch::isfinite(dz_geometric).all()) &&
                    wrf::sdirk3::guarded_item<double>(dz_geometric.min()) > 0.0 &&
                    wrf::sdirk3::guarded_item<bool>(torch::isfinite(eta_layer_thickness).all()) &&
                    wrf::sdirk3::guarded_item<double>(eta_layer_thickness.min()) > 0.0 &&
                    wrf::sdirk3::guarded_item<bool>(torch::isfinite(hybrid_layer_pressure).all()) &&
                    wrf::sdirk3::guarded_item<double>(hybrid_layer_pressure.min()) > 0.0))
            throw PhysicalTrialRejection(std::string(where)+": non-positive theta, mass, or layer geometry");
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
        if (!(wrf::sdirk3::guarded_item<bool>(torch::isfinite(eos).all()) &&
                    wrf::sdirk3::guarded_item<double>(eos.min()) > 1e-20 &&
                    wrf::sdirk3::guarded_item<bool>(torch::isfinite(pressure_full).all()) &&
                    wrf::sdirk3::guarded_item<double>(pressure_full.min()) > 0.0 &&
                    wrf::sdirk3::guarded_item<bool>(torch::isfinite(density).all()) &&
                    wrf::sdirk3::guarded_item<double>(density.min()) > 0.0))
            throw PhysicalTrialRejection(std::string(where)+": diagnosed EOS, pressure, or density is non-positive");
    }
    void step(float dt) {
        solver.unifiedStep(u.data(),v.data(),w.data(),ph.data(),theta.data(),mu.data(),
            ru.data(),rv.data(),rw.data(),rph.data(),rt.data(),rm.data(),
            1.0f/spacing,1.0f/spacing,metric.data(),metric.data(),mass_map.data(),mass_map.data(),
            u_map.data(),u_map.data(),v_map.data(),v_map.data(),
            one.data(),zero.data(),one.data(),zero.data(),half.data(),half.data(),
            1,dt,nx,ny,nz,nu,nv,nw);
        requireAcceptedTrialStep(solver.getLastStepOutcomeCode());
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
