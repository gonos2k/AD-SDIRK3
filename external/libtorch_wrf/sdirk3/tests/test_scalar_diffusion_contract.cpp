// Direct production-library scalar diffusion contract.
#include "../wrf_sdirk3_tile_unified.h"
#include "tile_test_fixture.h"
#include <torch/torch.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

struct ScalarDiffusionTag {
    using type = torch::Tensor (TileSDIRK3UnifiedSolver::*)
        (const torch::Tensor&, const torch::Tensor&, float, float,
         const torch::Tensor&, const torch::Tensor&, const torch::Tensor&,
         const torch::Tensor&);
    friend type access(ScalarDiffusionTag);
};
template<typename Tag, typename Tag::type Member> struct MemberAccessor {
    friend typename Tag::type access(Tag) { return Member; }
};
template struct MemberAccessor<ScalarDiffusionTag,
                               &TileSDIRK3UnifiedSolver::compute_horizontal_diffusion_scalar_wrf>;

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
            supply_msfvx ? msfvx : torch::Tensor(), mut);
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

// Hydrostatic flat fixture: rho*g*dz*|rdnw| equals dry column mass,
// so the independent velocity tendency is 2*K*Lq after one mass conversion.
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

bool run_scalar_zero_gate() {
    using namespace wrf::sdirk3;
    using namespace wrf::sdirk3::test;
    auto& cfg = g_sdirk3_config;
    cfg = SDIRK3Config{};
    cfg.diffusion_option = 2;
    cfg.khdif = cfg.kvdif = 0.0f;
    cfg.mass_coordinate_mode = 0;
    cfg.imex_split_mode = 3;
    cfg.hevi_split = false;
    cfg.use_stress_tensor = false;
    const auto evaluate = [](bool scalar) {
        TileCase tile;
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
    const auto off = evaluate(false), on = evaluate(true), delta = on - off;
    const double du = delta.slice(0, 0, su).abs().max().item<double>();
    const double dv = delta.slice(0, su, su + sv).abs().max().item<double>();
    const double dw = delta.slice(0, su + sv, su + sv + sw).abs().max().item<double>();
    const double dt = delta.slice(0, su + sv + 2 * sw, su + sv + 2 * sw + st)
                            .abs().max().item<double>();
    const bool pass = torch::isfinite(on).all().item<bool>() && du == 0.0 && dv == 0.0 &&
                      dw == 0.0 && dt > 0.0;
    std::cout << (pass ? "PASS " : "FAIL ") << "actual RHS khdif0 scalar gate"
              << " du=" << du << " dv=" << dv << " dw=" << dw << " dt=" << dt << '\n';
    return pass;
}
} // namespace

int main() {
    bool ok = run(torch::kFloat32);
    ok = run(torch::kFloat64) && ok;
    ok = run_normal_stress(torch::kFloat32) && ok;
    ok = run_normal_stress(torch::kFloat64) && ok;
    ok = run_actual_rhs_contract() && ok;
    ok = run_scalar_zero_gate() && ok;
    ok = run_normal_rhs() && ok;
    std::cout << (ok ? "scalar diffusion contract: PASS\n"
                     : "scalar diffusion contract: FAIL\n");
    return ok ? 0 : 1;
}
