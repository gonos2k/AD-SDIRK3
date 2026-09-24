#include "tile_test_fixture.h"
#include <array>

namespace {
using namespace wrf::sdirk3::test;

struct GoverningRhsTag {
    using type = torch::Tensor (TileSDIRK3UnifiedSolver::*)(
        const torch::Tensor&, wrf::sdirk3::RhsMode);
    friend type access(GoverningRhsTag);
};
template<typename Tag, typename Tag::type Member> struct GoverningAccessor {
    friend typename Tag::type access(Tag) { return Member; }
};
template struct GoverningAccessor<GoverningRhsTag,
    &TileSDIRK3UnifiedSolver::computeUnifiedRHS>;

void check_governing_step_budget() {
    using wrf::sdirk3::RhsMode;
    auto& cfg = wrf::sdirk3::g_sdirk3_config;
    cfg.debug_level = 0;
    cfg.imex_split_mode = 3;
    cfg.hevi_split = false;
    cfg.split_explicit = false;
    cfg.mass_coordinate_mode = 1;
    cfg.diffusion_option = 2;
    cfg.khdif = 1000.0f;
    cfg.kvdif = 0.0f;
    cfg.precond_type = 0;
    cfg.use_autograd = true;
    cfg.retain_graph_for_adjoint = true;
    cfg.imex_slow_in_tangent = true;
    constexpr float spacing = 1000.0f, dt = 0.01f;
    const auto setup = [](TileCase& tile) {
        tile.solver.setTerrainSlopes(torch::zeros({ny,nw,nx}),
                                     torch::zeros({ny,nw,nx}));
        for (int j=0; j<ny; ++j) {
            for (int k=0; k<nz; ++k) {
                for (int i=0; i<nu; ++i) {
                    tile.u[(j*nz+k)*nu+i] = i == nx ? 0.0f :
                        std::sin(2.0*std::acos(-1.0)*i/nx);
                }
                for (int i=0; i<nx; ++i)
                    tile.theta[(j*nz+k)*nx+i] = 0.5f*
                        std::cos(2.0*std::acos(-1.0)*i/nx)*
                        std::cos(2.0*std::acos(-1.0)*j/(ny-1));
            }
        }
    };
    TileCase tile(spacing);
    setup(tile);
    const auto before = tile.state();
    tile.step(dt);
    const auto after = tile.state();
    TORCH_CHECK(torch::isfinite(after).all().item<bool>() &&
                after.slice(0,total-sm,total).min().item<double>() > -80000.0 &&
                after.slice(0,su+sv+2*sw,total-sm).min().item<double>() > -300.0,
                "non-finite or nonphysical dry mass/theta after the tile step");
    const auto mass_change = (after-before).slice(0,total-sm,total).to(torch::kFloat64);
    const double mass_signal = mass_change.abs().sum().item<double>();
    const double mass_drift = std::abs(mass_change.sum().item<double>());
    const double mass_budget = 64.0*std::numeric_limits<float>::epsilon()*mass_signal;
    TORCH_CHECK(mass_signal > 1.0 && mass_drift <= mass_budget,
                "nonzero closed-domain dry mass budget failed: drift=", mass_drift,
                " budget=", mass_budget, " signal=", mass_signal);
    const auto heat_content = [](const torch::Tensor& state) {
        const auto mass=state.slice(0,total-sm,total).to(torch::kFloat64).view({ny,nx})+80000.0;
        const auto theta=state.slice(0,su+sv+2*sw,total-sm)
            .to(torch::kFloat64).view({ny,nz,nx})+300.0;
        return (mass.unsqueeze(1)*theta).sum().item<double>()/nz;
    };
    const auto initial_mass=before.slice(0,total-sm,total).to(torch::kFloat64)
        .view({ny,nx})+80000.0;
    const auto initial_theta=before.slice(0,su+sv+2*sw,total-sm)
        .to(torch::kFloat64).view({ny,nz,nx});
    const double theta_anomaly_l1=(initial_mass.unsqueeze(1)*initial_theta)
        .abs().sum().item<double>()/nz;
    const double heat_drift=std::abs(heat_content(after)-heat_content(before));
    const double heat_budget=4.0*std::numeric_limits<float>::epsilon()*theta_anomaly_l1;
    TORCH_CHECK(theta_anomaly_l1>1.0 && std::isfinite(heat_drift) &&
                heat_drift<=heat_budget,
                "closed-domain mass-weighted theta budget failed: drift=",heat_drift,
                " budget=",heat_budget," anomaly_l1=",theta_anomaly_l1);
    std::cout << "GOV_STEP mass_l1=" << mass_signal << " mass_drift=" << mass_drift
              << " budget=" << mass_budget << " theta_anomaly_l1=" << theta_anomaly_l1
              << " heat_drift=" << heat_drift << " heat_budget=" << heat_budget << '\n';

    const auto terminal = torch::cos(torch::arange(total,torch::kFloat32)*0.031f)
        / std::sqrt(static_cast<float>(total));
    const auto direction = torch::sin(torch::arange(total,torch::kFloat32)*0.017f)
        / std::sqrt(static_cast<float>(total));
    const auto pulled = tile.solver.pullbackLastStep(terminal);
    for (float h : {0.04f,0.02f}) {
        TileCase plus(spacing), minus(spacing);
        setup(plus); setup(minus);
        plus.set(before+h*direction);
        minus.set(before-h*direction);
        plus.step(dt); minus.step(dt);
        const auto tangent=(plus.state().to(torch::kFloat64)-minus.state().to(torch::kFloat64))
            /(2.0*h);
        const double lhs=tangent.dot(terminal.to(torch::kFloat64)).item<double>();
        const double rhs=direction.to(torch::kFloat64).dot(pulled.to(torch::kFloat64)).item<double>();
        const double relative_error=std::abs(lhs-rhs)/std::max(std::abs(lhs),std::abs(rhs));
        TORCH_CHECK(std::max(std::abs(lhs),std::abs(rhs))>1e-3 &&
                    std::isfinite(relative_error) && relative_error<5e-4,
                    "active-diffusion full-step adjoint mismatch: ",relative_error);
        std::cout << "GOV_STEP adjoint_h=" << h << " relative_error=" << relative_error << '\n';
    }
    cfg.khdif=0.0f;
    TileCase off(spacing);
    setup(off); off.set(before); off.step(dt);
    const auto off_pulled=off.solver.pullbackLastStep(terminal);
    cfg.khdif=1000.0f;
    const double state_change=(after-off.state()).abs().max().item<double>();
    const double gradient_change=(pulled-off_pulled).abs().max().item<double>();
    // Flat normal-stress Fourier scale is a positive-control estimate here.
    const double flat_mode_scale=2.0*cfg.khdif*dt*
        (4.0*std::pow(std::sin(std::acos(-1.0)/nx),2))/(spacing*spacing);
    TORCH_CHECK(state_change>0.01*flat_mode_scale &&
                gradient_change>0.01*flat_mode_scale,
                "active diffusion did not measurably affect state and pullback");
    std::cout << "GOV_STEP diffusion_state=" << state_change
              << " diffusion_pullback=" << gradient_change << '\n';

    const auto evaluate_order = [&](const std::array<RhsMode,3>& order) {
        TileCase probe(spacing);
        setup(probe); probe.set(after); probe.step(1e-4f); probe.set(after);
        std::array<torch::Tensor,3> parts;
        for (const auto mode : order)
            parts[static_cast<int>(mode)] =
                (probe.solver.*access(GoverningRhsTag{}))(probe.state(),mode).detach().clone();
        return parts;
    };
    const auto forward=evaluate_order({RhsMode::Full,RhsMode::ExplicitOnly,RhsMode::ImplicitOnly});
    const auto reverse=evaluate_order({RhsMode::ImplicitOnly,RhsMode::ExplicitOnly,RhsMode::Full});
    const int ends[]={0,su,su+sv,su+sv+sw,su+sv+2*sw,su+sv+2*sw+st,total};
    const char* names[]={"U","V","W","PH","T","MU"};
    for (int mode=0; mode<3; ++mode)
        TORCH_CHECK(torch::equal(forward[mode],reverse[mode]),
                    "RHS mode changed with evaluation order: ",mode);
    for (int block=0; block<6; ++block) {
        const auto f=forward[0].slice(0,ends[block],ends[block+1]);
        const auto i=forward[1].slice(0,ends[block],ends[block+1]);
        const auto e=forward[2].slice(0,ends[block],ends[block+1]);
        const double scale=std::max({1.0,f.abs().max().item<double>(),
            i.abs().max().item<double>(),e.abs().max().item<double>()});
        const double error=(f-i-e).abs().max().item<double>();
        const double budget=32.0*std::numeric_limits<float>::epsilon()*scale;
        TORCH_CHECK(std::isfinite(error) && error<=budget,
                    "Full/Explicit/Implicit RHS split changed ",names[block],
                    ": error=",error," budget=",budget);
        std::cout << "GOV_RHS field=" << names[block] << " closure=" << error
                  << " budget=" << budget << '\n';
    }
    TORCH_CHECK(forward[2].slice(0,0,su).abs().max().item<double>()>1e-4 &&
                forward[1].slice(0,0,su).abs().max().item<double>()>1e-4,
                "uninformative zero explicit or implicit U signal");
    TORCH_CHECK(forward[2].slice(0,su+sv+2*sw,total-sm).abs().max().item<double>()>1e-5 &&
                forward[1].slice(0,su+sv+2*sw,total-sm).abs().max().item<double>()>1e-5,
                "uninformative zero explicit or implicit theta signal");
}

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
    constexpr float temporal_tolerance = 1e-7f;
    const int coarse_steps = 64;
    const int fine_steps = 128;
    const int reference_steps = 512;
    const auto coarse = integrate(coarse_steps, temporal_tolerance);
    const auto fine = integrate(fine_steps, temporal_tolerance);
    const auto reference = integrate(reference_steps, temporal_tolerance);
    const auto tighter = integrate(fine_steps, 1e-9f);
    const int starts[] = {su+sv, su+sv+sw};
    const int sizes[] = {sw, sw};
    const char* names[] = {"w", "ph"};
    for (int block=0; block<2; ++block) {
        const auto norm = [&](const torch::Tensor& difference) {
            return difference.slice(0,starts[block],starts[block]+sizes[block]).norm().item<double>();
        };
        const double coarse_error = norm(coarse-reference);
        const double fine_error = norm(fine-reference);
        const double solve_change = norm(tighter-fine);
        const double order = std::log2(coarse_error/fine_error);
        std::cout << "TILE_ORDER field=" << names[block]
                  << " steps=" << coarse_steps << "/" << fine_steps
                  << "/" << reference_steps << " tolerance=" << temporal_tolerance
                  << " coarse_error=" << coarse_error
                  << " fine_error=" << fine_error << " order=" << order
                  << " tighter_solve_change=" << solve_change << '\n';
        TORCH_CHECK(std::isfinite(order) && fine_error > 0 && order > 2.7 && order < 3.4,
                    "full tile temporal convergence is not third order in ", names[block]);
        TORCH_CHECK(std::isfinite(solve_change) && solve_change < 0.01*fine_error,
                    "Newton error obscures temporal convergence in ", names[block]);
    }
    // This horizontally uniform dry column has theta'=0 and diagnosed Omega=0.
    // Potential temperature stays constant during vertical acoustic motion; a
    // nonzero theta signal here would measure a spurious source, not time order.
    for (const auto& state : {coarse, fine, reference, tighter}) {
        TORCH_CHECK(state.slice(0, su+sv+2*sw, total-sm).abs().max().item<double>() == 0.0,
                    "vertical acoustic motion changed constant potential temperature");
    }
}
}

struct DiagnosticPullback {
    torch::Tensor output;
    torch::Tensor pullback;
};

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

        auto run_diagnostic_pullback = [&](int debug_level, std::string& trace) {
            std::ostringstream captured;
            auto* captured_previous = std::cerr.rdbuf(captured.rdbuf());
            try {
                cfg.debug_level = debug_level;
                cfg.retain_graph_for_adjoint = true;
                TileCase tile;
                tile.step(0.1f);
                DiagnosticPullback result{tile.state(), tile.solver.pullbackLastStep(terminal)};
                std::cerr.rdbuf(captured_previous);
                trace = captured.str();
                return result;
            } catch (...) {
                std::cerr.rdbuf(captured_previous);
                throw;
            }
        };
        std::string quiet_trace, verbose_trace;
        const auto quiet = run_diagnostic_pullback(0, quiet_trace);
        const auto verbose = run_diagnostic_pullback(2, verbose_trace);
        TORCH_CHECK(torch::equal(quiet.output, verbose.output) &&
                    torch::equal(quiet.pullback, verbose.pullback),
                    "verbose transpose residual diagnostics changed the pullback");
        TORCH_CHECK(quiet_trace.find("SDIRK3_CONVERGED_STAGE_BLOCK_RESIDUAL") == std::string::npos,
                    "quiet pullback emitted native block residual diagnostics");
        TORCH_CHECK(verbose_trace.find("SDIRK3_CONVERGED_STAGE_BLOCK_RESIDUAL") != std::string::npos,
                    "verbose pullback omitted native block residual diagnostics");
        for (const char* block : {"ru", "rv", "rw", "ph", "t", "mu"}) {
            TORCH_CHECK(verbose_trace.find(std::string(" ") + block + "_r=") != std::string::npos &&
                        verbose_trace.find(std::string(" ") + block + "_b=") != std::string::npos &&
                        verbose_trace.find(std::string(" ") + block + "_rel=") != std::string::npos,
                        "verbose native block residual diagnostics omitted block ", block);
        }
        std::istringstream diagnostic_lines(verbose_trace);
        for (std::string line; std::getline(diagnostic_lines, line); ) {
            if (line.find("SDIRK3_CONVERGED_STAGE_BLOCK_RESIDUAL") == 0)
                std::cout << line << '\n';
        }
        check_governing_step_budget();
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
