// Compare production FGMRES budgets and preconditioning with fixed A, b, and S=I.
#include "tile_test_fixture.h"

#include "../wrf_sdirk3_unified_preconditioner.h"
#include "../wrf_sdirk3_config.h"
#include "../wrf_sdirk3_newton_solver.h"
#include "../wrf_sdirk3_jvp_fwad_or_fd.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

using wrf::sdirk3::UnifiedPreconditioner;
using namespace wrf::sdirk3::test;

struct RhsTag {
    using type = torch::Tensor (TileSDIRK3UnifiedSolver::*)
        (const torch::Tensor&, wrf::sdirk3::RhsMode);
    friend type access(RhsTag);
};
template<typename Tag, typename Tag::type Member> struct Accessor {
    friend typename Tag::type access(Tag) { return Member; }
};
template struct Accessor<RhsTag, &TileSDIRK3UnifiedSolver::computeUnifiedRHS>;
struct FullHaloTag {
    using type = torch::Tensor (TileSDIRK3UnifiedSolver::*)(const torch::Tensor&, wrf::sdirk3::RhsMode);
    friend type access(FullHaloTag);
};
template struct Accessor<FullHaloTag, &TileSDIRK3UnifiedSolver::computeUnifiedRHSFullHalo>;

struct TransportTag {
    using type = bool (TileSDIRK3UnifiedSolver::*)() const;
    friend type access(TransportTag);
};
template struct Accessor<TransportTag, &TileSDIRK3UnifiedSolver::usesImplicitTransport>;
struct CaptureTag {
    using type = bool TileSDIRK3UnifiedSolver::*;
    friend type access(CaptureTag);
};
template struct Accessor<CaptureTag, &TileSDIRK3UnifiedSolver::capture_theta_faces_now_>;
struct FacesTag {
    using type = TileSDIRK3UnifiedSolver::ArkBudgetTrace::ThetaFaces TileSDIRK3UnifiedSolver::*;
    friend type access(FacesTag);
};
template struct Accessor<FacesTag, &TileSDIRK3UnifiedSolver::rhs_theta_faces_>;

} // namespace

int main(int argc, char** argv) {
    auto& cfg = wrf::sdirk3::g_sdirk3_config;
    cfg.debug_level = 0;
    cfg.precond_acoustic_4x4 = 1;
    const bool face_only = argc > 1 && std::string(argv[1]) == "--implicit-transport-faces";
    const bool implicit_transport = face_only ||
        (argc > 1 && std::string(argv[1]) == "--implicit-transport");
    cfg.imex_split_mode = implicit_transport ? 4 : 3;
    const bool phi_unity = argc > 1 && std::string(argv[1]) == "--phi-unity";
    if (phi_unity) {
        ::setenv("WRF_SDIRK3_PHI_SCHUR_DENOM_UNITY", "1", /*overwrite=*/1);
    } else {
        ::unsetenv("WRF_SDIRK3_PHI_SCHUR_DENOM_UNITY");
    }

    TileCase tile;
    // A completed production step establishes the WRF geometry/omega contract.
    tile.step(1.0e-4f);
    if (implicit_transport) {
    const auto transport_enabled = [&] { return (tile.solver.*access(TransportTag{}))(); };
    TORCH_CHECK(transport_enabled(), "standard fixture must select implicit transport");
    for (const auto mode : {wrf::sdirk3::RhsMode::ImplicitOnly,
                            wrf::sdirk3::RhsMode::ExplicitOnly}) {
        bool rejected=false;
        try { (tile.solver.*access(FullHaloTag{}))(tile.state(),mode); }
        catch (const c10::Error&) { rejected=true; }
        TORCH_CHECK(rejected, "mode 4 split FullHalo must fail closed");
    }
    cfg.hevi_split = true;
    TORCH_CHECK(!transport_enabled(), "HEVI must retain its original transport ownership");
    cfg.hevi_split = false;
    cfg.enable_ad_halo_exchange = true;
    TORCH_CHECK(!transport_enabled(), "AD halo must retain its original transport ownership");
    bool rejected_halo=false;
    try { tile.rhsAt(tile.state(),wrf::sdirk3::RhsMode::ImplicitOnly,0.1f); }
    catch (const c10::Error&) { rejected_halo=true; }
    TORCH_CHECK(rejected_halo, "mode 4 AD halo split RHS must fail closed");
    cfg.enable_ad_halo_exchange = false;
    cfg.imex_split_mode = 2;
    TORCH_CHECK(!transport_enabled(), "other IMEX modes must retain transport ownership");
    cfg.imex_split_mode = 4;
    // Constant transported fields must be stationary in each partition, not
    // merely after cancelling an artificial fast/slow product-rule pair.
    // Exercise two layer masses and the physical periodic/symmetric layout.
    for (const auto coefficients : {std::pair<float, float>{1.0f, 0.0f},
                                    std::pair<float, float>{0.6f, 16000.0f}}) {
        TileCase transport;
        std::fill(transport.one.begin(), transport.one.end(), coefficients.first);
        std::fill(transport.zero.begin(), transport.zero.end(), coefficients.second);
        transport.step(1.0e-4f);
        auto state = torch::zeros({total}, torch::kFloat64);
        state.slice(0, 0, su).fill_(20.0);
        state.slice(0, su + sv + 2*sw, total-sm).fill_(10.0);
        state.slice(0, total-sm, total).fill_(4000.0);
        auto v = state.slice(0, su, su+sv).view({nv,nz,nx});
        for (int j=0; j<nv; ++j)
            v.select(0,j).fill_(2.0*std::sin(M_PI*j/ny));
        transport.checkPhysicalState(state, "transport split state");
        const auto full = transport.rhsAt(state, wrf::sdirk3::RhsMode::Full, 600.0f);
        const auto fast = transport.rhsAt(state, wrf::sdirk3::RhsMode::ImplicitOnly, 600.0f);
        const auto slow = transport.rhsAt(state, wrf::sdirk3::RhsMode::ExplicitOnly, 600.0f);
        for (const auto mode : {wrf::sdirk3::RhsMode::Full,
                                wrf::sdirk3::RhsMode::ImplicitOnly,
                                wrf::sdirk3::RhsMode::ExplicitOnly}) {
            const auto plain = transport.rhsAt(state, mode, 600.0f, state);
            transport.solver.*access(CaptureTag{}) = true;
            const auto captured = transport.rhsAt(state, mode, 600.0f, state);
            if (mode == wrf::sdirk3::RhsMode::ImplicitOnly) {
                const auto faces = transport.solver.*access(FacesTag{});
                TORCH_CHECK(faces.adv_x.defined() && faces.adv_y.defined() && faces.adv_z.defined(),
                            "implicit transport lacks actual producer faces");
                const auto xdiv=-(faces.adv_x.slice(2,1,nu)-faces.adv_x.slice(2,0,nx))/transport.spacing;
                const auto ydiv=-(faces.adv_y.slice(0,1,nv)-faces.adv_y.slice(0,0,ny))/transport.spacing;
                const auto zdiv=4.0*(faces.adv_z.slice(1,1,nw)-faces.adv_z.slice(1,0,nz));
                const auto theta_dot=captured.slice(0,su+sv+2*sw,total-sm).view({ny,nz,nx});
                const auto mu_dot=captured.slice(0,total-sm,total).view({ny,nx}).unsqueeze(1);
                const double layer_mass=coefficients.first*84000.0+coefficients.second;
                const auto error=layer_mass*theta_dot+10.0*coefficients.first*mu_dot-xdiv-ydiv-zdiv;
                TORCH_CHECK(error.slice(0,1,ny-1).slice(1,1,nz-1).slice(2,1,nx-1)
                                .abs().max().item<double>() < 1.0e-8,
                            "implicit theta producer-face conservation failed");
            }
            transport.solver.*access(CaptureTag{}) = false;
            TORCH_CHECK(torch::equal(plain,captured), "theta face capture changed RHS");
        }
        const auto u_core = [](const torch::Tensor& f) {
            return f.slice(0,0,su).view({ny,nz,nu}).slice(0,1,ny-1)
                .slice(1,1,nz-1).slice(2,2,nx-1);
        };
        const auto t_core = [](const torch::Tensor& f) {
            return f.slice(0,su+sv+2*sw,total-sm).view({ny,nz,nx})
                .slice(0,1,ny-1).slice(1,1,nz-1).slice(2,1,nx-1);
        };
        const auto max_abs = [](const torch::Tensor& f) { return f.abs().max().item<double>(); };
        TORCH_CHECK(max_abs(full-fast-slow) < 1.0e-9, "transport split RHS closure");
        const auto mass_part = [](const torch::Tensor& f) {
            return f.slice(0,total-sm,total);
        };
        TORCH_CHECK(max_abs(mass_part(fast)) > 1.0e-3,
                    "transport test needs a nonzero fast mass tendency");
        TORCH_CHECK(max_abs(mass_part(full)-mass_part(fast)) < 1.0e-12 &&
                    max_abs(mass_part(slow)) < 1.0e-12,
                    "continuity must remain entirely in the fast partition");
        for (const auto& f : {fast, slow}) {
            TORCH_CHECK(max_abs(u_core(f)) < 1.0e-9, "constant U split invariant");
            TORCH_CHECK(max_abs(t_core(f)) < 1.0e-9, "constant theta split invariant");
        }
        if (coefficients.first == 1.0f) {
            // Keep the stage reference fixed across AD and central differences.
            // Two FP64 directions exercise mass/velocity dependence and theta.
            auto base = state.clone();
            auto bu = base.slice(0,0,su).view({ny,nz,nu});
            auto bt = base.slice(0,su+sv+2*sw,total-sm).view({ny,nz,nx});
            auto bm = base.slice(0,total-sm,total).view({ny,nx});
            for (int j=0; j<ny; ++j) {
                bu.select(0,j).add_(0.3*std::cos(M_PI*(j+0.5)/ny));
                bt.select(0,j).add_(0.2*std::cos(M_PI*(j+0.5)/ny));
                bm.select(0,j).add_(100.0*std::cos(M_PI*(j+0.5)/ny));
            }
            transport.checkPhysicalState(base,"nonuniform transport split state");
            const auto full4=transport.rhsAt(base,wrf::sdirk3::RhsMode::Full,600.0f,base);
            cfg.imex_split_mode=3;
            const auto full3=transport.rhsAt(base,wrf::sdirk3::RhsMode::Full,600.0f,base);
            cfg.imex_split_mode=4;
            TORCH_CHECK(torch::equal(full3,full4), "mode 3/4 Full RHS must be bit-identical");
            const auto fast4=transport.rhsAt(base,wrf::sdirk3::RhsMode::ImplicitOnly,600.0f,base);
            const auto slow4=transport.rhsAt(base,wrf::sdirk3::RhsMode::ExplicitOnly,600.0f,base);
            TORCH_CHECK(max_abs(full4-fast4-slow4)<1.0e-9 &&
                        max_abs(mass_part(full4)-mass_part(fast4))<1.0e-12 &&
                        max_abs(mass_part(slow4))<1.0e-12 &&
                        max_abs(mass_part(fast4))>1.0e-3,
                        "nonuniform mode-4 partition/continuity ownership failed");
            for (const auto mode : {wrf::sdirk3::RhsMode::Full,
                                    wrf::sdirk3::RhsMode::ImplicitOnly,
                                    wrf::sdirk3::RhsMode::ExplicitOnly}) {
                const auto plain=transport.rhsAt(base,mode,600.0f,base);
                transport.solver.*access(CaptureTag{})=true;
                const auto observed=transport.rhsAt(base,mode,600.0f,base);
                if (mode == wrf::sdirk3::RhsMode::ImplicitOnly) {
                    const auto faces=transport.solver.*access(FacesTag{});
                    TORCH_CHECK(faces.adv_x.defined() && faces.adv_y.defined() && faces.adv_z.defined(),
                                "nonuniform Fast producer faces unavailable");
                    const auto xd=-(faces.adv_x.slice(2,1,nu)-faces.adv_x.slice(2,0,nx))/transport.spacing;
                    const auto yd=-(faces.adv_y.slice(0,1,nv)-faces.adv_y.slice(0,0,ny))/transport.spacing;
                    const auto zd=4.0*(faces.adv_z.slice(1,1,nw)-faces.adv_z.slice(1,0,nz));
                    const auto td=observed.slice(0,su+sv+2*sw,total-sm).view({ny,nz,nx});
                    const auto md=mass_part(observed).view({ny,1,nx});
                    const auto error=(80000.0+bm.unsqueeze(1))*td+bt*md-xd-yd-zd;
                    TORCH_CHECK(max_abs(error.slice(0,1,ny-1).slice(1,1,nz-1).slice(2,1,nx-1))<1.0e-8 &&
                                max_abs(faces.adv_x.slice(2,0,1)-faces.adv_x.slice(2,nx,nu))<1.0e-12 &&
                                max_abs(faces.adv_y.slice(0,0,1))<1.0e-12 &&
                                max_abs(faces.adv_y.slice(0,ny,nv))<1.0e-12 &&
                                max_abs(faces.adv_z.slice(1,0,1))<1.0e-12 &&
                                max_abs(faces.adv_z.slice(1,nz,nw))<1.0e-12,
                                "nonuniform Fast producer faces or boundaries failed");
                }
                transport.solver.*access(CaptureTag{})=false;
                TORCH_CHECK(torch::equal(plain,observed), "nonuniform face capture changed RHS");
            }
            std::cout << "IMPLICIT_TRANSPORT_FULL_BIT_PARITY mode3_mode4=1\n";
            // The same approximate raw principal model must be selected for
            // both ARK partitions; it does not include transport couplings.
            const auto grid=transport.solver.getGridInfo();
            const auto physics=std::make_shared<wrf::sdirk3::PhysicsConfig>();
            cfg.imex_split_mode=3;
            UnifiedPreconditioner p3(grid,physics,0.1f,0.4358665215f);
            TORCH_CHECK(p3.raw_principal_enabled(), "mode-3 raw principal selector changed");
            cfg.imex_split_mode=4;
            UnifiedPreconditioner p4(grid,physics,0.1f,0.4358665215f);
            TORCH_CHECK(p4.raw_principal_enabled(), "mode-4 raw principal selector inactive");
            cfg.hevi_split=true;
            UnifiedPreconditioner unsupported(grid,physics,0.1f,0.4358665215f);
            TORCH_CHECK(!unsupported.raw_principal_enabled(), "raw principal ignored HEVI guard");
            cfg.hevi_split=false;
            p4.bind_raw_principal_state_or_throw(base,2,"nonuniform mode-4 stage");
            p4.update(base,0.1f,0.4358665215f);
            const auto b4=1.0e-3*torch::sin(torch::arange(total,base.options())*0.031);
            std::function<torch::Tensor(const torch::Tensor&)> m4 =
                [&](const torch::Tensor& x) { return p4.apply(x); };
            TORCH_CHECK(torch::isfinite(m4(b4)).all().item<bool>(),
                        "mode-4 raw principal application is nonfinite");
            const auto rhs4=[&](const torch::Tensor& x) {
                return (transport.solver.*access(RhsTag{}))(x,wrf::sdirk3::RhsMode::ImplicitOnly);
            };
            transport.rhsAt(base,wrf::sdirk3::RhsMode::ImplicitOnly,0.1f,base);
            const float stage_h=0.1f*0.4358665215f;
            const auto a4=[&](const torch::Tensor& x) {
                bool used_fd=false;
                std::string reason;
                auto j=wrf::sdirk3::compute_jvp_fwad_or_fd(rhs4,base,x,0,0.0f,&used_fd,&reason);
                TORCH_CHECK(!used_fd,"mode-4 JVP fell back to finite differences: ",reason);
                return x-stage_h*j;
            };
            for (int budget : {8,16}) {
                const auto result=wrf::sdirk3::krylov_methods::solve_fgmres(
                    a4,b4,torch::zeros_like(b4),0,0.0f,8,1.0e-5f,budget/8,
                    m4,nullptr,nullptr,false,false);
                const double relative=(b4-a4(result.x)).norm().item<double>()/b4.norm().item<double>();
                TORCH_CHECK(std::isfinite(relative),"mode-4 FGMRES true residual is nonfinite");
                if (result.success)
                    TORCH_CHECK(relative<=1.0e-5, "mode-4 FGMRES claimed success above true tolerance");
                if (budget == 16)
                    TORCH_CHECK(result.success && relative<=1.0e-5,
                                "mode-4 raw-principal FGMRES failed its true-residual tolerance");
                std::cout << "MODE4_RAW_PRINCIPAL budget=" << budget
                          << " success=" << result.success << " true_relative=" << relative << '\n';
            }
            auto vm = torch::zeros_like(base);
            auto dv = vm.slice(0,su,su+sv).view({nv,nz,nx});
            auto dm = vm.slice(0,total-sm,total).view({ny,nx});
            for (int j=0; j<nv; ++j)
                dv.select(0,j).fill_(0.1*std::sin(2.0*M_PI*j/ny));
            for (int j=0; j<ny; ++j)
                dm.select(0,j).fill_(100.0*std::cos(M_PI*(j+0.5)/ny));
            auto td = torch::zeros_like(base);
            auto dt = td.slice(0,su+sv+2*sw,total-sm).view({ny,nz,nx});
            for (int j=0; j<ny; ++j)
                dt.select(0,j).fill_(0.5*std::cos(M_PI*(j+0.5)/ny));
            transport.rhsAt(base, wrf::sdirk3::RhsMode::Full, 600.0f, base);
            for (int direction_index=0; direction_index<2; ++direction_index) {
                const auto& direction = direction_index == 0 ? vm : td;
                std::vector<torch::Tensor> tangents;
                for (const auto mode : {wrf::sdirk3::RhsMode::Full,
                                        wrf::sdirk3::RhsMode::ImplicitOnly,
                                        wrf::sdirk3::RhsMode::ExplicitOnly}) {
                    const auto F = [&](const torch::Tensor& x) {
                        return (transport.solver.*access(RhsTag{}))(x, mode);
                    };
                    bool used_fd = false;
                    std::string reason;
                    auto tangent = wrf::sdirk3::compute_jvp_fwad_or_fd(
                        F, base, direction, 0, 0.0f, &used_fd, &reason);
                    TORCH_CHECK(!used_fd, "transport FWAD fallback: ", reason);
                    constexpr double step = 1.0e-3;
                    auto fd = (F(base + step*direction) - F(base - step*direction))/(2*step);
                    const double scale = std::max(max_abs(tangent), max_abs(fd));
                    TORCH_CHECK(max_abs(tangent-fd) <= 1.0e-8 + 3.0e-4*scale,
                                "transport FWAD/central FD mismatch");
                    tangents.push_back(tangent);
                }
                const double scale = std::max({max_abs(tangents[0]),
                    max_abs(tangents[1]), max_abs(tangents[2])});
                TORCH_CHECK(max_abs(tangents[0]-tangents[1]-tangents[2]) <=
                            1.0e-10 + 1.0e-8*scale, "transport tangent split closure");
                const double mass_scale = max_abs(mass_part(tangents[0]));
                TORCH_CHECK(max_abs(mass_part(tangents[0])-mass_part(tangents[1])) <=
                            1.0e-12 + 1.0e-8*mass_scale &&
                            max_abs(mass_part(tangents[2])) < 1.0e-12,
                            "continuity tangent must remain in the fast partition");
                if (direction_index == 0) {
                    TORCH_CHECK(mass_scale > 1.0e-5,
                                "V/mass direction must exercise continuity tangent");
                }
            }
        }
    }
    }
    if (face_only) {
        for (int j=0; j<nv; ++j)
            for (int k=0; k<nz; ++k)
                for (int i=0; i<nx; ++i)
                    tile.v[(j*nz+k)*nx+i]=0.1f*std::sin(M_PI*j/ny);
        tile.step(0.001f);
        TORCH_CHECK(torch::isfinite(tile.state()).all().item<bool>(),
                    "mode-4 nonuniform forward step is nonfinite");
        return 0;
    }
    const auto U = tile.state();
    const auto rhs = [&](const torch::Tensor& x) {
        return (tile.solver.*access(RhsTag{}))(x, wrf::sdirk3::RhsMode::ImplicitOnly);
    };
    const auto F0 = rhs(U);
    const float h = 0.1f * 0.4358665215f;
    const auto jvp = [&](const torch::Tensor& v) {
        bool used_fd = false;
        std::string reason;
        const auto result = wrf::sdirk3::compute_jvp_fwad_or_fd(
            rhs, U, v, 0, 0.0f, &used_fd, &reason);
        TORCH_CHECK(!used_fd, "production RHS fwAD fell back to FD: ", reason);
        return result;
    };
    auto A = [&](const torch::Tensor& v) {
        const auto Jv = jvp(v);
        return v - h*Jv;
    };

    // Isolate the raw Phi -> U/V units from the production RHS.  The direction is
    // horizontally varying but constant in k, so it exercises only the horizontal
    // geopotential gradient and does not introduce an EOS/thermodynamic perturbation.
    // TileCase has a scalar 80,000 Pa base column, so the weighted PGF mass cancels
    // the coupled-momentum -> velocity conversion exactly on this fixture:
    //   A_u,phi = h * dPhi/dx,   A_v,phi = h * dPhi/dy.
    constexpr float phi_step_x = 1.0e4f;  // m^2/s^2 per x cell
    constexpr float phi_step_y = 7.0e3f;  // m^2/s^2 per y cell
    constexpr float fixture_mu0 = 8.0e4f; // Pa, TileCase::mass
    std::vector<float> phi_gradient(sw, 0.0f);
    for (int j = 0; j < ny; ++j) {
        for (int k = 0; k < nw; ++k) {
            for (int i = 0; i < nx; ++i) {
                phi_gradient[(j*nw + k)*nx + i] =
                    phi_step_x * static_cast<float>(i) +
                    phi_step_y * static_cast<float>(j);
            }
        }
    }
    auto v_phi_pgf = torch::zeros_like(U);
    v_phi_pgf.slice(0, su + sv + sw, su + sv + 2*sw).copy_(
        torch::from_blob(phi_gradient.data(), {sw}, U.options()).clone());
    const auto A_phi_pgf = A(v_phi_pgf);
    const auto A_u_phi_pgf = A_phi_pgf.slice(0, 0, su).reshape({ny, nz, nu});
    const auto A_v_phi_pgf = A_phi_pgf.slice(0, su, su + sv).reshape({nv, nz, nx});
    // Exclude the symmetric/wrap boundaries.  Check two mass levels explicitly;
    // vertical constancy means both levels must carry the same scalar response.
    const auto A_u_phi_interior = A_u_phi_pgf.slice(1, 1, 3).slice(2, 1, nx - 1);
    const auto A_v_phi_interior = A_v_phi_pgf.slice(0, 1, ny).slice(1, 1, 3);
    const float expected_A_u_phi = h * phi_step_x / tile.spacing;
    const float expected_A_v_phi = h * phi_step_y / tile.spacing;
    const double u_phi_err =
        (A_u_phi_interior - expected_A_u_phi).abs().max().item<double>() /
        std::max(std::abs(static_cast<double>(expected_A_u_phi)), 1.0e-30);
    const double v_phi_err =
        (A_v_phi_interior - expected_A_v_phi).abs().max().item<double>() /
        std::max(std::abs(static_cast<double>(expected_A_v_phi)), 1.0e-30);
    std::cout << "RHS_PRECOND_PHI_PGF fixed_mu0=" << fixture_mu0
              << " expected_A_u_phi=" << expected_A_u_phi
              << " expected_A_v_phi=" << expected_A_v_phi
              << " A_u_phi_rel_err=" << u_phi_err
              << " A_v_phi_rel_err=" << v_phi_err
              << " levels_checked=2 interior=1\n";
    TORCH_CHECK(torch::isfinite(A_u_phi_interior).all().item<bool>() &&
                torch::isfinite(A_v_phi_interior).all().item<bool>(),
                "Phi-only PGF probe produced a non-finite U/V response");
    TORCH_CHECK(u_phi_err < 5.0e-3 && v_phi_err < 5.0e-3,
                "Phi-only PGF probe disagrees with the raw weighted-PGF contract: U rel=",
                u_phi_err, " V rel=", v_phi_err);

    torch::manual_seed(20260905);
    auto probe_v = torch::randn_like(U);
    const auto j1 = jvp(probe_v);
    TORCH_CHECK(torch::isfinite(j1).all().item<bool>(), "production fwAD JVP is non-finite");

    // Directional block readout from the actual ImplicitOnly JVP.  These are
    // measurements of A=I-hJ in raw packed coordinates, not assertions that
    // the approximate preconditioner must equal A.
    auto v_phi = torch::zeros_like(U);
    v_phi.slice(0, su + sv + sw, su + sv + 2*sw).copy_(
        torch::randn({sw}, U.options()));
    auto v_mu = torch::zeros_like(U);
    v_mu.slice(0, total-sm, total).copy_(torch::randn({sm}, U.options()));
    const auto A_phi = v_phi - h*jvp(v_phi);
    const auto A_mu = v_mu - h*jvp(v_mu);
    const double a_mu_phi = A_phi.slice(0, total-sm, total).norm().item<double>() /
                            std::max(v_phi.slice(0, su + sv + sw, su + sv + 2*sw).norm().item<double>(), 1e-30);
    const double a_phi_mu = A_mu.slice(0, su + sv + sw, su + sv + 2*sw).norm().item<double>() /
                            std::max(v_mu.slice(0, total-sm, total).norm().item<double>(), 1e-30);
    std::cout << "RHS_PRECOND_BLOCKS mode=" << (phi_unity ? "phi_unity" : "shipped")
              << " rel_A_mu_phi=" << a_mu_phi
              << " rel_A_phi_mu=" << a_phi_mu << '\n';

    auto grid = tile.solver.getGridInfo();
    auto physics = std::make_shared<wrf::sdirk3::PhysicsConfig>();
    UnifiedPreconditioner P(grid, physics, 0.1f, 0.4358665215f);
    const auto mu_pert = U.slice(0, total-sm, total).reshape({ny,nx});
    if (P.raw_principal_enabled())
        P.bind_raw_principal_state_or_throw(U, 1, "fixed RHS contract state");
    else
        P.bind_stage_state_or_throw(mu_pert, 1);
    P.update(U, 0.1f, 0.4358665215f);
    std::function<torch::Tensor(const torch::Tensor&)> M =
        [&](const torch::Tensor& v) { return P.apply(v); };

    // Observe one independent M column.  This is deliberately a unit residual in a
    // single interior Phi mass cell, rather than the PGF gradient direction above:
    // it reports the current inverse-preconditioner U/V response without folding a
    // spatial stencil into the input.  The controlled comparison uses the source's
    // existing MU-PHI Schur switch; no production coefficient is changed here.
    constexpr int probe_j = 2;
    constexpr int probe_k = 1;
    constexpr int probe_i = 3;
    std::vector<float> phi_column_data(sw, 0.0f);
    phi_column_data[(probe_j*nw + probe_k)*nx + probe_i] = 1.0f;
    auto v_phi_column = torch::zeros_like(U);
    v_phi_column.slice(0, su + sv + sw, su + sv + 2*sw).copy_(
        torch::from_blob(phi_column_data.data(), {sw}, U.options()).clone());
    const char* prior_mu_phi_zero = std::getenv("WRF_SDIRK3_MU_PHI_SCHUR_ZERO");
    const bool had_prior_mu_phi_zero = prior_mu_phi_zero != nullptr;
    const std::string prior_mu_phi_zero_value = had_prior_mu_phi_zero ? prior_mu_phi_zero : "";
    ::unsetenv("WRF_SDIRK3_MU_PHI_SCHUR_ZERO");
    const auto M_phi_column = M(v_phi_column);
    ::setenv("WRF_SDIRK3_MU_PHI_SCHUR_ZERO", "1", /*overwrite=*/1);
    const auto M_phi_column_mu_schur_zero = M(v_phi_column);
    if (had_prior_mu_phi_zero)
        ::setenv("WRF_SDIRK3_MU_PHI_SCHUR_ZERO", prior_mu_phi_zero_value.c_str(), /*overwrite=*/1);
    else
        ::unsetenv("WRF_SDIRK3_MU_PHI_SCHUR_ZERO");
    const auto M_u_phi_column = M_phi_column.slice(0, 0, su).reshape({ny, nz, nu});
    const auto M_v_phi_column = M_phi_column.slice(0, su, su + sv).reshape({nv, nz, nx});
    const auto M_u_phi_column_zero =
        M_phi_column_mu_schur_zero.slice(0, 0, su).reshape({ny, nz, nu});
    const auto M_v_phi_column_zero =
        M_phi_column_mu_schur_zero.slice(0, su, su + sv).reshape({nv, nz, nx});
    const float m_u_phi = M_u_phi_column[probe_j][probe_k][probe_i].item<float>();
    const float m_v_phi = M_v_phi_column[probe_j][probe_k][probe_i].item<float>();
    const float m_u_phi_zero = M_u_phi_column_zero[probe_j][probe_k][probe_i].item<float>();
    const float m_v_phi_zero = M_v_phi_column_zero[probe_j][probe_k][probe_i].item<float>();
    const float current_M_u_phi_model = -h / (fixture_mu0 * tile.spacing);
    const float current_M_v_phi_model = -h / (fixture_mu0 * tile.spacing);
    std::cout << "RHS_PRECOND_PHI_COLUMN model="
              << (P.raw_principal_enabled() ? "raw_principal" : "legacy")
              << " legacy_mu_phi_switch_active=" << !P.raw_principal_enabled()
              << " input_j=" << probe_j
              << " input_k=" << probe_k << " input_i=" << probe_i
              << " M_u_phi=" << m_u_phi << " M_v_phi=" << m_v_phi
              << " M_u_phi_mu_schur_zero=" << m_u_phi_zero
              << " M_v_phi_mu_schur_zero=" << m_v_phi_zero
              << " legacy_local_model_u=" << current_M_u_phi_model
              << " legacy_local_model_v=" << current_M_v_phi_model << '\n';
    TORCH_CHECK(std::isfinite(m_u_phi) && std::isfinite(m_v_phi) &&
                std::isfinite(m_u_phi_zero) && std::isfinite(m_v_phi_zero),
                "Phi-column preconditioner response is non-finite");

    const auto b = torch::randn_like(U) * 1.0e-3f;
    const auto x0 = torch::zeros_like(U);
    using wrf::sdirk3::krylov_methods::solve_fgmres;
    const float tol = 1.0e-5f;
    for (int budget : {8, 16}) {
        const int restarts = budget / 8;
        std::function<torch::Tensor(const torch::Tensor&)> no_pc;
        const auto none = solve_fgmres(A, b, x0, 0, 0.0f, 8, tol, restarts,
                                       no_pc, nullptr, nullptr, false, false);
        const auto actual = solve_fgmres(A, b, x0, 0, 0.0f, 8, tol, restarts,
                                         M, nullptr, nullptr, false, false);
        const auto true_rel = [&](const torch::Tensor& x) {
            return (b-A(x)).norm().item<double>() / b.norm().item<double>();
        };
        const double none_rel = true_rel(none.x);
        const double actual_rel = true_rel(actual.x);
        std::cout << "RHS_PRECOND_CONTRACT mode=" << (phi_unity ? "phi_unity" : "shipped")
                  << " fixed_state=1 fixed_b=1 fixed_metric=1"
                  << " budget=" << budget
                  << " no_pc_iterations=" << none.iterations
                  << " no_pc_true_rel=" << none_rel
                  << " actual_pc_iterations=" << actual.iterations
                  << " actual_pc_true_rel=" << actual_rel
                  << " actual_pc_success=" << actual.success << '\n';
        TORCH_CHECK(std::isfinite(none_rel) && std::isfinite(actual_rel),
                    "production FGMRES returned a non-finite true residual");
        TORCH_CHECK(none.success && none_rel <= tol,
                    "unpreconditioned control failed its true-residual tolerance");
        if (actual.success)
            TORCH_CHECK(actual_rel <= tol, "preconditioned solver claimed success above true-residual tolerance");
    }
    TORCH_CHECK(torch::isfinite(F0).all().item<bool>(), "actual RHS produced non-finite values");
    std::cout << "RHS_PRECOND_CONTRACT NOTE: M is an approximate inverse; this fixture does not assert M=A or M=A^-1.\n";
    return 0;
}
