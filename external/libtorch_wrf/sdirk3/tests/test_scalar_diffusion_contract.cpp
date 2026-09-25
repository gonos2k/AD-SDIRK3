// Direct production-library scalar diffusion contract.
#include "../wrf_sdirk3_tile_unified.h"
#include "tile_test_fixture.h"
#include <torch/torch.h>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <limits>
#include <string>
#include <utility>
#include <vector>

struct ScalarDiffusionTag {
    using type = torch::Tensor (TileSDIRK3UnifiedSolver::*)
        (const torch::Tensor&, const torch::Tensor&, float, float,
         const torch::Tensor&, const torch::Tensor&, const torch::Tensor&,
         const torch::Tensor&, torch::Tensor*, torch::Tensor*,
         const torch::Tensor&, const torch::Tensor&, const torch::Tensor&);
    friend type access(ScalarDiffusionTag);
};
template<typename Tag, typename Tag::type Member> struct MemberAccessor {
    friend typename Tag::type access(Tag) { return Member; }
};
template struct MemberAccessor<ScalarDiffusionTag,
                               &TileSDIRK3UnifiedSolver::compute_horizontal_diffusion_scalar_wrf>;

struct MapUxTag {
    using type = torch::Tensor TileSDIRK3UnifiedSolver::*;
    friend type access(MapUxTag);
};
struct MapUyTag {
    using type = torch::Tensor TileSDIRK3UnifiedSolver::*;
    friend type access(MapUyTag);
};
struct MapVyTag {
    using type = torch::Tensor TileSDIRK3UnifiedSolver::*;
    friend type access(MapVyTag);
};
template struct MemberAccessor<MapUxTag, &TileSDIRK3UnifiedSolver::msfux_>;
template struct MemberAccessor<MapUyTag, &TileSDIRK3UnifiedSolver::msfuy_>;
template struct MemberAccessor<MapVyTag, &TileSDIRK3UnifiedSolver::msfvy_>;

struct ActualRhsTag {
    using type = torch::Tensor (TileSDIRK3UnifiedSolver::*) (
        const torch::Tensor&, wrf::sdirk3::RhsMode);
    friend type access(ActualRhsTag);
};
struct CoordinateTag {
    using type = void (TileSDIRK3UnifiedSolver::*)(const float*, const float*,
                                                   const float*, const float*);
    friend type access(CoordinateTag);
};
struct DiffusionTag {
    using type = void (TileSDIRK3UnifiedSolver::*)(const float*, const float*,
                                                   const float*, const float*);
    friend type access(DiffusionTag);
};
template struct MemberAccessor<ActualRhsTag, &TileSDIRK3UnifiedSolver::computeUnifiedRHS>;
template struct MemberAccessor<CoordinateTag,
                               &TileSDIRK3UnifiedSolver::setVerticalCoordinateCoefficients>;
template struct MemberAccessor<DiffusionTag,
                               &TileSDIRK3UnifiedSolver::setDiffusionCoefficients>;

struct MomentumDiffusionTag {
    using type = torch::Tensor (TileSDIRK3UnifiedSolver::*)(
        const torch::Tensor&, const torch::Tensor&, const torch::Tensor&, const torch::Tensor&,
        float, float, const torch::Tensor&, const torch::Tensor&, const torch::Tensor&,
        const torch::Tensor&, const torch::Tensor&, const torch::Tensor&, const torch::Tensor&);
    friend type access(MomentumDiffusionTag);
};
struct VMomentumDiffusionTag {
    using type = MomentumDiffusionTag::type;
    friend type access(VMomentumDiffusionTag);
};
template struct MemberAccessor<MomentumDiffusionTag,
    &TileSDIRK3UnifiedSolver::compute_horizontal_diffusion_u_wrf>;
template struct MemberAccessor<VMomentumDiffusionTag,
    &TileSDIRK3UnifiedSolver::compute_horizontal_diffusion_v_wrf>;

namespace {
constexpr int nx = 8, ny = 6, nz = 4;
constexpr double pi = 3.14159265358979323846;
constexpr double Kh = 2.0, MUT = 784.8;
using torch::indexing::Slice;

bool run(torch::Dtype dtype) {
    const double eps = dtype == torch::kFloat32
        ? std::numeric_limits<float>::epsilon()
        : std::numeric_limits<double>::epsilon();
    TileSDIRK3UnifiedSolver tile(nx, ny, nz, 1.0f, 1.0f,
                                 {1.0f}, {1.0f}, std::vector<float>(nz, 1.0f), 0);
    auto opt = torch::TensorOptions().dtype(dtype).device(torch::kCPU);
    auto ii = torch::arange(nx, opt).view({1, 1, nx});
    auto jj = torch::arange(ny, opt).view({ny, 1, 1});
    auto qx = torch::sin(ii * (2.0 * pi / nx)).expand({ny, nz, nx});
    auto qy = torch::sin(jj * (2.0 * pi / ny)).expand({ny, nz, nx});
    auto constant = torch::ones({ny, nz, nx}, opt);
    auto Kh_field = torch::full({ny, nz, nx}, Kh, opt);
    auto msftx = torch::ones({ny, nx}, opt);
    auto msfty = torch::ones({ny, nx}, opt);
    auto msfvx = torch::ones({ny + 1, nx}, opt);
    auto mut = torch::full({ny, nx}, MUT, opt);
    auto call = [&](const torch::Tensor& q, bool supply_msfvx) {
        return (tile.*access(ScalarDiffusionTag{}))(
            q, Kh_field, 1.0f, 1.0f, msftx, msfty,
            supply_msfvx ? msfvx : torch::Tensor(), mut, nullptr, nullptr,
            torch::Tensor(),torch::Tensor(),torch::Tensor());
    };
    const double zero_tol = 512.0 * eps * (1.0 + std::abs(Kh * MUT));
    const double mode_tol = 512.0 * eps * (1.0 + std::abs(Kh * MUT));
    const double lambda_x = -4.0 * std::sin(pi / nx) * std::sin(pi / nx);
    const double lambda_y = -4.0 * std::sin(pi / ny) * std::sin(pi / ny);
    bool ok = true;
    auto check = [&](const char* label, double value, double tol) {
        const bool pass = std::isfinite(value) && value <= tol;
        std::cout << (pass ? "PASS " : "FAIL ") << label << " value="
                  << value << " tol=" << tol << '\n';
        ok = ok && pass;
    };

    for (bool supply_msfvx : {false, true}) {
        auto zero = call(constant, supply_msfvx);
        check(supply_msfvx ? "constant zero response (msfvx)" : "constant zero response (fallback)",
              zero.abs().max().item<double>(), zero_tol);
        check(supply_msfvx ? "constant zero total (msfvx)" : "constant zero total (fallback)",
              std::abs(zero.sum().item<double>()), zero_tol);

        for (const auto& mode : {std::pair<const char*, torch::Tensor>{"x", qx},
                                 {"y", qy}}) {
            auto got = call(torch::ones_like(mode.second) + mode.second, supply_msfvx);
            const double lambda = mode.first[0] == 'x' ? lambda_x : lambda_y;
            auto expected = mode.second * (Kh * MUT * lambda); // dissipative sign
            auto interior = mode.first[0] == 'x'
                ? (got - expected).index({Slice(), Slice(), Slice(1, nx - 1)})
                : (got - expected).index({Slice(1, ny - 1), Slice(), Slice()});
            check((std::string("Fourier ") + mode.first + " interior eigenvalue").c_str(),
                  interior.abs().max().item<double>(), mode_tol);
            check((std::string("Fourier ") + mode.first + " q dot Lq").c_str(),
                  (mode.second * got).sum().item<double>(), mode_tol * mode.second.numel());
            check((std::string("Fourier ") + mode.first + " zero total").c_str(),
                  std::abs(got.sum().item<double>()), mode_tol * mode.second.numel());
        }
    }
    return ok;
}

// A packed periodic tile carries the first mass column again as its last
// alias. The unique physical core ends at nx-2, so H1[nx-1] is the seam face.
bool run_packed_periodic_seam(torch::Dtype dtype) {
    using wrf::sdirk3::test::TileCase;
    TileCase tile(1.0f);
    tile.solver.setWRFIndices(1,nx,1,ny,1,nz, 1,nx,1,ny,1,nz+1,
                              -2,nx+4,-2,ny+4,1,nz+1);
    const int n=nx-1;
    const auto opt=torch::TensorOptions().dtype(dtype).device(torch::kCPU);
    const auto phase=torch::arange(n,opt)*(2.0*pi/n);
    const auto scalar_core=torch::sin(phase).view({1,1,n}).expand({ny,nz,n});
    const auto q=torch::cat({scalar_core,scalar_core.slice(2,0,1)},2);
    const auto k_core=(2.0+0.1*torch::cos(phase)).view({1,1,n}).expand({ny,nz,n});
    const auto kh=torch::cat({k_core,k_core.slice(2,0,1)},2);
    const auto mut_core=(MUT+20.0*torch::sin(phase)).view({1,n}).expand({ny,n});
    const auto mut=torch::cat({mut_core,mut_core.slice(1,0,1)},1);
    const auto mx_core=(1.0+0.03*torch::cos(phase)).view({1,n}).expand({ny,n});
    const auto my_core=(1.0+0.02*torch::sin(phase)).view({1,n}).expand({ny,n});
    const auto mx=torch::cat({mx_core,mx_core.slice(1,0,1)},1);
    const auto my=torch::cat({my_core,my_core.slice(1,0,1)},1);
    torch::Tensor h1;
    const auto got=(tile.solver.*access(ScalarDiffusionTag{}))(
        q,kh,1.0f,1.0f,mx,my,torch::ones({ny+1,nx},opt),mut,&h1,nullptr,
        torch::Tensor(),torch::Tensor(),torch::Tensor());
    const auto expected=(0.5*(mx.select(1,n-1)+mx.select(1,0))/
                         (0.5*(my.select(1,n-1)+my.select(1,0)))).unsqueeze(1)*
        (0.5*(kh.select(2,n-1)+kh.select(2,0)))*
        (0.5*(mut.select(1,n-1)+mut.select(1,0))).unsqueeze(1)*
        (q.select(2,0)-q.select(2,n-1));
    const double seam_error=(h1.select(2,0)-expected).abs().max().item<double>();
    const double west_alias=(h1.select(2,0)-h1.select(2,n)).abs().max().item<double>();
    const double east_alias=(h1.select(2,nx)-h1.select(2,1)).abs().max().item<double>();
    const double tendency_alias=(got.select(2,n)-got.select(2,0))
        .abs().max().item<double>();
    const double unique_sum=std::abs((got.slice(2,0,n)/
        (mx.slice(1,0,n)*my.slice(1,0,n)).unsqueeze(1)).sum().item<double>());
    const double signal=expected.abs().max().item<double>();
    const double eps=dtype==torch::kFloat32 ? std::numeric_limits<float>::epsilon()
                                           : std::numeric_limits<double>::epsilon();
    const double budget=256.0*eps*std::max(1.0,signal);
    const bool pass=signal>1.0 && seam_error<budget && west_alias==0.0 &&
                    east_alias==0.0 && tendency_alias<budget &&
                    unique_sum<budget*ny*nz*n;
    std::cout << (pass ? "PASS " : "FAIL ") << "packed periodic scalar seam"
              << " seam=" << signal << " error=" << seam_error
              << " west_alias=" << west_alias << " east_alias=" << east_alias
              << " tendency_alias=" << tendency_alias
              << " unique_sum=" << unique_sum << " budget=" << budget << '\n';
    return pass;
}

// Flat normal stresses: Fortran tau=-2*rho*K*Dq and signed dnw=-1.
// Raw coupled tendency is +2*g*dz*rho*K*Lq, independent of column mass.
bool run_normal_stress(torch::Dtype dtype) {
    using wrf::sdirk3::test::TileCase;
    auto& cfg = wrf::sdirk3::g_sdirk3_config;
    cfg = wrf::sdirk3::SDIRK3Config{};
    cfg.diffusion_option = 2;
    const auto opt = torch::TensorOptions().dtype(dtype);
    const double eps = dtype == torch::kFloat32 ? std::numeric_limits<float>::epsilon()
                                               : std::numeric_limits<double>::epsilon();
    const double gravity = static_cast<double>(9.81f);
    const double dz[nz] = {10.0, 20.0, 15.0, 25.0};
    bool ok = true;
    for (bool u_mode : {true, false}) for (double density : {1.0, 0.7})
        for (double mass_scale : {1.0, 2.0}) {
            TileCase tile(1.0f);
            tile.useDoubleGridMetrics(); // prescribed |rdnw|=1, not a computed oracle input
            tile.solver.setTerrainSlopes(torch::zeros({ny,nz+1,nx}, opt),
                                          torch::zeros({ny,nz+1,nx}, opt));
            auto u = torch::zeros({ny,nz,nx+1}, opt);
            auto v = torch::zeros({ny+1,nz,nx}, opt);
            auto w = torch::zeros({ny,nz+1,nx}, opt);
            auto phi = torch::zeros_like(w);
            double height = 0.0;
            for (int k=0; k<=nz; ++k) {
                phi.select(1,k).fill_(gravity*height);
                if (k<nz) height += dz[k];
            }
            if (u_mode) for (int i=1; i<nx; ++i)
                u.select(2,i).fill_(std::sin(2*pi*i/nx));
            if (!u_mode) for (int j=1; j<ny; ++j)
                v.select(0,j).fill_(std::sin(pi*j/ny));
            const auto rho = torch::full({ny,nz,nx}, density, opt);
            const auto viscosity = torch::full_like(rho, Kh);
            const auto umap = torch::ones({ny,nx+1}, opt);
            const auto vmap = torch::ones({ny+1,nx}, opt);
            const auto mmap = torch::ones({ny,nx}, opt);
            const auto mu_u = mass_scale*MUT*(umap +
                0.03*torch::arange(nx+1,opt).view({1,nx+1}));
            const auto mu_v = mass_scale*MUT*(vmap +
                0.02*torch::arange(ny+1,opt).view({ny+1,1}));
            const auto actual = u_mode
                ? (tile.solver.*access(MomentumDiffusionTag{}))(
                    u,v,w,viscosity,1,1,umap,umap,mmap,mmap,mu_u,phi,rho)
                : (tile.solver.*access(VMomentumDiffusionTag{}))(
                    u,v,w,viscosity,1,1,vmap,vmap,mmap,mmap,mu_v,phi,rho);
            auto expected = torch::zeros_like(actual);
            const double rho_value = rho[0][0][0].item<double>();
            if (u_mode) for (int j=1; j<ny-1; ++j) for (int k=0; k<nz; ++k)
                for (int i=1; i<nx; ++i) {
                    const double lap = u[j][k][i+1].item<double>() -
                        2*u[j][k][i].item<double>() + u[j][k][i-1].item<double>();
                    expected[j][k][i] = 2*gravity*dz[k]*rho_value*Kh*lap;
                }
            if (!u_mode) for (int j=1; j<ny; ++j) for (int k=0; k<nz; ++k)
                for (int i=0; i<nx; ++i) {
                    const double lap = v[j+1][k][i].item<double>() -
                        2*v[j][k][i].item<double>() + v[j-1][k][i].item<double>();
                    expected[j][k][i] = 2*gravity*dz[k]*rho_value*Kh*lap;
                }
            const double error = (actual-expected).abs().max().item<double>();
            const double tolerance = 128*eps*std::max(1.0,expected.abs().max().item<double>());
            const double work = ((u_mode ? u : v)*actual).sum().item<double>();
            const bool pass = torch::isfinite(actual).all().item<bool>() &&
                error <= tolerance && work < 0.0;
            std::cout << (pass ? "PASS " : "FAIL ") << "normal stress "
                << (u_mode ? "U-X" : "V-Y") << " dtype=" << dtype << " rho=" << density
                << " mass_scale=" << mass_scale << " error=" << error
                << " tol=" << tolerance << " work=" << work << '\n';
            ok = pass && ok;
        }
    return ok;
}

bool run_actual_rhs_contract() {
    using namespace wrf::sdirk3;
    using namespace wrf::sdirk3::test;
    auto& cfg = g_sdirk3_config;
    cfg = SDIRK3Config{};
    cfg.diffusion_option = 2;
    cfg.khdif = cfg.kvdif = 1.0f;
    cfg.mass_coordinate_mode = 0;
    cfg.imex_split_mode = 3;
    cfg.hevi_split = false;
    cfg.use_stress_tensor = false;
    const auto evaluate = [](bool provide_coefficients) {
        TileCase tile;
        auto grid = tile.solver.getGridInfo();
        TORCH_CHECK(grid, "actual RHS fixture has no grid info");
        grid->rdn = torch::full({nz}, float(nz));
        grid->rdnw = torch::full({nw}, float(nz));
        const std::vector<float> c1f{0.73f, 1.11f, 0.89f, 1.27f, 0.61f};
        const std::vector<float> c2f{2.0f, -5.0f, 3.0f, 7.0f, -1.0f};
        const std::vector<float> c1h{0.67f, 1.19f, 0.83f, 1.31f};
        const std::vector<float> c2h{4.0f, -2.0f, 5.0f, 9.0f};
        (tile.solver.*access(CoordinateTag{}))(c1f.data(), c2f.data(), c1h.data(), c2h.data());
        auto fields = tile.fields();
        for (size_t f = 0; f < fields.size(); ++f)
            for (size_t n = 0; n < fields[f]->size(); ++n)
                (*fields[f])[n] = f == 5 ? 80000.0f + 0.25f * float(n)
                                          : 0.01f * float((f + 1) * (n + 1));
        if (provide_coefficients) {
            std::vector<float> kh_mom(ny * nz * nx, 1.0f), kh_scalar(ny * nz * nx, 3.0f);
            (tile.solver.*access(DiffusionTag{}))(kh_mom.data(), nullptr,
                                                   kh_scalar.data(), nullptr);
        }
        return (tile.solver.*access(ActualRhsTag{}))(
            tile.state(), RhsMode::Full).detach().clone();
    };
    const auto defaults = evaluate(false);
    const auto explicit_coefficients = evaluate(true);
    const auto diff = (defaults - explicit_coefficients).abs();
    const auto block_diff = [&](int64_t begin, int64_t end) {
        return diff.slice(0, begin, end).max().item<double>();
    };
    const double u_diff = block_diff(0, su), v_diff = block_diff(su, su + sv);
    const double w_diff = block_diff(su + sv, su + sv + sw);
    const double t_diff = block_diff(su + sv + 2 * sw, su + sv + 2 * sw + st);
    const double m_diff = block_diff(total - sm, total);
    const bool finite = torch::isfinite(defaults).all().item<bool>() &&
                        torch::isfinite(explicit_coefficients).all().item<bool>();
    const bool equal = torch::equal(defaults, explicit_coefficients);
    const bool pass = finite && defaults.numel() == total &&
                      explicit_coefficients.numel() == total && equal;
    std::cout << (pass ? "PASS " : "FAIL ")
              << "actual RHS option2 default/explicit block_max="
              << u_diff << ',' << v_diff << ',' << w_diff << ',' << t_diff << ',' << m_diff
              << " shape=" << defaults.numel() << " finite=" << finite
              << " equal=" << equal << '\n';
    return pass;
}

bool run_option1_scalar_rhs_contract() {
    using namespace wrf::sdirk3;
    using namespace wrf::sdirk3::test;
    auto& cfg=g_sdirk3_config;
    cfg=SDIRK3Config{};
    cfg.diffusion_option=1;
    cfg.khdif=2.0f;
    cfg.kvdif=0.0f;
    cfg.mass_coordinate_mode=0;
    cfg.imex_split_mode=3;
    cfg.hevi_split=false;
    cfg.wrf_omega_ww_cp=false;
    cfg.use_stress_tensor=false;
    const std::vector<float> c1h{1.1953125f,1.5703125f,1.4921875f,.296875f};
    const std::vector<float> c2h{4000.0f,-2000.0f,5000.0f,9000.0f};
    const auto prepare=[&](TileCase& tile) -> std::vector<float> {
        const std::vector<float> c1f(nz+1,1.0f),c2f(nz+1,0.0f);
        (tile.solver.*access(CoordinateTag{}))(
            c1f.data(),c2f.data(),c1h.data(),c2h.data());
        std::vector<float> pressure(st),t_init(st);
        for (int j=0;j<ny;++j) for (int k=0;k<nz;++k)
            for (int i=0;i<nx;++i) {
                const int index=(j*nz+k)*nx+i;
                pressure[index]=100000.0f-(k+.5f)*80000.0f/nz;
                t_init[index]=.5f*i+.125f*j+.0625f*((i*i)%3)+.03125f*k;
            }
        tile.solver.setBaseState(pressure.data(),t_init.data(),nullptr,nullptr);
        for (int j=0;j<ny;++j) for (int i=0;i<nx;++i)
            tile.mu[j*nx+i]=128.0f*i;
        for (int j=0;j<ny;++j) for (int k=0;k<nz;++k)
            for (int i=0;i<nx;++i)
                tile.theta[(j*nz+k)*nx+i]=t_init[(j*nz+k)*nx+i]+
                    .03125f*i+.015625f*k+.0625f*j+.0078125f*i*j;
        std::vector<float> ux(ny*(nx+1)),uy(ny*(nx+1)),vy((ny+1)*nx);
        for (int j=0;j<ny;++j) for (int f=0;f<=nx;++f) {
            const int x=f%nx;
            ux[j*(nx+1)+f]=1.0f+x/16.0f+j/128.0f;
            uy[j*(nx+1)+f]=1.0f+x/64.0f+j/32.0f;
        }
        for (int f=0;f<=ny;++f) for (int i=0;i<nx;++i)
            vy[f*nx+i]=1.0f+i/128.0f+f/16.0f;
        (tile.solver.*access(MapUxTag{}))=torch::tensor(ux).view({ny,nx+1});
        (tile.solver.*access(MapUyTag{}))=torch::tensor(uy).view({ny,nx+1});
        (tile.solver.*access(MapVyTag{}))=torch::tensor(vy).view({ny+1,nx});
        return t_init;
    };
    const auto reference=[&]() {
        TileCase tile(100.0f);
        const auto t_init=prepare(tile);
        const auto opt=torch::TensorOptions().dtype(torch::kFloat32);
        const auto q=torch::from_blob(tile.theta.data(),{ny,nz,nx},opt).clone()-
                     torch::tensor(t_init,opt).view({ny,nz,nx});
        const auto mut=torch::from_blob(tile.mu.data(),{ny,nx},opt).clone()+80000.0f;
        const auto c1=torch::tensor(c1h,opt).view({1,nz,1});
        const auto c2=torch::tensor(c2h,opt).view({1,nz,1});
        const auto layer=c1*mut.unsqueeze(1)+c2;
        const auto kh=torch::full_like(q,6.0f);
        const auto maps=torch::ones({ny,nx},opt);
        const auto coupled=(tile.solver.*access(ScalarDiffusionTag{}))(
            q,kh,.01f,.01f,maps,maps,torch::ones({ny+1,nx},opt),layer,
            nullptr,nullptr,tile.solver.*access(MapUxTag{}),
            tile.solver.*access(MapUyTag{}),tile.solver.*access(MapVyTag{}));
        // This RHS fixture leaves wrf_omega_ww_cp off, so the public
        // primitive-theta RHS divides the coupled diffusion by MU+MUB.
        return (coupled/mut.unsqueeze(1)).reshape({st}).detach().clone();
    }();
    const auto evaluate=[&](bool supply,RhsMode mode) {
        TileCase tile(100.0f);
        prepare(tile);
        if (supply) {
            std::vector<float> kh_mom(ny*nz*nx,2.0f);
            std::vector<float> kh_scalar(ny*nz*nx,6.0f);
            (tile.solver.*access(DiffusionTag{}))(
                kh_mom.data(),nullptr,kh_scalar.data(),nullptr);
        }
        return (tile.solver.*access(ActualRhsTag{}))(
            tile.state(),mode).detach().clone();
    };
    bool ok=true;
    for (auto mode : {RhsMode::Full,RhsMode::ExplicitOnly}) {
        const auto fallback=evaluate(false,mode);
        const auto supplied=evaluate(true,mode);
        cfg.khdif=0.0f;
        const auto off=evaluate(false,mode);
        cfg.khdif=2.0f;
        const auto theta_delta=(fallback-supplied)
            .slice(0,su+sv+2*sw,su+sv+2*sw+st);
        const auto theta_signal=(fallback-off)
            .slice(0,su+sv+2*sw,su+sv+2*sw+st);
        const double error=theta_delta.abs().max().item<double>();
        const double signal=theta_signal.abs().max().item<double>();
        const double reference_error=(theta_signal-reference).abs().max().item<double>();
        const double reference_budget=8.0*std::numeric_limits<float>::epsilon();
        const bool pass=torch::isfinite(fallback).all().item<bool>() &&
            torch::isfinite(supplied).all().item<bool>() &&
            torch::isfinite(off).all().item<bool>() && error==0.0 &&
            signal>100.0*reference_budget && reference_error<=reference_budget;
        std::cout << (pass?"PASS ":"FAIL ")
                  << "option1 scalar default/physical coefficient RHS mode="
                  << (mode==RhsMode::Full?"full":"explicit")
                  << " error=" << error << " signal=" << signal
                  << " reference_error=" << reference_error
                  << " budget=" << reference_budget << '\n';
        ok=pass && ok;
    }
    return ok;
}

// The 1D metric fallback must use one implied layer depth for the outer
// divergence factor and the terrain gradient, even with stretched eta levels.
bool run_u_terrain_fallback(torch::Dtype dtype) {
    auto& cfg=wrf::sdirk3::g_sdirk3_config;
    cfg=wrf::sdirk3::SDIRK3Config{};
    cfg.diffusion_option=2;
    cfg.z_top_default=10.0f; // Keep the small terrain signal resolved in FP32.
    // Reciprocal eta widths of {0.4,0.3,0.2,0.1}, summing to one.
    const std::vector<float> inverse_eta{2.5f,10.0f/3.0f,5.0f,10.0f};
    const std::vector<float> half(nz+1,0.5f);
    const auto opt=torch::TensorOptions().dtype(dtype);
    TileSDIRK3UnifiedSolver tile(nx,ny,nz,1.0f,1.0f,{1.0f},{1.0f},inverse_eta,0);
    tile.setVerticalInterpolationCoefficients(
        half.data(),half.data(),1.0f,0.0f,0.0f);
    auto u=torch::zeros({ny,nz,nx+1},opt);
    auto v=torch::zeros({ny+1,nz,nx},opt);
    auto w=torch::zeros({ny,nz+1,nx},opt);
    auto rho=torch::zeros({ny,nz,nx},opt);
    for (int k=0;k<nz;++k) rho.select(1,k).fill_(1.0+0.3*k);
    for (int i=1;i<nx;++i) u.select(2,i).fill_(std::sin(2*pi*i/nx));
    const auto viscosity=torch::full_like(rho,Kh);
    const auto umap=torch::ones({ny,nx+1},opt);
    const auto mmap=torch::ones({ny,nx},opt);
    const auto mass=torch::full_like(umap,MUT);
    const double slope=0.2, gravity=static_cast<double>(9.81f);
    const auto zx=torch::full({ny,nz+1,nx},slope,opt);
    const auto zy=torch::zeros_like(zx);
    const auto eval=[&](bool sloped) {
        tile.setTerrainSlopes(sloped?zx:torch::zeros_like(zx),zy);
        return (tile.*access(MomentumDiffusionTag{}))(
            u,v,w,viscosity,1,1,umap,umap,mmap,mmap,mass,torch::Tensor(),rho);
    };
    const auto delta=eval(true)-eval(false);
    double error=0.0,signal=0.0;
    for (int k=1;k<nz-1;++k) {
        const double adjacent=u[2][k][2].item<double>()-
                              u[2][k][0].item<double>();
        const double tau_low=-2*Kh*(1+0.3*(k-1))*adjacent;
        const double tau_high=-2*Kh*(1+0.3*(k+1))*adjacent;
        const double expected=gravity*inverse_eta[k]*slope*0.25*(tau_high-tau_low);
        const double observed=delta[2][k][1].item<double>();
        error=std::max(error,std::abs(observed-expected));
        signal=std::max(signal,std::abs(expected));
    }
    const double eps=dtype==torch::kFloat32 ? std::numeric_limits<float>::epsilon()
                                        : std::numeric_limits<double>::epsilon();
    const double tolerance=512*eps*(1+signal);
    const bool pass=torch::isfinite(delta).all().item<bool>() &&
        signal>tolerance && error<=tolerance;
    std::cout << (pass?"PASS ":"FAIL ") << "U terrain 1D dtype=" << dtype
              << " signal=" << signal << " error=" << error
              << " tol=" << tolerance << '\n';
    return pass;
}

// Hydrostatic flat fixture: rho*g*dz*|rdnw| equals dry column mass,
// so the independent velocity tendency is 2*K*Lq after one mass conversion.
// Fortran's g*dz/dnw multiplies the entire stress divergence. The dz in
// the terrain gradient must cancel, including on vertically stretched grids.
bool run_u_terrain_thickness(torch::Dtype dtype) {
    using wrf::sdirk3::test::TileCase;
    auto& cfg = wrf::sdirk3::g_sdirk3_config;
    cfg = wrf::sdirk3::SDIRK3Config{};
    cfg.diffusion_option = 2;
    const auto opt = torch::TensorOptions().dtype(dtype);
    const double gravity = static_cast<double>(9.81f), slope = 0.2;
    const double eps = dtype == torch::kFloat32 ? std::numeric_limits<float>::epsilon()
                                               : std::numeric_limits<double>::epsilon();
    bool ok = true;
    for (double depth_scale : {1.0, 2.0}) {
        TileCase tile(1.0f);
        tile.useDoubleGridMetrics();
        // Preserve a vertically constant velocity at the bottom W face.
        tile.solver.setVerticalInterpolationCoefficients(
            tile.half.data(),tile.half.data(),1.0f,0.0f,0.0f);
        auto u = torch::zeros({ny,nz,nx+1},opt);
        auto v = torch::zeros({ny+1,nz,nx},opt);
        auto w = torch::zeros({ny,nz+1,nx},opt);
        for (int i=1; i<nx; ++i) u.select(2,i).fill_(std::sin(2*pi*i/nx));
        auto rho = torch::zeros({ny,nz,nx},opt);
        for (int k=0; k<nz; ++k) rho.select(1,k).fill_(1.0+0.3*k);
        const auto viscosity = torch::full_like(rho,Kh);
        const auto umap = torch::ones({ny,nx+1},opt);
        const auto mmap = torch::ones({ny,nx},opt);
        const auto mass = torch::full({ny,nx+1},MUT,opt);
        auto phi = torch::zeros({ny,nz+1,nx},opt);
        const double dz[nz] = {10.0,20.0,15.0,25.0};
        double height = 0.0;
        for (int k=0; k<=nz; ++k) {
            for (int i=0; i<nx; ++i) phi.select(1,k).select(1,i).fill_(
                gravity*(height+slope*i));
            if (k<nz) height += depth_scale*dz[k];
        }
        const auto slope_x = torch::full({ny,nz+1,nx},slope,opt);
        const auto slope_y = torch::zeros_like(slope_x);
        const auto eval = [&](const torch::Tensor& zx) {
            tile.solver.setTerrainSlopes(zx,slope_y);
            return (tile.solver.*access(MomentumDiffusionTag{}))(
                u,v,w,viscosity,1,1,umap,umap,mmap,mmap,mass,phi,rho).clone();
        };
        const auto delta = eval(slope_x)-eval(torch::zeros_like(slope_x));
        double error=0.0, signal=0.0;
        for (int k=1; k<nz-1; ++k) for (int i=1; i<nx; ++i) {
            const double adjacent = u[2][k][i+1].item<double>()-
                                    u[2][k][i-1].item<double>();
            const double tau_low = -2*Kh*(1+0.3*(k-1))*adjacent;
            const double tau_high = -2*Kh*(1+0.3*(k+1))*adjacent;
            // With fnm=fnp=1/2, the adjacent W-face average difference
            // is one quarter of the two-level mass-stress difference.
            const double expected = gravity*slope*0.25*(tau_high-tau_low);
            const double observed = delta[2][k][i].item<double>();
            error = std::max(error,std::abs(observed-expected));
            signal = std::max(signal,std::abs(expected));
        }
        const double tolerance = 512*eps*(1+signal);
        const bool pass = torch::isfinite(delta).all().item<bool>() &&
            signal > tolerance && error <= tolerance;
        std::cout << (pass ? "PASS " : "FAIL ") << "U terrain depth=" << depth_scale
                  << " dtype=" << dtype << " signal=" << signal
                  << " error=" << error << " tol=" << tolerance << '\n';
        ok = pass && ok;
    }
    return ok;
}

bool run_normal_rhs() {
    using namespace wrf::sdirk3;
    using wrf::sdirk3::test::TileCase;
    constexpr float spacing = 1000.0f, viscosity = 1000.0f;
    const auto evaluate = [](bool u_mode, bool on, RhsMode mode) {
        auto& cfg = g_sdirk3_config;
        cfg = SDIRK3Config{};
        cfg.diffusion_option = 2;
        cfg.khdif = on ? viscosity : 0.0f;
        cfg.kvdif = 0.0f;
        cfg.mass_coordinate_mode = 0;
        cfg.wrf_omega_ww_cp = false;
        cfg.imex_split_mode = 3;
        cfg.hevi_split = false;
        cfg.use_stress_tensor = false;
        TileCase tile(spacing);
        tile.solver.setTerrainSlopes(torch::zeros({ny,nz+1,nx}),
                                      torch::zeros({ny,nz+1,nx}));
        (tile.solver.*access(CoordinateTag{}))(
            tile.one.data(),tile.zero.data(),tile.one.data(),tile.zero.data());
        auto grid = tile.solver.getGridInfo();
        grid->rdn = torch::full({nz},float(nz));
        grid->rdnw = torch::full({nz+1},float(nz));
        for (int j=0; j<ny; ++j) for (int k=0; k<nz; ++k)
            for (int i=1; i<nx; ++i)
                tile.u[(j*nz+k)*(nx+1)+i] = u_mode ? std::sin(2*pi*i/nx) : 0;
        for (int j=1; j<ny; ++j) for (int k=0; k<nz; ++k)
            for (int i=0; i<nx; ++i)
                tile.v[(j*nz+k)*nx+i] = u_mode ? 0 : std::sin(pi*j/ny);
        return (tile.solver.*access(ActualRhsTag{}))(tile.state(),mode).detach().clone();
    };
    constexpr int su = ny*nz*(nx+1), sv = (ny+1)*nz*nx;
    bool ok = true;
    for (bool u_mode : {true,false}) {
        const auto full = evaluate(u_mode,true,RhsMode::Full) -
                          evaluate(u_mode,false,RhsMode::Full);
        const auto explicit_delta = evaluate(u_mode,true,RhsMode::ExplicitOnly) -
                                    evaluate(u_mode,false,RhsMode::ExplicitOnly);
        const auto select = [=](const torch::Tensor& rhs) {
            return u_mode ? rhs.slice(0,0,su).view({ny,nz,nx+1}) :
                            rhs.slice(0,su,su+sv).view({ny+1,nz,nx});
        };
        const auto actual = select(full);
        double error = 0.0, work = 0.0;
        const double lambda = -4*std::pow(std::sin(u_mode ? pi/nx : pi/(2*ny)),2)
                              / (spacing*spacing);
        for (int j=1; j<ny-1; ++j) for (int k=0; k<nz; ++k)
            for (int i=1; i<nx; ++i) {
                const double wave = u_mode ? std::sin(2*pi*i/nx) : std::sin(pi*j/ny);
                const double value = actual[j][k][i].item<double>();
                error = std::max(error,std::abs(value-2*viscosity*lambda*wave));
                work += wave*value;
            }
        const double split_error = (actual-select(explicit_delta)).abs().max().item<double>();
        // Absolute FP32 engineering budget for this prescribed velocity RHS fixture.
        const double tolerance = 1.0e-6;
        const bool pass = torch::isfinite(full).all().item<bool>() &&
            torch::isfinite(explicit_delta).all().item<bool>() &&
            error <= tolerance && split_error <= tolerance && work < 0.0;
        std::cout << (pass ? "PASS " : "FAIL ") << "normal RHS "
            << (u_mode ? "U-X" : "V-Y") << " error=" << error
            << " full_explicit=" << split_error << " tol=" << tolerance
            << " work=" << work << '\n';
        ok = pass && ok;
    }
    return ok;
}

// In option 2, W stresses must use the same mass-point physical density
// as U/V. Holding geometry fixed makes their diffusion linear in (1+qv).
bool run_w_density_state() {
    using namespace wrf::sdirk3;
    using wrf::sdirk3::test::TileCase;
    const auto evaluate = [](double qv, bool diffusion, float theta_pert = 0.0f) {
        auto& cfg = g_sdirk3_config;
        cfg = SDIRK3Config{};
        cfg.diffusion_option = 2;
        cfg.khdif = 0.0f;
        cfg.kvdif = diffusion ? 1000.0f : 0.0f;
        cfg.mass_coordinate_mode = 0;
        cfg.imex_split_mode = 3;
        cfg.hevi_split = false;
        cfg.use_stress_tensor = false;
        TileCase tile(1000.0f);
        (tile.solver.*access(CoordinateTag{}))(
            tile.one.data(),tile.zero.data(),tile.one.data(),tile.zero.data());
        auto grid = tile.solver.getGridInfo();
        grid->rdn = torch::full({nz},float(nz));
        grid->rdnw = torch::full({nz+1},float(nz));
        grid->qv = torch::full({ny,nz,nx},qv);
        std::fill(tile.theta.begin(),tile.theta.end(),theta_pert);
        for (int j=0; j<ny; ++j) for (int k=0; k<nz; ++k)
            for (int i=1; i<nx; ++i)
                tile.u[(j*nz+k)*(nx+1)+i] =
                    (k+1)*std::sin(2*pi*i/nx);
        return (tile.solver.*access(ActualRhsTag{}))(
            tile.state(),RhsMode::Full).detach().clone();
    };
    constexpr int su=ny*nz*(nx+1), sv=(ny+1)*nz*nx;
    constexpr int sw=ny*(nz+1)*nx;
    const auto dry = (evaluate(0.0,true)-evaluate(0.0,false))
        .slice(0,su+sv,su+sv+sw);
    const auto moist = (evaluate(0.2,true)-evaluate(0.2,false))
        .slice(0,su+sv,su+sv+sw);
    // At fixed PH and column mass, calc_p_rho's alt is unchanged when
    // potential temperature (and its diagnosed pressure) changes.
    const auto warm = (evaluate(0.0,true,30.0f)-evaluate(0.0,false,30.0f))
        .slice(0,su+sv,su+sv+sw);
    const double signal = (0.2*dry).abs().max().item<double>();
    const double error = (moist-1.2*dry).abs().max().item<double>();
    const double theta_error = (warm-dry).abs().max().item<double>();
    const double tolerance = 256*std::numeric_limits<float>::epsilon()*(1+signal);
    const bool pass = torch::isfinite(dry).all().item<bool>() &&
        torch::isfinite(moist).all().item<bool>() &&
        torch::isfinite(warm).all().item<bool>() &&
        signal > 10*tolerance && error <= tolerance && theta_error <= tolerance;
    std::cout << (pass ? "PASS " : "FAIL ")
              << "W physical-density qv response signal=" << signal
              << " qv_error=" << error << " theta_error=" << theta_error
              << " tol=" << tolerance << '\n';
    return pass;
}

bool run_scalar_zero_gate() {
    using namespace wrf::sdirk3;
    using namespace wrf::sdirk3::test;
    auto& cfg = g_sdirk3_config;
    cfg = SDIRK3Config{};
    cfg.khdif = cfg.kvdif = 0.0f;
    cfg.mass_coordinate_mode = 0;
    cfg.imex_split_mode = 3;
    cfg.hevi_split = false;
    cfg.use_stress_tensor = false;
    const auto evaluate = [](bool scalar) {
        TileCase tile(1000.0f);
        auto grid = tile.solver.getGridInfo();
        TORCH_CHECK(grid, "scalar gate fixture has no grid info");
        grid->rdn = torch::full({nz}, float(nz));
        grid->rdnw = torch::full({nw}, float(nz));
        const std::vector<float> c1f{0.73f, 1.11f, 0.89f, 1.27f, 0.61f};
        const std::vector<float> c2f{2.0f, -5.0f, 3.0f, 7.0f, -1.0f};
        const std::vector<float> c1h{0.67f, 1.19f, 0.83f, 1.31f};
        const std::vector<float> c2h{4.0f, -2.0f, 5.0f, 9.0f};
        (tile.solver.*access(CoordinateTag{}))(c1f.data(), c2f.data(), c1h.data(), c2h.data());
        for (size_t n = 0; n < tile.theta.size(); ++n)
            tile.theta[n] = std::sin(0.31f * float(n)) + 0.1f * std::cos(0.07f * float(n));
        if (scalar) {
            std::vector<float> kh_scalar(ny * nz * nx, 3.0f);
            (tile.solver.*access(DiffusionTag{}))(nullptr, nullptr, kh_scalar.data(), nullptr);
        }
        return (tile.solver.*access(ActualRhsTag{}))(
            tile.state(), RhsMode::Full).detach().clone();
    };
    bool ok=true;
    for (int option : {1,2}) {
        cfg.diffusion_option=option;
        const auto off=evaluate(false),on=evaluate(true),delta=on-off;
        const double du=delta.slice(0,0,su).abs().max().item<double>();
        const double dv=delta.slice(0,su,su+sv).abs().max().item<double>();
        const double dw=delta.slice(0,su+sv,su+sv+sw).abs().max().item<double>();
        const double dt=delta.slice(0,su+sv+2*sw,su+sv+2*sw+st)
                              .abs().max().item<double>();
        const bool pass=torch::isfinite(on).all().item<bool>() &&
            du==0.0 && dv==0.0 && dw==0.0 && dt>0.0;
        std::cout << (pass?"PASS ":"FAIL ")
                  << "actual RHS khdif0 scalar gate option=" << option
                  << " du=" << du << " dv=" << dv
                  << " dw=" << dw << " dt=" << dt << '\n';
        ok=pass && ok;
    }
    return ok;
}
void dump_flat_periodic_x(torch::Dtype dtype) {
    using wrf::sdirk3::test::TileCase;
    TileCase tile(10.0f);
    const auto opt=torch::TensorOptions().dtype(dtype).device(torch::kCPU);
    const auto i=torch::arange(nx,opt);
    const auto q=(1.0+0.03*torch::sin(i*(2.0*pi/nx)))
        .view({1,1,nx}).expand({ny,nz,nx}).clone();
    const auto kh=torch::full({ny,nz,nx},2.0,opt);
    const auto map=torch::ones({ny,nx},opt);
    const auto mut=torch::full({ny,nx},784.8,opt);
    const auto out=(tile.solver.*access(ScalarDiffusionTag{}))(
        q,kh,0.1f,0.13f,map,map,torch::ones({ny+1,nx},opt),mut,
        nullptr,nullptr,torch::Tensor(),torch::Tensor(),torch::Tensor())
        .to(torch::kFloat64).contiguous();
    const auto a=out.accessor<double,3>();
    for (int j=0; j<ny; ++j)
        for (int k=0; k<nz; ++k)
            for (int x=0; x<nx; ++x)
                std::cout << "C_PARITY " << j+1 << ' ' << k+1 << ' ' << x+1
                          << ' ' << std::setprecision(17) << a[j][k][x] << '\n';
}

void dump_hybrid_layer_mass(torch::Dtype dtype, const std::string& mass_mode) {
    using wrf::sdirk3::test::TileCase;
    TileCase tile(10.0f);
    const auto opt=torch::TensorOptions().dtype(dtype).device(torch::kCPU);
    std::vector<double> q_values, kh_values, mut_values, layer_values, no_c2_values;
    constexpr double c1[nz]={1.1953125,1.5703125,1.4921875,0.296875};
    for (int j=0; j<ny; ++j) {
        for (int k=0; k<nz; ++k) {
            for (int x=0; x<nx; ++x) {
                const double mut=90000.0+128.0*x;
                q_values.push_back(1.0+0.03125*x+0.0078125*((x*x)%3)+0.015625*k);
                kh_values.push_back(2.0+0.125*x+0.0625*k);
                layer_values.push_back(c1[k]*mut+(1.0-c1[k])*80000.0);
                no_c2_values.push_back(c1[k]*mut);
            }
        }
    }
    for (int j=0; j<ny; ++j)
        for (int x=0; x<nx; ++x)
            mut_values.push_back(90000.0+128.0*x);
    auto tensor=[&](const std::vector<double>& values, std::vector<int64_t> shape) {
        return torch::tensor(values,torch::TensorOptions().dtype(torch::kFloat64))
            .to(dtype).reshape(shape);
    };
    const auto q=tensor(q_values,{ny,nz,nx});
    const auto kh=tensor(kh_values,{ny,nz,nx});
    const auto mut=tensor(mut_values,{ny,nx});
    const auto layer=tensor(layer_values,{ny,nz,nx});
    const auto no_c2=tensor(no_c2_values,{ny,nz,nx});
    const auto map=torch::ones({ny,nx},opt);
    torch::Tensor mass;
    if (mass_mode=="hybrid") mass=layer;
    else if (mass_mode=="legacy") mass=mut;
    else if (mass_mode=="no-c2") mass=no_c2;
    else if (mass_mode=="sigma2d") mass=mut;
    else if (mass_mode=="sigma3d") mass=mut.unsqueeze(1).expand({ny,nz,nx});
    else throw std::invalid_argument("unknown layer-mass mode");
    const auto out=(tile.solver.*access(ScalarDiffusionTag{}))(
        q,kh,0.1f,0.13f,map,map,torch::ones({ny+1,nx},opt),mass,
        nullptr,nullptr,torch::ones({ny,nx+1},opt),
        torch::ones({ny,nx+1},opt),torch::ones({ny+1,nx},opt))
        .to(torch::kFloat64).contiguous();
    const auto a=out.accessor<double,3>();
    for (int j=0; j<ny; ++j)
        for (int k=0; k<nz; ++k)
            for (int x=0; x<nx; ++x)
                std::cout << "C_HYBRID " << j+1 << ' ' << k+1 << ' ' << x+1
                          << ' ' << std::setprecision(17) << a[j][k][x] << '\n';
}

void dump_hybrid_face_maps(torch::Dtype dtype, const std::string& map_mode) {
    using wrf::sdirk3::test::TileCase;
    TileCase tile(10.0f);
    const auto opt=torch::TensorOptions().dtype(dtype).device(torch::kCPU);
    constexpr double c1[nz]={1.1953125,1.5703125,1.4921875,.296875};
    const bool old_base=map_mode=="old-base";
    std::vector<double> qv,khv,lv,mxv,myv,muxv,muyv,mvxv,mvyv;
    for (int j=0;j<ny;++j) for (int k=0;k<nz;++k) for (int x=0;x<nx;++x) {
        const double mut=90000.0+128.0*x+256.0*j;
        const double q=1.0+.03125*x+.0078125*((x*x)%3)+.015625*k+
                       .0625*j+.015625*((j*j)%2)+.0078125*x*j;
        qv.push_back(q+(old_base ? .5*x+.125*j+.0625*((x*x)%3)+.03125*k : 0.0));
        khv.push_back(2.0+.125*x+.0625*k+.03125*j);
        lv.push_back(c1[k]*mut+(1.0-c1[k])*80000.0);
    }
    for (int j=0;j<ny;++j) for (int x=0;x<nx;++x) {
        mxv.push_back(1.0+x/32.0+j/64.0);
        myv.push_back(1.0+x/64.0+j/32.0);
    }
    const bool old_x=map_mode=="old-x" || map_mode=="old-both";
    const bool old_y=map_mode=="old-y" || map_mode=="old-both";
    if (map_mode!="exact" && map_mode!="base" && !old_base && !old_x && !old_y)
        throw std::invalid_argument("unknown stagger-map mode");
    for (int j=0;j<ny;++j) for (int f=0;f<=nx;++f) {
        const int x=f%nx, left=(f+nx-1)%nx;
        muxv.push_back(old_x ? .5*(mxv[j*nx+left]+mxv[j*nx+x])
                             : 1.0+x/16.0+j/128.0);
        muyv.push_back(old_x ? .5*(myv[j*nx+left]+myv[j*nx+x])
                             : 1.0+x/64.0+j/32.0);
    }
    for (int f=0;f<=ny;++f) for (int x=0;x<nx;++x) {
        mvxv.push_back(1.0+x/32.0+f/128.0);
        mvyv.push_back(old_y && f>0 && f<ny
            ? .5*(myv[(f-1)*nx+x]+myv[f*nx+x])
            : 1.0+x/128.0+f/16.0);
    }
    const auto tensor=[&](const std::vector<double>& values,std::vector<int64_t> shape) {
        return torch::tensor(values,torch::TensorOptions().dtype(torch::kFloat64))
            .to(dtype).reshape(shape);
    };
    const auto out=(tile.solver.*access(ScalarDiffusionTag{}))(
        tensor(qv,{ny,nz,nx}),tensor(khv,{ny,nz,nx}),.1f,.13f,
        tensor(mxv,{ny,nx}),tensor(myv,{ny,nx}),tensor(mvxv,{ny+1,nx}),
        tensor(lv,{ny,nz,nx}),nullptr,nullptr,tensor(muxv,{ny,nx+1}),
        tensor(muyv,{ny,nx+1}),tensor(mvyv,{ny+1,nx}))
        .to(torch::kFloat64).contiguous();
    const auto a=out.accessor<double,3>();
    for (int j=0;j<ny;++j) for (int k=0;k<nz;++k) for (int x=0;x<nx;++x)
        std::cout << "C_MAP " << j+1 << ' ' << k+1 << ' ' << x+1 << ' '
                  << std::setprecision(17) << a[j][k][x] << '\n';
}
} // namespace

int main(int argc, char** argv) {
    if (argc==4 && std::string(argv[1])=="--hybrid-map-parity") {
        if (std::string(argv[2])=="fp32") dump_hybrid_face_maps(torch::kFloat32,argv[3]);
        else if (std::string(argv[2])=="fp64") dump_hybrid_face_maps(torch::kFloat64,argv[3]);
        else return 2;
        return 0;
    }
    if (argc==4 && std::string(argv[1])=="--hybrid-layer-parity") {
        if (std::string(argv[2])=="fp32") dump_hybrid_layer_mass(torch::kFloat32,argv[3]);
        else if (std::string(argv[2])=="fp64") dump_hybrid_layer_mass(torch::kFloat64,argv[3]);
        else return 2;
        return 0;
    }
    if (argc==3 && std::string(argv[1])=="--flat-x-parity") {
        if (std::string(argv[2])=="fp32") dump_flat_periodic_x(torch::kFloat32);
        else if (std::string(argv[2])=="fp64") dump_flat_periodic_x(torch::kFloat64);
        else return 2;
        return 0;
    }
    bool ok = run(torch::kFloat32);
    ok = run(torch::kFloat64) && ok;
    ok = run_packed_periodic_seam(torch::kFloat32) && ok;
    ok = run_packed_periodic_seam(torch::kFloat64) && ok;
    ok = run_normal_stress(torch::kFloat32) && ok;
    ok = run_normal_stress(torch::kFloat64) && ok;
    ok = run_actual_rhs_contract() && ok;
    ok = run_option1_scalar_rhs_contract() && ok;
    ok = run_scalar_zero_gate() && ok;
    ok = run_normal_rhs() && ok;
    ok = run_u_terrain_thickness(torch::kFloat32) && ok;
    ok = run_u_terrain_thickness(torch::kFloat64) && ok;
    ok = run_w_density_state() && ok;
    ok = run_u_terrain_fallback(torch::kFloat32) && ok;
    ok = run_u_terrain_fallback(torch::kFloat64) && ok;
    std::cout << (ok ? "scalar diffusion contract: PASS\n"
                     : "scalar diffusion contract: FAIL\n");
    return ok ? 0 : 1;
}
