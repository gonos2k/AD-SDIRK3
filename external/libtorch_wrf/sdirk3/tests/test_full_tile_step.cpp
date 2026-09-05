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

namespace {
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

void check_horizontal_pgf() {
    // Fortran horizontal_pressure_gradient, with p'=al'=0:
    // dV/dt = -dPhi/dy. Constant vertical offsets preserve the base pressure.
    bool valid = true;
    constexpr float slope = 2e-5f, dt = 0.01f;
    for (float spacing : {100000.0f, 200000.0f}) {
        // Independent scalar anchors from the three Fortran terms, isolated so
        // compensating errors cannot hide an extra averaging factor.
        const double expected[] = {1.6, 288.0, 16.0};
        for (int term=0; term<3; ++term) {
            auto amplitude = torch::tensor(1.0, torch::kFloat64).requires_grad_(true);
            const auto scalar = [&](double value) { return torch::full_like(amplitude, value); };
            const auto force = wrf::sdirk3::acoustic::horizontal_pgf_primary(
                1.0f/spacing, scalar(80000),
                (term == 0 ? 4e-5*spacing : 0)*amplitude, scalar(2.4),
                (term == 1 ? 3e-3*spacing : 0)*amplitude, scalar(0.2),
                (term == 2 ? 2e-3*spacing : 0)*amplitude);
            const auto derivative = torch::autograd::grad({force}, {amplitude})[0];
            const double value_error = std::abs(force.item<double>()/expected[term]-1);
            const double gradient_error = std::abs(derivative.item<double>()/expected[term]-1);
            std::cout << "PGF_PRIMARY term=" << term << " spacing=" << spacing
                      << " force=" << force.item<double>() << " expected=" << expected[term]
                      << " gradient_error=" << gradient_error << '\n';
            valid = valid && value_error < 2e-7 && gradient_error < 2e-7;
        }
        for (bool y_direction : {false, true}) {
            TileCase tile(spacing);
            for (int j=0; j<ny; ++j)
                for (int k=0; k<nw; ++k)
                    for (int i=0; i<nx; ++i)
                        tile.ph[(j*nw+k)*nx+i] = slope*spacing*(y_direction ? j : i);
            tile.step(dt);
            double sum = 0;
            int count = 0;
            for (int j=2; j<ny-1; ++j)
                for (int k=1; k<nz-1; ++k)
                    for (int i=2; i<nx-2; ++i) {
                        sum += y_direction ? tile.v[(j*nz+k)*nx+i] : tile.u[(j*nz+k)*nu+i];
                        ++count;
                    }
            const double measured = sum/count/dt;
            const double relative_error = std::abs(measured+slope)/slope;
            std::cout << "PGF_ANCHOR direction=" << (y_direction ? 'y' : 'x')
                      << " spacing=" << spacing << " acceleration=" << measured
                      << " expected=" << -slope << " relative_error=" << relative_error << '\n';
            valid = valid && std::isfinite(relative_error) && relative_error < 1e-3;

            // At phi'=mu'=0, alpha=alpha_base and the dry equation of state
            // gives p=pb*(theta/300)^(cp/cv). This tests the pressure term through
            // the full tile, independently of the shared PGF helper.
            TileCase thermal(spacing);
            for (int j=0; j<ny; ++j)
                for (int k=0; k<nz; ++k)
                    for (int i=0; i<nx; ++i)
                        thermal.theta[(j*nz+k)*nx+i] = y_direction ? j : i;
            constexpr float thermal_dt = 0.001f;
            thermal.step(thermal_dt);
            double measured_pressure = 0, expected_pressure = 0;
            for (int j=2; j<ny-1; ++j)
                for (int k=1; k<nz-1; ++k)
                    for (int i=2; i<nx-2; ++i) {
                        const int coordinate = y_direction ? j : i;
                        const double pb = 100000.0-(k+0.5)*80000.0/nz;
                        const double alpha = 287.0*300.0/pb*std::pow(pb/100000.0,287.0/1004.5);
                        const double dp = pb*(std::pow(1+coordinate/300.0,1004.5/717.5)
                            - std::pow(1+(coordinate-1)/300.0,1004.5/717.5));
                        expected_pressure -= alpha*dp/spacing;
                        measured_pressure += (y_direction ? thermal.v[(j*nz+k)*nx+i]
                            : thermal.u[(j*nz+k)*nu+i])/thermal_dt;
                    }
            const double pressure_error = std::abs(measured_pressure/expected_pressure-1);
            std::cout << "PGF_PRESSURE direction=" << (y_direction ? 'y' : 'x')
                      << " spacing=" << spacing << " acceleration=" << measured_pressure/count
                      << " expected=" << expected_pressure/count << " relative_error=" << pressure_error << '\n';
            valid = valid && std::isfinite(pressure_error) && pressure_error < 1e-3;
        }
    }
    TORCH_CHECK(valid, "production horizontal PGF violates its metric/averaging contract");
}

void check_symmetric_walls() {
    // An internal tile edge is not a physical wall; P is self-adjoint and
    // idempotent on fixed boundaries, and removes only the constrained entries.
    const auto a = torch::arange(24,torch::kFloat64).view({2,3,4});
    const auto b = torch::sin(a);
    const auto project = [](const torch::Tensor& x) {
        return wrf::sdirk3::project_symmetric_normal_velocity(x,2,4,4,7,true,true);
    };
    TORCH_CHECK(torch::equal(project(project(a)),project(a)), "boundary projection is not idempotent");
    TORCH_CHECK(std::abs(((project(a)*b)-(a*project(b))).sum().item<double>()) < 1e-14,
                "boundary projection is not self-adjoint");
    TORCH_CHECK(torch::equal(a,wrf::sdirk3::project_symmetric_normal_velocity(a,2,4,1,9,true,true)),
                "an internal tile edge was mistaken for a physical wall");
    const auto halo = wrf::sdirk3::project_symmetric_normal_velocity(a,2,0,1,9,true,true);
    TORCH_CHECK(torch::equal(halo.select(2,1),torch::zeros_like(a.select(2,1))) &&
                torch::equal(halo.select(2,0),a.select(2,0)) &&
                torch::equal(halo.slice(2,2,4),a.slice(2,2,4)),
                "full-halo origin placed the physical wall on the wrong local index");
    auto masked_nan = a.clone();
    masked_nan.select(2,0).fill_(std::numeric_limits<double>::quiet_NaN());
    TORCH_CHECK(torch::isfinite(project(masked_nan)).all().item<bool>(), "inactive boundary NaN leaked");
    masked_nan.select(2,1).fill_(std::numeric_limits<double>::quiet_NaN());
    TORCH_CHECK(!torch::isfinite(project(masked_nan)).all().item<bool>(), "active NaN was hidden");
    constexpr float f = 1e-4f, speed = 10.0f, dt = 0.01f;
    bool valid = true;
    for (bool y_wall : {false, true}) {
        TileCase tile(100000.0f, f);
        if (y_wall) {
            std::fill(tile.u.begin(), tile.u.end(), speed);
        } else {
            tile.solver.setBoundaryConditions(false, true, true, true, false, false,
                                               false, false, false, false);
            std::fill(tile.v.begin(), tile.v.end(), speed);
        }
        tile.step(dt);
        double wall_max = 0, interior_sum = 0;
        int count = 0;
        auto terminal = torch::zeros(total, torch::kFloat32);
        for (int j=0; j<(y_wall ? nv : ny); ++j)
            for (int k=0; k<nz; ++k)
                for (int i=0; i<(y_wall ? nx : nu); ++i) {
                    const int local = y_wall ? (j*nz+k)*nx+i : (j*nz+k)*nu+i;
                    const double value = y_wall ? tile.v[local] : tile.u[local];
                    const bool wall = y_wall ? (j==0 || j==nv-1) : (i==0 || i==nu-1);
                    if (wall) {
                        wall_max = std::max(wall_max,std::abs(value));
                        terminal.index_put_({(y_wall ? su : 0)+local},1.0f);
                    } else if (j>1 && j<ny-1 && i>1 && i<nx-1) {
                        interior_sum += value; ++count;
                    }
                }
        const double expected = (y_wall ? -1 : 1)*f*speed*dt;
        const double error = std::abs(interior_sum/count/expected-1);
        const double wall_pullback = tile.solver.pullbackLastStep(terminal).abs().max().item<double>();
        std::cout << "SYMMETRIC_WALL axis=" << (y_wall ? 'y' : 'x')
                  << " wall_max=" << wall_max << " interior_error=" << error
                  << " wall_pullback=" << wall_pullback << '\n';
        TileCase contaminated(100000.0f,f);
        if (y_wall) {
            std::fill(contaminated.u.begin(),contaminated.u.end(),speed);
            for (int j : {0,nv-1})
                for (int k=0; k<nz; ++k)
                    for (int i=0; i<nx; ++i) contaminated.v[(j*nz+k)*nx+i] = 3.0f;
        } else {
            contaminated.solver.setBoundaryConditions(false,true,true,true,false,false,
                                                      false,false,false,false);
            std::fill(contaminated.v.begin(),contaminated.v.end(),speed);
            for (int j=0; j<ny; ++j)
                for (int k=0; k<nz; ++k)
                    for (int i : {0,nu-1}) contaminated.u[(j*nz+k)*nu+i] = 3.0f;
        }
        contaminated.step(dt);
        const bool input_projected = torch::equal(tile.state(),contaminated.state());
        std::cout << "SYMMETRIC_WALL input_projection_equal=" << input_projected << '\n';
        valid = valid && wall_max == 0 && wall_pullback == 0 && input_projected
                      && std::isfinite(error) && error < 1e-3;
    }
    TORCH_CHECK(valid, "fixed symmetric boundary or its full-step derivative is inconsistent");
}

void check_rhs_debug_invariance() {
    auto& cfg = wrf::sdirk3::g_sdirk3_config;
    cfg.retain_graph_for_adjoint = false;
    cfg.nan_sanitize_mode = 0;
    std::vector<torch::Tensor> valid_states;
    bool all_rejected = true;
    for (int level : {0,2}) {
        cfg.debug_level = level;
        TileCase valid;
        valid.step(0.01f);
        valid_states.push_back(valid.state());
        TileCase invalid;
        // Finite input, but negative absolute theta makes the fractional EOS
        // power undefined. Logging must not turn that invalid RHS into zero.
        std::fill(invalid.theta.begin(),invalid.theta.end(),-400.0f);
        bool rejected = false;
        try { invalid.step(0.01f); }
        catch (const std::exception&) { rejected = true; }
        std::cout << "RHS_DEBUG level=" << level << " invalid_rejected=" << rejected << '\n';
        all_rejected = all_rejected && rejected;
    }
    cfg.debug_level = 0;
    TORCH_CHECK(torch::equal(valid_states[0],valid_states[1]), "debug level changed the finite forward result");
    TORCH_CHECK(all_rejected, "debug level converted an invalid RHS into successful integration");
}

void check_temporal_order() {
    const auto integrate = [](int steps, float tolerance) {
        wrf::sdirk3::g_sdirk3_config.newton_tol = tolerance;
        TileCase tile;
        for (int j=0; j<ny; ++j)
            for (int k=0; k<nw; ++k)
                for (int i=0; i<nx; ++i)
                    tile.w[(j*nw+k)*nx+i] = 0.1f*std::sin(std::acos(-1.0)*k/nz);
        for (int step=0; step<steps; ++step) tile.step(64.0f/steps);
        return tile.state().to(torch::kFloat64);
    };
    const auto coarse = integrate(32, 1e-7f);
    const auto fine = integrate(64, 1e-7f);
    const auto reference = integrate(256, 1e-7f);
    const auto tighter = integrate(64, 1e-8f);
    const int starts[] = {su+sv, su+sv+sw, su+sv+2*sw};
    const int sizes[] = {sw, sw, st};
    const char* names[] = {"w", "ph", "t"};
    for (int block=0; block<3; ++block) {
        const auto norm = [&](const torch::Tensor& difference) {
            return difference.slice(0,starts[block],starts[block]+sizes[block]).norm().item<double>();
        };
        const double coarse_error = norm(coarse-reference);
        const double fine_error = norm(fine-reference);
        const double solve_change = norm(tighter-fine);
        const double order = std::log2(coarse_error/fine_error);
        std::cout << "TILE_ORDER field=" << names[block] << " coarse_error=" << coarse_error
                  << " fine_error=" << fine_error << " order=" << order
                  << " tighter_solve_change=" << solve_change << '\n';
        TORCH_CHECK(std::isfinite(order) && fine_error > 0 && order > 2.7 && order < 3.4,
                    "full tile temporal convergence is not third order in ", names[block]);
        TORCH_CHECK(std::isfinite(solve_change) && solve_change < 0.01*fine_error,
                    "Newton error obscures temporal convergence in ", names[block]);
    }
}
}

int main(int argc, char** argv) {
    auto& cfg = wrf::sdirk3::g_sdirk3_config;
    cfg.debug_level = 0;
    cfg.imex_split_mode = 3;
    cfg.use_autograd = true;
    cfg.imex_slow_in_tangent = true;
    cfg.retain_graph_for_adjoint = true;
    cfg.precond_type = 0;
    cfg.max_newton_iter = 40;
    cfg.newton_tol = 1e-7f;
    cfg.krylov_tol = 1e-6f;
    cfg.gmres_restart = 30;
    cfg.max_krylov_iter = 20;
    cfg.stage_fail_action = 1;
    cfg.gmres_warmstart = false;
    cfg.inn_warmstart_enable = false;
    std::ostringstream log;
    auto* previous = std::cerr.rdbuf(log.rdbuf());
    try {
        if (argc == 2 && std::string(argv[1]) == "--symmetric-walls") {
            check_symmetric_walls();
            std::cerr.rdbuf(previous);
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--rhs-debug-invariance") {
            check_rhs_debug_invariance();
            std::cerr.rdbuf(previous);
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--horizontal-pgf") {
            cfg.retain_graph_for_adjoint = false;
            check_horizontal_pgf();
            std::cerr.rdbuf(previous);
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--temporal-order") {
            cfg.retain_graph_for_adjoint = false;
            check_temporal_order();
            std::cerr.rdbuf(previous);
            return 0;
        }
        TORCH_CHECK(argc == 1, "unsupported tile test mode");
        TileCase base;
        const auto initial = base.state();
        base.step(0.1f);
        const auto final = base.state();
        std::cout << "REST change=" << (final-initial).norm().item<double>() << '\n';
        TORCH_CHECK(torch::isfinite(final).all().item<bool>(), "non-finite tile state");
        auto terminal = torch::cos(torch::arange(total, torch::kFloat32) * 0.031f);
        terminal = terminal / terminal.norm();
        const auto pulled = base.solver.pullbackLastStep(terminal);
        std::cout << "PULLBACK norm=" << pulled.norm().item<double>() << '\n';
        TORCH_CHECK(torch::isfinite(pulled).all().item<bool>(), "non-finite pullback");
        TORCH_CHECK((pulled-terminal).norm().item<double>() > 0.05,
                    "the tested step derivative must not reduce to the identity");
        TORCH_CHECK(base.solver.pullbackLastStep(torch::zeros_like(terminal)).norm().item<double>() == 0,
                    "zero objective must give zero gradient");
        bool invalid_rejected = false;
        try { base.solver.pullbackLastStep(terminal * std::numeric_limits<float>::quiet_NaN()); }
        catch (const c10::Error&) { invalid_rejected = true; }
        TORCH_CHECK(invalid_rejected, "non-finite cotangent was accepted");
        auto direction = torch::sin(torch::arange(total, torch::kFloat32) * 0.017f);
        direction = direction / direction.norm();
        cfg.retain_graph_for_adjoint = false;
        double previous_remainder = 0;
        for (float epsilon : {0.04f, 0.02f, 0.01f}) {
            TileCase plus, minus;
            plus.set(initial + epsilon*direction);
            minus.set(initial - epsilon*direction);
            plus.step(0.1f);
            minus.step(0.1f);
            const auto tangent = (plus.state().to(torch::kFloat64)-minus.state().to(torch::kFloat64))
                                 /(2.0*epsilon);
            const double lhs = tangent.dot(terminal.to(torch::kFloat64)).item<double>();
            const double rhs = direction.to(torch::kFloat64).dot(pulled.to(torch::kFloat64)).item<double>();
            std::cout << "DOT eps=" << epsilon << " lhs=" << lhs << " rhs=" << rhs
                      << " relative_error=" << std::abs(lhs-rhs)/std::max(std::abs(lhs),std::abs(rhs)) << '\n';
            TORCH_CHECK(std::isfinite(lhs) && std::abs(lhs-rhs)/std::max(std::abs(lhs),std::abs(rhs)) < 5e-4,
                        "full tile tangent / adjoint dot test failed");
            const auto delta = plus.state().to(torch::kFloat64) - final.to(torch::kFloat64);
            const double objective = delta.dot(terminal.to(torch::kFloat64)).item<double>()
                                     + 0.5*delta.square().sum().item<double>();
            const double remainder = std::abs(objective - epsilon*rhs);
            std::cout << "TAYLOR eps=" << epsilon << " remainder=" << remainder
                      << " ratio=" << previous_remainder/remainder << '\n';
            TORCH_CHECK(std::isfinite(remainder) && remainder > 0, "uninformative Taylor test");
            if (previous_remainder > 0)
                TORCH_CHECK(previous_remainder/remainder > 3.5 && previous_remainder/remainder < 4.5,
                            "objective Taylor remainder is not second order");
            previous_remainder = remainder;
        }
        TileCase replay;
        replay.step(0.1f);
        TORCH_CHECK(torch::equal(replay.state(), final), "graph retention changed the forward result");
        for (int step = 2; step <= 100; ++step) {
            replay.step(0.1f);
            const auto state = replay.state();
            TORCH_CHECK(torch::isfinite(state).all().item<bool>(), "non-finite multistep rest state");
            if (step == 10 || step == 100) {
                const double drift = (state-initial).norm().item<double>();
                const double mass_drift = state.slice(0,total-sm,total).abs().max().item<double>();
                std::cout << "REST steps=" << step << " drift=" << drift << " mass_max=" << mass_drift << '\n';
                const int sizes[] = {su,sv,sw,sw,st,sm};
                const char* names[] = {"u","v","w","ph","t","mu"};
                int offset = 0;
                for (int block=0; block<6; ++block) {
                    std::cout << "REST_BLOCK steps=" << step << " field=" << names[block]
                              << " max_abs=" << state.slice(0,offset,offset+sizes[block]).abs().max().item<double>() << '\n';
                    offset += sizes[block];
                }
                // Uniform horizontal rest has no dry mass-flux divergence.
                // Report the other blocks; finiteness is not exact equilibrium.
                TORCH_CHECK(mass_drift == 0, "uniform dry mass was not preserved");
            }
        }
        base.step(0.1f); // Retention is now off: the previous graph must be invalidated.
        bool stale_rejected = false;
        try { base.solver.pullbackLastStep(terminal); }
        catch (const c10::Error&) { stale_rejected = true; }
        TORCH_CHECK(stale_rejected, "previous-step graph remained available after another step");
        std::cout << "Full tile step contracts passed\n";
        std::cerr.rdbuf(previous);
        return 0;
    } catch (const std::exception& e) {
        std::cerr.rdbuf(previous);
        std::cerr << e.what() << '\n';
        const auto captured = log.str();
        std::cerr << captured;
        return 1;
    }
}
