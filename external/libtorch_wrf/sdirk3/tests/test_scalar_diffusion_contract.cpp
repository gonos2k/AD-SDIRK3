// Direct production-library scalar diffusion contract.
#include "../wrf_sdirk3_tile_unified.h"
#include "tile_test_fixture.h"
#include <torch/torch.h>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <limits>
#include <string>
#include <tuple>
#include <type_traits>
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

struct Option2ScalarTag {
    using type = torch::Tensor (TileSDIRK3UnifiedSolver::*)(
        const torch::Tensor&,const torch::Tensor&,const torch::Tensor&,
        const torch::Tensor&,const torch::Tensor&,const torch::Tensor&,
        const torch::Tensor&,const torch::Tensor&,const torch::Tensor&,
        const torch::Tensor&,double,double,double,
        float,float,double,const torch::Tensor&,const torch::Tensor&,
        const torch::Tensor&,const torch::Tensor&);
    friend type access(Option2ScalarTag);
};
template struct MemberAccessor<Option2ScalarTag,
    &TileSDIRK3UnifiedSolver::compute_horizontal_diffusion_scalar_option2_wrf>;

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
struct MapVxTag {
    using type = torch::Tensor TileSDIRK3UnifiedSolver::*;
    friend type access(MapVxTag);
};
struct MapTxTag {
    using type = torch::Tensor TileSDIRK3UnifiedSolver::*;
    friend type access(MapTxTag);
};
struct MapTyTag {
    using type = torch::Tensor TileSDIRK3UnifiedSolver::*;
    friend type access(MapTyTag);
};
template struct MemberAccessor<MapUxTag, &TileSDIRK3UnifiedSolver::msfux_>;
template struct MemberAccessor<MapUyTag, &TileSDIRK3UnifiedSolver::msfuy_>;
template struct MemberAccessor<MapVyTag, &TileSDIRK3UnifiedSolver::msfvy_>;
template struct MemberAccessor<MapVxTag, &TileSDIRK3UnifiedSolver::msfvx_>;
template struct MemberAccessor<MapTxTag, &TileSDIRK3UnifiedSolver::msftx_>;
template struct MemberAccessor<MapTyTag, &TileSDIRK3UnifiedSolver::msfty_>;

struct Option1MomentumTag {
    using type = std::tuple<torch::Tensor,torch::Tensor,torch::Tensor>
        (TileSDIRK3UnifiedSolver::*)(const torch::Tensor&,const torch::Tensor&,
                                     const torch::Tensor&,const torch::Tensor&,
                                     const torch::Tensor&,float,float);
    friend type access(Option1MomentumTag);
};
template struct MemberAccessor<Option1MomentumTag,
    &TileSDIRK3UnifiedSolver::compute_horizontal_diffusion_option1_momentum>;

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
struct FnmFromWrfTag {
    using type = bool TileSDIRK3UnifiedSolver::*;
    friend type access(FnmFromWrfTag);
};
template struct MemberAccessor<FnmFromWrfTag,
                               &TileSDIRK3UnifiedSolver::fnm_fnp_from_wrf_>;
struct WdampContractTag {
    using type = wrf::sdirk3::WdampRuntimeContract TileSDIRK3UnifiedSolver::*;
    friend type access(WdampContractTag);
};
template struct MemberAccessor<WdampContractTag,
                               &TileSDIRK3UnifiedSolver::wdamp_contract_>;
struct RdnwTag {
    using type = torch::Tensor (TileSDIRK3UnifiedSolver::*)(
        const torch::Device&,torch::ScalarType,int64_t) const;
    friend type access(RdnwTag);
};
struct RdnTag { using type = RdnwTag::type; friend type access(RdnTag); };
template struct MemberAccessor<RdnwTag,&TileSDIRK3UnifiedSolver::getRdnwTensor>;
template struct MemberAccessor<RdnTag,&TileSDIRK3UnifiedSolver::getRdnTensor>;
struct KhMomOverrideTag { using type = torch::Tensor TileSDIRK3UnifiedSolver::*;
    friend type access(KhMomOverrideTag); };
struct KhScalarOverrideTag { using type = KhMomOverrideTag::type;
    friend type access(KhScalarOverrideTag); };
template struct MemberAccessor<KhMomOverrideTag,&TileSDIRK3UnifiedSolver::Kh_mom_>;
template struct MemberAccessor<KhScalarOverrideTag,&TileSDIRK3UnifiedSolver::Kh_scalar_>;
struct CaptureThetaTag { using type = bool TileSDIRK3UnifiedSolver::*;
    friend type access(CaptureThetaTag); };
template struct MemberAccessor<CaptureThetaTag,
                               &TileSDIRK3UnifiedSolver::capture_theta_faces_now_>;
template struct MemberAccessor<DiffusionTag,
                               &TileSDIRK3UnifiedSolver::setDiffusionCoefficients>;

struct UOption2HelperTag { friend auto access(UOption2HelperTag); };
struct VOption2HelperTag { friend auto access(VOption2HelperTag); };
template<typename Tag, auto Member> struct AutoMemberAccessor {
    friend auto access(Tag) { return Member; }
};
template struct AutoMemberAccessor<UOption2HelperTag,
    &TileSDIRK3UnifiedSolver::compute_horizontal_diffusion_u_wrf>;
template struct AutoMemberAccessor<VOption2HelperTag,
    &TileSDIRK3UnifiedSolver::compute_horizontal_diffusion_v_wrf>;

struct Option2WHelperTag { friend auto access(Option2WHelperTag); };
template struct AutoMemberAccessor<Option2WHelperTag,
    &TileSDIRK3UnifiedSolver::compute_horizontal_diffusion_w_wrf>;

struct Defor13StageGeometryTag {
    using type = torch::Tensor (TileSDIRK3UnifiedSolver::*)(
        const torch::Tensor&, const torch::Tensor&, const torch::Tensor&,
        const torch::Tensor&, const torch::Tensor&, const torch::Tensor&,
        const torch::Tensor&, const torch::Tensor&);
    friend type access(Defor13StageGeometryTag);
};
template struct MemberAccessor<Defor13StageGeometryTag,
    &TileSDIRK3UnifiedSolver::compute_defor13>;

template<typename Member>
torch::Tensor call_option2_momentum_helper(
    Member member, TileSDIRK3UnifiedSolver& solver,
    const torch::Tensor& u, const torch::Tensor& v, const torch::Tensor& w,
    const torch::Tensor& kh, float rdx, float rdy,
    const torch::Tensor& map_u, const torch::Tensor& map_v,
    const torch::Tensor& map_m, const torch::Tensor& muu,
    const torch::Tensor& ph_full, const torch::Tensor& rho,
    const torch::Tensor& zx, const torch::Tensor& zy,
    const torch::Tensor& rdzw, const torch::Tensor& rdz,
    bool report_explicit_geometry=false) {
    if constexpr (std::is_invocable_v<Member, TileSDIRK3UnifiedSolver&,
                  const torch::Tensor&, const torch::Tensor&, const torch::Tensor&,
                  const torch::Tensor&, float, float, const torch::Tensor&,
                  const torch::Tensor&, const torch::Tensor&, const torch::Tensor&,
                  const torch::Tensor&, const torch::Tensor&, const torch::Tensor&,
                  const torch::Tensor&, const torch::Tensor&, const torch::Tensor&,
                  const torch::Tensor&>) {
        if (report_explicit_geometry) std::cout << "GEOMETRY_INPUT stage_explicit=1\n";
        return (solver.*member)(u,v,w,kh,rdx,rdy,map_u,map_v,map_m,map_m,muu,
                                ph_full,rho,zx,zy,rdzw,rdz);
    } else {
        if (report_explicit_geometry) std::cout << "GEOMETRY_INPUT stage_explicit=0\n";
        return (solver.*member)(u,v,w,kh,rdx,rdy,map_u,map_v,map_m,map_m,muu,
                                ph_full,rho);
    }
}
namespace {
constexpr int nx = 8, ny = 6, nz = 4;
constexpr int nu=nx+1,nv=ny+1,nw=nz+1;
constexpr int su=ny*nz*nu,sv=nv*nz*nx,sw=ny*nw*nx,st=ny*nz*nx,sm=ny*nx;
constexpr int total=su+sv+2*sw+st+sm;
constexpr double pi = 3.14159265358979323846;
constexpr double Kh = 2.0, MUT = 784.8;
using torch::indexing::Slice;

// Direct-RHS fixture with domain bounds selected before its base state and
// zero-copy WRF profiles are installed. Packed mass aliases are therefore part
// of the initial fixture geometry, not a post-setup index mutation.
struct Option2RhsFixture {
    std::vector<float> half,c1f,c2f,c1h,c2h;
    TileSDIRK3UnifiedSolver solver;

    Option2RhsFixture(bool packed,float spacing,
                      const std::vector<float>& half_mass,
                      const std::vector<float>& offset_mass)
        : half(nw,0.5f),c1f(nw,1.0f),c2f(nw,0.0f),
          c1h(half_mass),c2h(offset_mass),
          solver(nx,ny,nz,spacing,spacing,{1.0f/spacing},{1.0f/spacing},
                 std::vector<float>(nz,float(nz)),0) {
        const int ide=packed?nx:nx+1,jde=packed?ny:ny+1;
        solver.setWRFIndices(1,ide,1,jde,1,nz,
            1,ide,1,jde,1,nw,-2,nx+4,-2,ny+4,1,nw);
        solver.setBoundaryConditions(true,false,false,false,true,true,
                                     false,false,false,false);

        std::vector<float> pbase(st),tinit(st,0.0f),phbase(sw),mubase(sm,80000.0f);
        for (int j=0;j<ny;++j) {
            for (int k=0;k<nz;++k) for (int i=0;i<nx;++i)
                pbase[(j*nz+k)*nx+i]=100000.0f-(k+0.5f)*80000.0f/nz;
            for (int k=0;k<nw;++k) for (int i=0;i<nx;++i)
                phbase[(j*nw+k)*nx+i]=9.81f*5000.0f*k;
        }
        solver.setBaseState(pbase.data(),tinit.data(),phbase.data(),mubase.data());
        (solver.*access(CoordinateTag{}))(
            c1f.data(),c2f.data(),c1h.data(),c2h.data());
        solver.setVerticalInterpolationCoefficients(
            half.data(),half.data(),2.0f,-1.5f,0.5f);

        const auto opt=torch::TensorOptions().dtype(torch::kFloat32);
        solver.*access(MapTxTag{})=torch::ones({ny,nx},opt);
        solver.*access(MapTyTag{})=torch::ones({ny,nx},opt);
        solver.*access(MapUxTag{})=torch::ones({ny,nx+1},opt);
        solver.*access(MapUyTag{})=torch::ones({ny,nx+1},opt);
        solver.*access(MapVxTag{})=torch::ones({ny+1,nx},opt);
        solver.*access(MapVyTag{})=torch::ones({ny+1,nx},opt);
    }
};

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

// Fortran module_diffusion_em.F cal_deform_and_div forms U shear as
// du/dz * 0.5*(rdz(i,k,j)+rdz(i-1,k,j)) (around lines 837-845). Packed
// periodic X has N unique mass columns plus an alias at column N. U faces are
// [west seam, interior faces, east seam, face-1 alias].
bool run_packed_u_rdz_seam() {
    using wrf::sdirk3::test::TileCase;
    constexpr int unique_n=nx-1;
    wrf::sdirk3::g_sdirk3_config=wrf::sdirk3::SDIRK3Config{};
    const auto opt=torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU);
    TileCase tile(1.0f);
    tile.solver.setBoundaryConditions(true,false,false,false,true,true,
                                      false,false,false,false);
    tile.solver.setWRFIndices(1,nx,1,ny,1,nz, 1,nx,1,ny,1,nz+1,
                              -2,nx+4,-2,ny+4,1,nz+1);

    auto u=torch::zeros({ny,nz,nx+1},opt);
    u.select(1,1).fill_(1.0f); // isolated vertical shear at W level k=1
    const auto w=torch::zeros({ny,nw,nx},opt);
    auto rdz=torch::empty({ny,nw,nx},opt);
    const float values[nx]={1,2,3,4,5,6,7,1};
    for (int j=0;j<ny;++j) for (int k=0;k<nw;++k) for (int i=0;i<nx;++i)
        rdz[j][k][i]=values[i];
    const auto zx=torch::zeros({ny,nw,nx+1},opt);
    const auto zy=torch::zeros({ny+1,nw,nx},opt);
    const auto rdzw=torch::ones({ny,nz,nx},opt);
    const auto rdnw=torch::ones({nz},opt);
    const auto actual=(tile.solver.*access(Defor13StageGeometryTag{}))(
        u,w,torch::tensor({1.0f},opt),rdnw,zx,zy,rdzw,rdz).contiguous();
    const auto a=actual.accessor<float,3>();

    auto expected_face=[&](int face) {
        if (face==nx) return 0.5f*(values[0]+values[1]); // packed terminal aliases face 1
        const int left=face==0 ? unique_n-1 : face-1;
        const int right=face==unique_n ? 0 : face;
        return 0.5f*(values[left]+values[right]);
    };
    double max_error=0.0;
    for (int j=0;j<ny;++j) for (int face=0;face<=nx;++face)
        max_error=std::max(max_error,std::abs(double(a[j][1][face]-expected_face(face))));
    double west_alias=0.0, terminal_alias=0.0;
    for (int j=0;j<ny;++j) {
        west_alias=std::max(west_alias,std::abs(double(a[j][1][unique_n]-a[j][1][0])));
        terminal_alias=std::max(terminal_alias,std::abs(double(a[j][1][nx]-a[j][1][1])));
    }
    const double west_expected=expected_face(0), face1_expected=expected_face(1);
    const double tolerance=2.0e-6;
    const bool pass=max_error<=tolerance && west_alias<=tolerance &&
                    terminal_alias<=tolerance && west_expected==4.0 && face1_expected==1.5;
    std::cout << (pass?"PASS ":"FAIL ") << "packed periodic U rdz seam"
              << " west=" << a[1][1][0] << " expected_west=" << west_expected
              << " face1=" << a[1][1][1] << " expected_face1=" << face1_expected
              << " east_alias=" << a[1][1][nx] << " expected_east_alias=" << face1_expected
              << " max_error=" << max_error << " west_alias_error=" << west_alias
              << " terminal_alias_error=" << terminal_alias
              << " tolerance=" << tolerance << '\n';
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
            const auto empty=torch::Tensor();
            const auto actual = u_mode
                ? call_option2_momentum_helper(access(UOption2HelperTag{}),tile.solver,
                    u,v,w,viscosity,1,1,umap,umap,mmap,mu_u,phi,rho,empty,empty,empty,empty)
                : call_option2_momentum_helper(access(VOption2HelperTag{}),tile.solver,
                    u,v,w,viscosity,1,1,vmap,vmap,mmap,mu_v,phi,rho,empty,empty,empty,empty);
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

bool run_option2_scalar_rhs_layer_mass_contract() {
    using namespace wrf::sdirk3;
    using namespace wrf::sdirk3::test;
    auto& cfg=g_sdirk3_config;
    constexpr double gravity=9.81;
    constexpr float dx=1000.0f;
    const auto opt=torch::TensorOptions().dtype(torch::kFloat32);
    const std::vector<float> c1f(nz+1,1.0f),c2f(nz+1,0.0f);
    const std::vector<float> c1h{0.75f,1.125f,0.875f,1.25f};
    const std::vector<float> c2h{0.0f,2000.0f,4000.0f,6000.0f};
    const int64_t theta_begin=su+sv+2*sw;
    const auto pack_mass=[](const torch::Tensor& q,int64_t m) {
        const auto x=torch::cat({q,q.slice(2,0,1)},2);
        return torch::cat({x,x.slice(0,m-1,m)},0);
    };
    const auto make_state=[&](bool packed) {
        const int m=packed?ny-1:ny,n=packed?nx-1:nx;
        auto theta=torch::empty({ny,nz,nx},opt);
        auto mu=torch::empty({ny,nx},opt);
        auto ph=torch::empty({ny,nw,nx},opt);
        auto ph_values=ph.accessor<float,3>();
        auto theta_values=theta.accessor<float,3>();
        auto mu_values=mu.accessor<float,2>();
        for (int j=0;j<m;++j) for (int k=0;k<nz;++k) for (int i=0;i<n;++i) {
            const double x=2*pi*i/n,y=pi*(j+0.5)/m;
            theta_values[j][k][i]=4.0*std::sin(x)+2.5*std::cos(y)+
                1.2*(k+1)*(0.5*std::sin(x)+0.3*std::cos(y));
        }
        for (int j=0;j<m;++j) for (int i=0;i<n;++i) {
            const double x=2*pi*i/n,y=pi*(j+0.5)/m;
            mu_values[j][i]=128.0*i+64.0*j+16.0*std::sin(x);
            const float height=600.0f*std::sin(x)+400.0f*std::cos(y);
            for (int k=0;k<nw;++k)
                ph_values[j][k][i]=static_cast<float>(gravity)*height;
        }
        if (packed) {
            theta=pack_mass(theta.slice(0,0,m).slice(2,0,n),m);
            mu=pack_mass(mu.slice(0,0,m).slice(1,0,n).unsqueeze(1),m).squeeze(1);
            ph=pack_mass(ph.slice(0,0,m).slice(2,0,n),m);
        }
        const auto u=torch::zeros({ny,nz,nx+1},opt);
        const auto v=torch::zeros({ny+1,nz,nx},opt);
        const auto w=torch::zeros({ny,nw,nx},opt);
        return torch::cat({u.reshape({-1}),v.reshape({-1}),w.reshape({-1}),
                           ph.reshape({-1}),theta.reshape({-1}),mu.reshape({-1})});
    };
    // Directions are expressed in their state units: 10 K theta scale,
    // 1000 m geopotential-height scale (g*dz), and 25 kPa column-mass scale.
    // The smaller central-FD step therefore perturbs theta by 0.1 K, PH by
    // O(10 m) and MU by 250 Pa while resolving each block's active response.
    const auto make_direction=[&](bool packed,int block) {
        const int m=packed?ny-1:ny,n=packed?nx-1:nx;
        auto direction=torch::zeros({total},opt);
        if (block==0) {
            auto core=torch::empty({m,nz,n},opt);
            auto a=core.accessor<float,3>();
            for(int j=0;j<m;++j)for(int k=0;k<nz;++k)for(int i=0;i<n;++i){
                const double x=2*pi*i/n,y=pi*(j+0.5)/m;
                a[j][k][i]=10.0*(std::sin(x)+0.4*std::cos(y)+0.2*k/nz);
            }
            auto full=packed?pack_mass(core,m):core;
            direction.slice(0,theta_begin,theta_begin+st).copy_(full.reshape({-1}));
        } else if (block==1) {
            auto core=torch::empty({m,nw,n},opt);
            auto a=core.accessor<float,3>();
            for(int j=0;j<m;++j)for(int k=0;k<nw;++k)for(int i=0;i<n;++i){
                const double x=2*pi*i/n,y=pi*(j+0.5)/m;
                const double eta=double(k)/(nw-1);
                const double height=1000.0*eta*(std::sin(x)+0.5*std::cos(y))+
                                    300.0*eta*std::sin(x+y);
                a[j][k][i]=static_cast<float>(gravity*height);
            }
            auto full=packed?pack_mass(core,m):core;
            const int64_t begin=su+sv+sw;
            direction.slice(0,begin,begin+sw).copy_(full.reshape({-1}));
        } else if (block==2) {
            auto core=torch::empty({m,1,n},opt);
            auto a=core.accessor<float,3>();
            for(int j=0;j<m;++j)for(int i=0;i<n;++i){
                const double x=2*pi*i/n,y=pi*(j+0.5)/m;
                a[j][0][i]=25000.0*(0.5+std::sin(x)*std::cos(y));
            }
            auto full=packed?pack_mass(core,m).squeeze(1):core.squeeze(1);
            direction.slice(0,total-sm,total).copy_(full.reshape({-1}));
        } else {
            TORCH_CHECK(false,"unknown Option 2 VJP state block");
        }
        return direction;
    };
    struct Result {
        torch::Tensor rhs,expected,legacy,geometry;
        double terrain_signal=0.0;
    };
    const auto evaluate=[&](bool on,bool packed,int km_opt=1,
                            const torch::Tensor& state_override=torch::Tensor(),
                            bool make_reference=true,
                            bool partial_owned=false) -> Result {
        cfg=SDIRK3Config{};
        cfg.diffusion_option=2;
        cfg.khdif=on?1000.0f:0.0f;
        cfg.kvdif=0.0f;
        cfg.mass_coordinate_mode=1;
        cfg.wrf_omega_ww_cp=false;
        cfg.mu_horizontal_div_only=false;
        cfg.wrf_damp_opt=0;
        cfg.imex_split_mode=3;
        cfg.hevi_split=false;
        cfg.use_stress_tensor=false;
        Option2RhsFixture fixture(packed,dx,c1h,c2h);
        auto& solver=fixture.solver;
        if (partial_owned) {
            TORCH_CHECK(!packed,"partial ownership fixture must use physical layout");
            solver.setWRFIndices(2,nx+1,1,ny+1,1,nz,
                1,nx+1,1,ny+1,1,nw,-2,nx+4,-2,ny+4,1,nw);
        }
        TORCH_CHECK(solver.*access(FnmFromWrfTag{}),
                    "RHS fixture must use WRF-provided fnm/fnp");
        auto grid=std::static_pointer_cast<WRFGridInfoExtended>(solver.getGridInfo());
        TORCH_CHECK(grid,"option-2 RHS fixture has no extended grid info");
        if (grid->qv.defined() && grid->qv.numel()>0)
            grid->qv=torch::zeros_like(grid->qv);
        // This is WRF km_opt=1. The Step 9 fast path must match the explicit
        // option-2 metric helper whenever all its declared gates are active.
        grid->smagorinsky_opt=km_opt;
        // Production resolves this once at unifiedStep entry, before stage RHS
        // evaluations. Seed the same runtime contract for this direct RHS test.
        (solver.*access(WdampContractTag{}))=resolve_wdamp_runtime_contract(
            true,1,1,true,true,false,false,false,false,false,true,true,
            false,false,false,false,false,"calc_ww_cp Omega (test fixture)");

        const int m=packed?ny-1:ny,n=packed?nx-1:nx;
        const auto state=state_override.defined()?state_override:make_state(packed);
        auto rhs=(solver.*access(ActualRhsTag{}))(
            state,RhsMode::ExplicitOnly).clone();

        // The first checks establish the supported dispatch contract. In
        // particular, nonzero raw/L parity below fails if Step 9 silently uses
        // its legacy fallback instead of the bounded km_opt=1 path.
        TORCH_CHECK(cfg.effective_wrf_omega_ww_cp() && cfg.wrf_damp_opt==0 &&
                    grid->smagorinsky_opt==1 &&
                    solver.*access(FnmFromWrfTag{}) &&
                    (solver.*access(WdampContractTag{})).active &&
                    (solver.*access(WdampContractTag{})).x_policy==WWCPBoundaryPolicy::Periodic &&
                    (solver.*access(WdampContractTag{})).y_policy==WWCPBoundaryPolicy::SymmetricReplicate &&
                    solver.getNumMoistSpecies()==0 &&
                    !(solver.*access(KhMomOverrideTag{})).defined() &&
                    !(solver.*access(KhScalarOverrideTag{})).defined() &&
                    !(solver.*access(CaptureThetaTag{})),
                    "option-2 RHS fast-path prerequisites not active");
        const auto state_phi=state.slice(0,su+sv+sw,su+sv+2*sw).view({ny,nw,nx});
        const auto state_t=state.slice(0,theta_begin,theta_begin+st).view({ny,nz,nx});
        const auto state_mu=state.slice(0,total-sm,total).view({ny,nx});
        TORCH_CHECK(grid->p_base.defined(),"missing GridInfo p_base");
        TORCH_CHECK(grid->th_base.defined(),"missing GridInfo th_base");
        TORCH_CHECK(grid->ph_base.defined(),"missing GridInfo ph_base");
        TORCH_CHECK(grid->mu_base.defined(),"missing GridInfo mu_base");
        TORCH_CHECK(grid->c1h.defined(),"missing GridInfo c1h");
        TORCH_CHECK(grid->c2h.defined(),"missing GridInfo c2h");
        const auto core3=[&](const torch::Tensor& q) {
            return packed?q.slice(0,0,m).slice(2,0,n):q;
        };
        const auto core2=[&](const torch::Tensor& q) {
            return packed?q.slice(0,0,m).slice(1,0,n):q;
        };
        auto phi_core=core3(state_phi);
        auto phbase=core3(grid->ph_base.to(opt));
        auto rdnw=(solver.*access(RdnwTag{}))(torch::kCPU,torch::kFloat32,nz);
        auto rdn=(solver.*access(RdnTag{}))(torch::kCPU,torch::kFloat32,nz);
        auto z_w=(phi_core+phbase)/gravity;
        const auto rdzw=(z_w.slice(1,1,nz+1)-z_w.slice(1,0,nz)).reciprocal();
        const auto x_left=torch::cat({z_w.slice(2,n-1,n),z_w},2);
        const auto x_right=torch::cat({z_w,z_w.slice(2,0,1)},2);
        const auto zx=0.001f*(x_right-x_left);
        const auto zy_inner=0.001f*(z_w.slice(0,1,m)-z_w.slice(0,0,m-1));
        const auto zy=torch::cat({torch::zeros({1,nw,n},opt),zy_inner,
                                  torch::zeros({1,nw,n},opt)},0);
        const double terrain_signal=std::max(zx.abs().max().item<double>(),
                                             zy.abs().max().item<double>());
        const auto geometry=torch::cat({rdzw.reshape({-1}),zx.reshape({-1}),
                                         zy.reshape({-1})}).detach().clone();
        if (!on || !make_reference)
            return {rhs,torch::Tensor(),torch::Tensor(),geometry,terrain_signal};
        auto q=core3(state_t);
        auto mu_core=core2(state_mu);
        auto pbase=core3(grid->p_base.to(opt));
        auto thbase=core3(grid->th_base.to(opt));
        auto mubase=core2(grid->mu_base.to(opt));
        auto c1=torch::tensor(c1h,opt),c2=torch::tensor(c2h,opt);
        const auto alb=compute_inverse_density(thbase,pbase,287.0f,717.5f,
                                                1004.5f,100000.0f);
        const auto prho=calc_p_rho_wrf(phi_core,q,mu_core,mubase,alb,pbase,
            rdnw,c1,c2,287.0f,717.5f,1004.5f,100000.0f,300.0f);
        const auto rho=prho.alt.reciprocal();
        const auto dnw=-rdnw.reciprocal();
        const auto dn=torch::cat({torch::zeros({1},opt),-rdn.slice(0,1,nz).reciprocal()},0);
        // These are the same WRF pointer arrays passed to the fixture setter;
        // this fixture's legacy GridInfo does not publish fnm/fnp.
        const auto fnm=torch::tensor(fixture.half,opt).slice(0,0,nz);
        const auto fnp=torch::tensor(fixture.half,opt).slice(0,0,nz);
        const double dw1=1.0/rdnw[0].item<double>(),d2=1.0/rdn[1].item<double>(),
                     d3=1.0/rdn[2].item<double>();
        const double cof1=(2*d2+d3)/(d2+d3)*dw1/d2;
        const double cof2=d2/(d2+d3)*dw1/d3;
        const double cf1=fnp[1].item<double>()+cof1;
        const double cf2=fnm[1].item<double>()-cof1-cof2;
        const double cf3=cof2;
        const auto kh= torch::full_like(q,3.0f*cfg.khdif);
        const auto msftx=torch::ones({m,n},opt),msfty=torch::ones({m,n},opt);
        const auto msfux=torch::ones({m,n+1},opt),msfvy=torch::ones({m+1,n},opt);
        const auto raw=(solver.*access(Option2ScalarTag{}))(
            q,kh,rho,zx,zy,rdzw,dnw,dn,fnm,fnp,cf1,cf2,cf3,
            1.0f/dx,1.0f/dx,gravity,msftx,msfty,msfux,msfvy);
        const auto layer=c1.view({1,nz,1})*(mu_core+mubase).unsqueeze(1)+
                         c2.view({1,nz,1});
        auto expected=raw/layer;
        const auto legacy_raw=(solver.*access(ScalarDiffusionTag{}))(
            q,kh,1.0f/dx,1.0f/dx,msftx,msfty,msfvy,mu_core+mubase,
            nullptr,nullptr,torch::Tensor(),torch::Tensor(),torch::Tensor());
        auto legacy_expected=legacy_raw/layer;
        if (packed) {
            const auto x=torch::cat({expected,expected.slice(2,0,1)},2);
            expected=torch::cat({x,x.slice(0,m-1,m)},0);
            const auto legacy_x=torch::cat({legacy_expected,legacy_expected.slice(2,0,1)},2);
            legacy_expected=torch::cat({legacy_x,legacy_x.slice(0,m-1,m)},0);
        }
        return {rhs,expected.detach().clone(),legacy_expected.detach().clone(),
                geometry,terrain_signal};
    };

    bool ok=true;
    const double eps=std::numeric_limits<float>::epsilon();
    for (bool packed : {false,true}) {
        const auto on=evaluate(true,packed,1);
        const auto off=evaluate(false,packed,1);
        const auto observed=(on.rhs-off.rhs).slice(0,theta_begin,theta_begin+st)
            .view({ny,nz,nx});
        const auto expected=on.expected;
        const double signal=expected.abs().max().item<double>();
        const double error=(observed-expected).abs().max().item<double>();
        const double legacy_error=(observed-on.legacy).abs().max().item<double>();
        const double legacy_separation=(expected-on.legacy).abs().max().item<double>();
        const double mass_delta=(on.rhs.slice(0,total-sm,total)-
                                 off.rhs.slice(0,total-sm,total)).abs().max().item<double>();
        const double terrain_signal=on.terrain_signal;
        const double tolerance=128.0*eps*std::max(1.0,signal);
        double alias_error=0.0;
        if (packed) {
            alias_error=std::max((observed.select(2,nx-1)-observed.select(2,0))
                                     .abs().max().item<double>(),
                (observed.select(0,ny-1)-observed.select(0,ny-2))
                                     .abs().max().item<double>());
        }
        const double mass_tolerance=32.0*eps;
        const bool pass=torch::isfinite(on.rhs).all().item<bool>() &&
            torch::isfinite(off.rhs).all().item<bool>() &&
            torch::isfinite(expected).all().item<bool>() &&
            signal>100.0*tolerance && error<=tolerance &&
            legacy_error>10.0*tolerance && legacy_separation>10.0*tolerance &&
            mass_delta<=mass_tolerance && terrain_signal>100.0*eps &&
            alias_error<=tolerance;
        std::cout << (pass?"PASS ":"FAIL ")
                  << "option-2 km_opt1 actual RHS theta ON-OFF "
                  << (packed?"packed":"physical") << " signal=" << signal
                  << " error=" << error << " legacy_error=" << legacy_error
                  << " legacy_separation=" << legacy_separation
                  << " mass_delta=" << mass_delta
                  << " terrain_signal=" << terrain_signal
                  << " alias_error=" << alias_error
                  << " mass_tol=" << mass_tolerance
                  << " tol=" << tolerance << '\n';
        ok=pass&&ok;
    }
    bool kmopt2_guard=false;
    try {
        (void)evaluate(true,false,2);
    } catch (const c10::Error& error) {
        kmopt2_guard=std::string(error.what()).find(
            "option-2 metric diffusion requires dry isotropic km_opt=1")!=std::string::npos;
    }
    std::cout << (kmopt2_guard?"PASS ":"FAIL ")
              << "option-2 canonical RHS rejects unsupported km_opt=2" << '\n';
    ok=kmopt2_guard&&ok;

    bool partial_tile_guard=false;
    try {
        (void)evaluate(true,false,1,make_state(false),false,true);
    } catch (const c10::Error& error) {
        partial_tile_guard=std::string(error.what()).find(
            "complete single-rank tile ownership")!=std::string::npos;
    }
    std::cout << (partial_tile_guard?"PASS ":"FAIL ")
              << "option-2 canonical RHS rejects partial tile ownership" << '\n';
    ok=partial_tile_guard&&ok;

    constexpr double h_large=2.0e-2,h_small=1.0e-2;
    constexpr double central_rel_budget=5.0e-2;
    constexpr double richardson_rel_budget=2.0e-2;
    for (bool packed : {false,true}) {
        const int m=packed?ny-1:ny,n=packed?nx-1:nx;
        const auto base=make_state(packed);
        auto seed=torch::empty({m,nz,n},opt);
        auto seed_a=seed.accessor<float,3>();
        for(int j=0;j<m;++j)for(int k=0;k<nz;++k)for(int i=0;i<n;++i){
            const double x=2*pi*i/n,y=pi*(j+0.5)/m;
            seed_a[j][k][i]=0.5+0.2*std::cos(x)+0.15*std::cos(y)+0.05*k;
        }
        seed=seed/torch::sqrt(seed.square().mean());
        const auto theta_core=[&](const torch::Tensor& rhs) {
            auto block=rhs.slice(0,theta_begin,theta_begin+st).view({ny,nz,nx});
            return packed?block.slice(0,0,m).slice(2,0,n):block;
        };
        const auto objective=[&](const torch::Tensor& state) {
            const auto on=evaluate(true,packed,1,state,false);
            const auto off=evaluate(false,packed,1,state,false);
            const auto h=theta_core(on.rhs-off.rhs);
            return std::pair<double,double>{(h*seed).mean().item<double>(),
                                            h.abs().max().item<double>()};
        };

        for (int block=0;block<3;++block) {
            auto direction=make_direction(packed,block);
            auto leaf=base.detach().clone().requires_grad_(true);
            const auto on=evaluate(true,packed,1,leaf,false);
            const auto off=evaluate(false,packed,1,leaf,false);
            const auto h0=theta_core(on.rhs-off.rhs);
            const auto scalar=(h0*seed).mean();
            const auto gradient=torch::autograd::grad({scalar},{leaf})[0];
            const double ad=(gradient*direction).sum().item<double>();
            const double h_signal=h0.abs().max().item<double>();

            const auto central=[&](double step) {
                const auto plus=objective(base+step*direction);
                const auto minus=objective(base-step*direction);
                return std::tuple<double,double,double>{
                    (plus.first-minus.first)/(2.0*step),
                    std::max(plus.second,minus.second),plus.first-minus.first};
            };
            const auto [fd_large,large_signal,large_delta]=central(h_large);
            const auto [fd_small,small_signal,small_delta]=central(h_small);
            (void)large_delta;(void)small_delta;
            const double richardson=(4.0*fd_small-fd_large)/3.0;
            const double signal=std::max(std::abs(ad),std::abs(richardson));
            // Difference-of-differences loses float32 bits as h shrinks. Bound
            // that floor from the measured ON−OFF tendency scale and fixed h.
            const double roundoff_floor=32.0*eps*
                std::max(h_signal,std::max(large_signal,small_signal))/h_small;
            const double central_budget=roundoff_floor+central_rel_budget*signal;
            const double richardson_budget=roundoff_floor+richardson_rel_budget*signal;

            double rdzw_response=0.0,zx_response=0.0,zy_response=0.0;
            if (block==1) {
                const auto plus=evaluate(true,packed,1,base+h_small*direction,false);
                const auto minus=evaluate(true,packed,1,base-h_small*direction,false);
                const auto geom_delta=(plus.geometry-minus.geometry).abs();
                const int64_t rdzw_size=m*nz*n,zx_size=m*nw*(n+1);
                rdzw_response=geom_delta.slice(0,0,rdzw_size).max().item<double>()/
                              (2.0*h_small);
                zx_response=geom_delta.slice(0,rdzw_size,rdzw_size+zx_size).max().item<double>()/
                            (2.0*h_small);
                zy_response=geom_delta.slice(0,rdzw_size+zx_size).max().item<double>()/
                            (2.0*h_small);
            }
            const bool geometry_active=block!=1 ||
                (rdzw_response>1.0e-6 && zx_response>1.0e-4 && zy_response>1.0e-4);
            const bool pass=std::isfinite(ad) && std::isfinite(richardson) &&
                signal>10.0*roundoff_floor &&
                std::abs(ad-fd_large)<=central_budget &&
                std::abs(ad-fd_small)<=central_budget &&
                std::abs(ad-richardson)<=richardson_budget &&
                geometry_active;
            const char* name=block==0?"theta":block==1?"PH":"MU";
            std::cout << (pass?"PASS ":"FAIL ")
                      << "option-2 ON-OFF RHS directional FD/VJP " << name
                      << " " << (packed?"packed":"physical")
                      << " vjp=" << ad
                      << " fd(" << h_large << ")=" << fd_large
                      << " fd(" << h_small << ")=" << fd_small
                      << " fd_richardson=" << richardson
                      << " err_large=" << std::abs(ad-fd_large)
                      << " err_small=" << std::abs(ad-fd_small)
                      << " error=" << std::abs(ad-richardson)
                      << " roundoff_floor=" << roundoff_floor
                      << " central_budget=" << central_budget
                      << " richardson_budget=" << richardson_budget
                      << " d_rdzw=" << rdzw_response << " d_zx=" << zx_response
                      << " d_zy=" << zy_response << '\n';
            ok=pass&&ok;
        }
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
        const auto empty=torch::Tensor();
        return call_option2_momentum_helper(access(UOption2HelperTag{}),tile,
            u,v,w,viscosity,1,1,umap,umap,mmap,mass,torch::Tensor(),rho,
            empty,empty,empty,empty);
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
            const auto empty=torch::Tensor();
            return call_option2_momentum_helper(access(UOption2HelperTag{}),tile.solver,
                u,v,w,viscosity,1,1,umap,umap,mmap,mass,phi,rho,
                empty,empty,empty,empty).clone();
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
    const auto evaluate = [](int option, bool u_mode, bool on, RhsMode mode) {
        auto& cfg = g_sdirk3_config;
        cfg = SDIRK3Config{};
        cfg.diffusion_option = option;
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
    for (int option : {1,2}) for (bool u_mode : {true,false}) {
        const auto full = evaluate(option,u_mode,true,RhsMode::Full) -
                          evaluate(option,u_mode,false,RhsMode::Full);
        const auto explicit_delta = evaluate(option,u_mode,true,RhsMode::ExplicitOnly) -
                                    evaluate(option,u_mode,false,RhsMode::ExplicitOnly);
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
                error = std::max(error,std::abs(value-(option==1?1:2)*viscosity*lambda*wave));
                work += wave*value;
            }
        const double split_error = (actual-select(explicit_delta)).abs().max().item<double>();
        // Absolute FP32 engineering budget for this prescribed velocity RHS fixture.
        const double tolerance = 1.0e-6;
        const bool pass = torch::isfinite(full).all().item<bool>() &&
            torch::isfinite(explicit_delta).all().item<bool>() &&
            error <= tolerance && split_error <= tolerance && work < 0.0;
        std::cout << (pass ? "PASS " : "FAIL ") << "normal RHS option=" << option << ' '
            << (u_mode ? "U-X" : "V-Y") << " error=" << error
            << " full_explicit=" << split_error << " tol=" << tolerance
            << " work=" << work << '\n';
        ok = pass && ok;
    }
    return ok;
}

// Option-1 Fortran returns a layer-mass coupled tendency. rk_addtend_dry
// removes the component map factor, while this solver accumulates in column
// mass and converts back by velocity_mass. Since alpha=L/map, the integrated
// primitive RHS increment must be raw/L (maps are already present in raw).
// This fixture checks that conversion in the actual Full and Explicit RHS,
// with hybrid coefficients, spatially varying MU, and non-unit maps.
bool run_option1_momentum_rhs_basis_contract() {
    using namespace wrf::sdirk3;
    using namespace wrf::sdirk3::test;
    constexpr float viscosity = 1000.0f;
    const std::vector<float> c1f_values{0.30f,0.45f,0.65f,0.82f,0.90f};
    const std::vector<float> c2f_values{5000.0f,8000.0f,12000.0f,15000.0f,18000.0f};
    const std::vector<float> c1h_values{0.25f,0.40f,0.60f,0.80f};
    const std::vector<float> c2h_values{18000.0f,15000.0f,12000.0f,9000.0f};

    const auto configure = [&] {
        auto& cfg = g_sdirk3_config;
        cfg = SDIRK3Config{};
        cfg.diffusion_option = 1;
        cfg.khdif = viscosity;
        cfg.kvdif = 0.0f;
        cfg.mass_coordinate_mode = 0;
        cfg.imex_split_mode = 3;
        cfg.hevi_split = false;
        cfg.wrf_omega_ww_cp = false;
        cfg.use_stress_tensor = false;
    };
    auto prepare = [&](TileCase& tile) {
        configure();
        (tile.solver.*access(CoordinateTag{}))(
            c1f_values.data(),c2f_values.data(),
            c1h_values.data(),c2h_values.data());

        for (int j=0; j<ny; ++j) {
            for (int i=0; i<nx; ++i) {
                const double phase_x=2*pi*i/nx;
                const double phase_y=2*pi*j/ny;
                tile.mu[j*nx+i] = 100.0f + 18.0f*std::sin(phase_x)
                                  + 11.0f*std::cos(phase_y);
                for (int k=0; k<nz; ++k) {
                    tile.u[(j*nz+k)*(nx+1)+i] =
                        std::sin(phase_x)*(1.0f+0.03f*j+0.02f*k);
                    tile.v[(j*nz+k)*nx+i] = j==0 || j==ny-1 ? 0.0f :
                        std::sin(pi*j/(ny-1))*(1.0f+0.02f*i+0.025f*k);
                }
                for (int k=0; k<nw; ++k) {
                    tile.w[(j*nw+k)*nx+i] = std::sin(phase_x)*
                        (1.0f+0.025f*j+0.03f*k);
                }
            }
            // The east U face aliases the west face on periodic X.
            for (int k=0; k<nz; ++k)
                tile.u[(j*nz+k)*(nx+1)+nx] = tile.u[(j*nz+k)*(nx+1)];
        }

        std::vector<float> mtx(ny*nx),mty(ny*nx);
        std::vector<float> mux(ny*(nx+1)),muy(ny*(nx+1));
        std::vector<float> mvx((ny+1)*nx),mvy((ny+1)*nx);
        for (int j=0; j<ny; ++j) for (int i=0; i<nx; ++i) {
            mtx[j*nx+i]=1.0f+0.015f*i+0.006f*j;
            mty[j*nx+i]=1.0f+0.004f*i+0.012f*j;
        }
        for (int j=0; j<ny; ++j) for (int f=0; f<=nx; ++f) {
            const int i=f%nx;
            mux[j*(nx+1)+f]=1.0f+0.018f*i+0.005f*j;
            muy[j*(nx+1)+f]=1.0f+0.007f*i+0.009f*j;
        }
        for (int f=0; f<=ny; ++f) for (int i=0; i<nx; ++i) {
            mvx[f*nx+i]=1.0f+0.011f*i+0.004f*f;
            mvy[f*nx+i]=1.0f+0.006f*i+0.010f*f;
        }
        const auto opt=torch::TensorOptions().dtype(torch::kFloat32);
        (tile.solver.*access(MapTxTag{}))=torch::tensor(mtx,opt).view({ny,nx});
        (tile.solver.*access(MapTyTag{}))=torch::tensor(mty,opt).view({ny,nx});
        (tile.solver.*access(MapUxTag{}))=torch::tensor(mux,opt).view({ny,nx+1});
        (tile.solver.*access(MapUyTag{}))=torch::tensor(muy,opt).view({ny,nx+1});
        (tile.solver.*access(MapVxTag{}))=torch::tensor(mvx,opt).view({ny+1,nx});
        (tile.solver.*access(MapVyTag{}))=torch::tensor(mvy,opt).view({ny+1,nx});
        return tile.state();
    };

    configure();
    TileCase oracle_tile(100.0f);
    const auto state=prepare(oracle_tile);
    g_sdirk3_config.khdif=viscosity;
    const auto opt=torch::TensorOptions().dtype(torch::kFloat32);
    const auto u=state.slice(0,0,su).view({ny,nz,nx+1});
    const auto v=state.slice(0,su,su+sv).view({ny+1,nz,nx});
    const auto w=state.slice(0,su+sv,su+sv+sw).view({ny,nw,nx});
    const auto mu_pert=state.slice(0,total-sm,total).view({ny,nx});
    // TileCase installs an 80000 Pa MUB; MU is the variable state perturbation.
    const auto mu_full=mu_pert+80000.0f;
    const auto kh=torch::full({ny,nz,nx},viscosity,opt);
    const auto raw=(oracle_tile.solver.*access(Option1MomentumTag{}))(
        u,v,w,kh,mu_full,1.0f/100.0f,1.0f/100.0f);

    const auto c1h=torch::tensor(c1h_values,opt).view({1,nz,1});
    const auto c2h=torch::tensor(c2h_values,opt).view({1,nz,1});
    const auto c1f_t=torch::tensor(c1f_values,opt).view({1,nw,1});
    const auto c2f_t=torch::tensor(c2f_values,opt).view({1,nw,1});
    const auto l_mass=c1h*mu_full.unsqueeze(1)+c2h;
    const auto l_u_core=0.5f*(l_mass.slice(2,0,nx-1)+l_mass.slice(2,1,nx));
    const auto l_u_seam=0.5f*(l_mass.slice(2,nx-1,nx)+l_mass.slice(2,0,1));
    const auto l_u=torch::cat({l_u_seam,l_u_core,l_u_seam},2);
    const auto l_v_inner=0.5f*(l_mass.slice(0,0,ny-1)+l_mass.slice(0,1,ny));
    const auto l_v=torch::cat({l_mass.slice(0,0,1),l_v_inner,
                               l_mass.slice(0,ny-1,ny)},0);
    const auto l_w=c1f_t*mu_full.unsqueeze(1)+c2f_t;
    const auto mu_u=torch::cat({
        0.5f*(mu_full.slice(1,nx-1,nx)+mu_full.slice(1,0,1)),
        0.5f*(mu_full.slice(1,0,nx-1)+mu_full.slice(1,1,nx)),
        0.5f*(mu_full.slice(1,nx-1,nx)+mu_full.slice(1,0,1))},1);
    const auto mu_v=torch::cat({mu_full.slice(0,0,1),
        0.5f*(mu_full.slice(0,0,ny-1)+mu_full.slice(0,1,ny)),
        mu_full.slice(0,ny-1,ny)},0);
    const auto map_u_y=oracle_tile.solver.*access(MapUyTag{});
    const auto map_v_x=oracle_tile.solver.*access(MapVxTag{});
    const auto map_w_y=oracle_tile.solver.*access(MapTyTag{});
    const std::array<torch::Tensor,3> expected{
        std::get<0>(raw)/l_u,std::get<1>(raw)/l_v,std::get<2>(raw)/l_w};
    // Negative-basis controls: omitting the column/layer conversion returns
    // raw/M, while applying M/alpha without first dividing the Fortran map
    // factor returns raw*map/L. The non-unit maps and hybrid masses must
    // separate both mistakes from the intended raw/L result.
    const std::array<torch::Tensor,3> unscaled{
        std::get<0>(raw)/mu_u.unsqueeze(1),
        std::get<1>(raw)/mu_v.unsqueeze(1),
        std::get<2>(raw)/mu_full.unsqueeze(1)};
    const std::array<torch::Tensor,3> wrong_map{
        std::get<0>(raw)*map_u_y.unsqueeze(1)/l_u,
        std::get<1>(raw)*map_v_x.unsqueeze(1)/l_v,
        std::get<2>(raw)*map_w_y.unsqueeze(1)/l_w};

    const auto evaluate=[&](const torch::Tensor& q,float khdif,RhsMode mode) {
        configure();
        TileCase tile(100.0f);
        prepare(tile);
        g_sdirk3_config.khdif=khdif;
        return (tile.solver.*access(ActualRhsTag{}))(q,mode);
    };
    constexpr std::array<const char*,3> names{"U","V","W"};
    constexpr std::array<std::pair<int,int>,3> ranges{{
        {0,su},{su,su+sv},{su+sv,su+sv+sw}}};
    bool ok=true;
    const double tolerance=3.0e-4;
    for (auto mode : {RhsMode::Full,RhsMode::ExplicitOnly}) {
        const auto on=evaluate(state,viscosity,mode).detach().clone();
        const auto off=evaluate(state,0.0f,mode).detach().clone();
        const auto delta=on-off;
        for (int component=0; component<3; ++component) {
            const auto actual=delta.slice(0,ranges[component].first,
                                          ranges[component].second)
                .view(expected[component].sizes());
            const auto error=(actual-expected[component]).abs().max().item<double>();
            const auto signal=expected[component].abs().max().item<double>();
            const auto unscaled_gap=(unscaled[component]-expected[component])
                .abs().max().item<double>();
            const auto wrong_map_gap=(wrong_map[component]-expected[component])
                .abs().max().item<double>();
            const bool pass=torch::isfinite(actual).all().item<bool>() &&
                            signal>100.0*tolerance && error<=tolerance &&
                            unscaled_gap>10.0*tolerance &&
                            wrong_map_gap>10.0*tolerance;
            std::cout << (pass?"PASS ":"FAIL ")
                      << "option1 momentum integrated basis mode="
                      << (mode==RhsMode::Full?"full":"explicit")
                      << " component=" << names[component]
                      << " error=" << error << " signal=" << signal
                      << " unscaled_gap=" << unscaled_gap
                      << " wrong_map_gap=" << wrong_map_gap
                      << " tol=" << tolerance << '\n';
            ok=pass&&ok;
        }
    }

    // The active diffusion increment has a modest signal embedded in the full
    // RHS. Check its VJP against a centered FD on fresh, identically prepared
    // solver contexts. Wind directions use O(1) amplitudes; the MU direction
    // uses a 1000 Pa step to keep the L(MU) sensitivity above FP32 roundoff.
    const auto diffusion_increment=[&](const torch::Tensor& q) {
        return evaluate(q,viscosity,RhsMode::Full)-
               evaluate(q,0.0f,RhsMode::Full);
    };
    // Resolve X and Y mass sensitivity separately. A uniform-MU base and
    // direction additionally checks cancellation of a uniform layer mass
    // from both the face flux and the primitive output conversion.
    auto uniform_mass_state=state.detach().clone();
    uniform_mass_state.slice(0,total-sm,total).fill_(100.0f);
    const std::array<const char*,6> input_names{"U","V","W","MU-X","MU-Y","MU-uniform"};
    const std::array<int,6> input_begin{0,su,su+sv,total-sm,total-sm,total-sm};
    const std::array<int,6> input_end{su,su+sv,su+sv+sw,total,total,total};
    const std::array<double,6> fd_steps{0.2,0.2,0.2,1000.0,1000.0,1000.0};
    double mu_axis_signal_min=std::numeric_limits<double>::infinity();
    for (int input=0; input<6; ++input) {
        const auto& base_state=input==5 ? uniform_mass_state : state;
        auto direction=torch::zeros_like(base_state);
        auto block=direction.slice(0,input_begin[input],input_end[input]);
        if (input==0) {
            auto q=block.view({ny,nz,nx+1});
            for (int j=0;j<ny;++j) for (int k=0;k<nz;++k)
                for (int i=0;i<nx;++i)
                    q[j][k][i]=std::sin(2*pi*i/nx)*(1.0+0.03*j+0.02*k);
            q.select(2,nx).copy_(q.select(2,0));
        } else if (input==1) {
            auto q=block.view({ny+1,nz,nx});
            for (int j=0;j<=ny;++j) for (int k=0;k<nz;++k)
                for (int i=0;i<nx;++i)
                    q[j][k][i]=(j==0 || j==ny) ? 0.0 :
                        std::sin(pi*j/ny)*(1.0+0.02*i+0.025*k);
        } else if (input==2) {
            auto q=block.view({ny,nw,nx});
            for (int j=0;j<ny;++j) for (int k=0;k<nw;++k)
                for (int i=0;i<nx;++i)
                    q[j][k][i]=std::sin(2*pi*i/nx)*(1.0+0.025*j+0.03*k);
        } else if (input==3 || input==4) {
            auto q=block.view({ny,nx});
            for (int j=0;j<ny;++j) for (int i=0;i<nx;++i)
                q[j][i]=input==3 ? std::sin(2*pi*i/nx) : std::cos(2*pi*j/ny);
        } else {
            block.fill_(1.0f);
        }
        direction=direction/direction.abs().max();

        auto cotangent=torch::zeros_like(state);
        for (int output=0; output<3; ++output) {
            auto q=cotangent.slice(0,ranges[output].first,ranges[output].second);
            const auto scaled=expected[output]/expected[output].abs().max();
            q.copy_(scaled.reshape({-1}));
        }
        if (input<3) {
            const int begin=ranges[input].first, end=ranges[input].second;
            auto only=cotangent.slice(0,begin,end).clone();
            cotangent.zero_();
            cotangent.slice(0,begin,end).copy_(only);
        }
        cotangent=cotangent/cotangent.norm();

        auto qvar=base_state.detach().clone().requires_grad_(true);
        const auto increment_ad_obj=(diffusion_increment(qvar)*cotangent).sum();
        const auto gradient=torch::autograd::grad({increment_ad_obj},{qvar})[0];
        const double increment_ad=(gradient*direction).sum().item<double>();
        if (input==3 || input==4)
            mu_axis_signal_min=std::min(mu_axis_signal_min,std::abs(increment_ad));

        for (double h : {fd_steps[input],fd_steps[input]*0.5}) {
            const auto plus_on=evaluate(base_state+h*direction,viscosity,RhsMode::Full).detach();
            const auto plus_off=evaluate(base_state+h*direction,0.0f,RhsMode::Full).detach();
            const auto minus_on=evaluate(base_state-h*direction,viscosity,RhsMode::Full).detach();
            const auto minus_off=evaluate(base_state-h*direction,0.0f,RhsMode::Full).detach();
            const auto hp=plus_on-plus_off, hm=minus_on-minus_off;
            const double increment_fd=(cotangent.to(torch::kFloat64)*
                ((hp-hm).to(torch::kFloat64)/(2.0*h))).sum().item<double>();
            const auto weighted_roundoff=(cotangent.square()*(plus_on.square()+
                plus_off.square()+minus_on.square()+minus_off.square())).sum().sqrt();
            const double floor=3.0*std::numeric_limits<float>::epsilon()*
                weighted_roundoff.item<double>()/(2.0*h);
            // A cancelled uniform-MU response must stay below one percent
            // of either resolved spatial MU sensitivity, allowing FP32
            // reduction noise without hiding an order-one mass-basis leak.
            const double budget=input==5 ?
                std::max(5.0*floor,0.01*mu_axis_signal_min) :
                floor+0.08*std::abs(increment_ad);
            const double error=std::abs(increment_fd-increment_ad);
            const bool pass=std::isfinite(increment_ad) && std::isfinite(increment_fd) &&
                (input==5 ? (std::abs(increment_ad)<=budget &&
                             std::abs(increment_fd)<=budget) :
                            std::abs(increment_ad)>3.0*floor) && error<=budget;
            std::cout << (pass?"PASS ":"FAIL ")
                      << "option1 momentum increment FD/VJP input=" << input_names[input]
                      << " h=" << h << " fd=" << increment_fd << " ad=" << increment_ad
                      << " floor=" << floor << " error=" << error
                      << " budget=" << budget << '\n';
            ok=pass&&ok;
        }
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

// Expose the raw private U helper for the source-grounded option-2 geometry
// oracle.  Cached terrain is deliberately flat while the supplied stage PH is
// terrain-following, so this pins whether D11 consumes stage-local zx.
void dump_option2_momentum_geometry() {
    using wrf::sdirk3::test::TileCase;
    auto& cfg=wrf::sdirk3::g_sdirk3_config;
    cfg=wrf::sdirk3::SDIRK3Config{};
    cfg.diffusion_option=2;
    constexpr float dx=1000.0f, gravity=9.81f;
    const auto opt=torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU);
    TileCase tile(dx);
    tile.solver.setTerrainSlopes(torch::zeros({ny,nw,nx},opt),
                                  torch::zeros({ny+1,nw,nx},opt));
    auto u=torch::zeros({ny,nz,nu},opt);
    auto v=torch::zeros({ny+1,nz,nx},opt);
    auto w=torch::zeros({ny,nw,nx},opt);
    auto ph_full=torch::empty({ny,nw,nx},opt);
    auto zx_stage=torch::empty({ny,nw,nx+1},opt);
    auto zy_stage=torch::zeros({ny+1,nw,nx},opt);
    auto rdzw_stage=torch::full({ny,nz,nx},1.0f/1000.0f,opt);
    auto rdz_stage=torch::full({ny,nw,nx},1.0f/1000.0f,opt);
    for (int j=0;j<ny;++j) for (int k=0;k<nw;++k) for (int i=0;i<nx;++i) {
        const float terrain=100.0f*std::cos(float(2.0*pi*i/nx));
        ph_full[j][k][i]=gravity*(terrain+1000.0f*k);
        const int im1=(i+nx-1)%nx;
        const float terrain_left=100.0f*std::cos(float(2.0*pi*im1/nx));
        zx_stage[j][k][i]=(terrain-terrain_left)/dx;
    }
    for (int j=0;j<ny;++j) for (int k=0;k<nw;++k)
        zx_stage[j][k][nx]=zx_stage[j][k][0];
    rdz_stage.select(1,0).fill_(2.0f/1000.0f);
    for (int j=0;j<ny;++j) for (int k=0;k<nz;++k) for (int i=0;i<nu;++i)
        u[j][k][i]=(k==nz-1 ? 1.0f : 0.0f);
    const auto kh=torch::full({ny,nz,nx},2.0f,opt);
    const auto rho=torch::ones({ny,nz,nx},opt);
    const auto map_u=torch::ones({ny,nu},opt);
    const auto map_v=torch::ones({ny+1,nx},opt);
    const auto map_m=torch::ones({ny,nx},opt);
    const auto muu=torch::full({ny,nx},784.8f,opt);
    const auto raw=call_option2_momentum_helper(access(UOption2HelperTag{}),tile.solver,
        u,v,w,kh,1.0f/dx,1.0f/dx,map_u,map_u,map_m,muu,ph_full,rho,
        zx_stage,zy_stage,rdzw_stage,rdz_stage,true).contiguous();
    const auto a=raw.accessor<float,3>();
    for (int j=0;j<ny;++j) for (int k=0;k<nz;++k) for (int i=0;i<nu;++i)
        std::cout << "U_RAW " << j << ' ' << k << ' ' << i << ' '
                  << std::setprecision(9) << a[j][k][i] << '\n';
}

void dump_option2_scalar_fortran_parity(torch::Dtype dtype, int case_id,
                                        const std::string& mutation={}) {
    using wrf::sdirk3::test::TileCase;
    constexpr int nw=nz+1;
    TORCH_CHECK(case_id >= 5 && case_id <= 10,
                "option-2 scalar parity case must be in [5,10]");
    TORCH_CHECK(mutation.empty() ||
                ((case_id==7 || case_id==8) && mutation=="no-slope") ||
                ((case_id==9 || case_id==10) &&
                 (mutation=="avg-product" || mutation=="unit-maps")) ||
                (case_id==10 && mutation=="uniform-depth"),
                "unsupported option-2 scalar parity counterfactual");
    TileCase tile(1000.0f);
    const auto opt=torch::TensorOptions().dtype(dtype).device(torch::kCPU);
    constexpr double gravity64=9.81;
    const double gravity=dtype==torch::kFloat32 ? double(9.81f) : gravity64;
    constexpr double eta_width=0.25;
    const double layer_depth=case_id==5 ? 20.0 : 2000.0;
    constexpr double mu_full=80000.0;
    constexpr double p_span=95000.0;
    constexpr double c1[nz]={0.4,0.8,1.2,1.6};
    std::vector<double> rho_level(nz), w_height(nw);
    for (int k=0;k<nz;++k) {
        const double layer_mass=c1[k]*mu_full+(1.0-c1[k])*p_span;
        rho_level[k]=case_id==5 ? 1.0 : eta_width*layer_mass/(gravity*layer_depth);
    }
    // Stretched physical Z with uniform eta widths, matching the Fortran
    // source-extraction fixture's rdzw while keeping dnw/fnm/fnp unchanged.
    constexpr double z_layer[nz]={300.0,500.0,550.0,650.0};
    for (int k=0;k<nw;++k) {
        w_height[k]=100.0;
        if (case_id==10) {
            // The source helper's mass level 1 is centered between W levels
            // 1 and 2; skip the unused W level 0 when laying out q's heights.
            for (int level=0;level<=k;++level)
                w_height[k]+=z_layer[std::min(level,nz-1)];
        } else {
            w_height[k]=100.0+layer_depth*(k+1);
        }
    }

    auto tensor=[&](const std::vector<double>& values,
                    std::vector<int64_t> shape) {
        return torch::tensor(values,torch::TensorOptions().dtype(torch::kFloat64))
            .to(dtype).reshape(shape);
    };
    const auto ix=torch::arange(nx,opt);
    const auto phase_x=ix*(2.0*pi/nx);
    const auto fourier_x=torch::sin(phase_x);
    const auto j=torch::arange(ny,opt);
    const auto phase_y=(j+0.5)*(pi/ny);
    const auto terrain_y=torch::cos(phase_y);
    const bool terrain=case_id==7 || case_id==8 || case_id==9 || case_id==10;
    const auto h_x=terrain ? 300.0*fourier_x : torch::zeros_like(fourier_x);
    const auto h_cell=h_x.view({1,nx}).expand({ny,nx}).clone()+
        (terrain ? 200.0*terrain_y.view({ny,1}).expand({ny,nx})
                 : torch::zeros({ny,nx},opt));
    const auto h_left=torch::cat({h_cell.slice(1,nx-1,nx),h_cell},1);
    const auto h_right=torch::cat({h_cell,h_cell.slice(1,0,1)},1);
    const auto zx_2d=(h_right-h_left)*0.001f;
    const auto zx=zx_2d.unsqueeze(1).expand({ny,nw,nx+1}).clone();
    const auto h_y_lower=torch::cat({h_cell.slice(0,0,1),h_cell},0);
    const auto h_y_upper=torch::cat({h_cell,h_cell.slice(0,ny-1,ny)},0);
    auto zy_2d=(h_y_upper-h_y_lower)*0.001f;
    zy_2d.slice(0,0,1).zero_();
    zy_2d.slice(0,ny,ny+1).zero_();
    const auto zy=zy_2d.unsqueeze(1).expand({ny+1,nw,nx}).clone();
    const auto zw=tensor(w_height,{1,nw,1})+h_cell.unsqueeze(1);
    const auto z_mass=0.5*(zw.slice(1,0,nz)+zw.slice(1,1,nz+1));

    torch::Tensor q;
    if (case_id==5 || case_id==6) {
        q=(1.0+0.03*fourier_x).view({1,1,nx}).expand({ny,nz,nx}).clone();
    } else {
        q=1.0+1.0e-4*z_mass;
        if (case_id==8 || case_id==9 || case_id==10) {
            const auto mixed_f=(2.0e-4*fourier_x.view({1,nx})+
                1.0e-4*terrain_y.view({ny,1})).view({ny,1,nx});
            q=q+z_mass*mixed_f*(case_id==10 ? 10.0 : 1.0);
        }
    }
    auto kh=torch::full({ny,nz,nx},2.0,opt);
    auto rho=tensor(rho_level,{1,nz,1}).expand({ny,nz,nx}).clone();
    if (case_id==9 || case_id==10) {
        std::vector<double> kh_values,rho_values;
        kh_values.reserve(ny*nz*nx);rho_values.reserve(ny*nz*nx);
        for (int y=0;y<ny;++y) for (int k=0;k<nz;++k) for (int x=0;x<nx;++x) {
            const double px=2.0*pi*x/nx,py=pi*(y+0.5)/ny;
            kh_values.push_back(1.85+0.70*std::sin(px)+0.30*std::cos(py)+0.10*k);
            rho_values.push_back(1.0+0.42*std::cos(px+0.37)+0.25*std::sin(py+0.21)+0.06*k);
        }
        kh=tensor(kh_values,{ny,nz,nx});
        rho=tensor(rho_values,{ny,nz,nx});
    }
    std::vector<double> rdzw_values;
    rdzw_values.reserve(ny*nz*nx);
    for (int y=0;y<ny;++y) for (int k=0;k<nz;++k) for (int x=0;x<nx;++x)
        rdzw_values.push_back(1.0/(case_id==10 ? z_layer[std::min(k+1,nz-1)] : layer_depth));
    auto rdzw=tensor(rdzw_values,{ny,nz,nx});
    const auto dnw=torch::full({nz},-eta_width,opt);
    const auto dn=torch::full({nz},-eta_width,opt);
    const auto fnm=torch::full({nz},0.5,opt);
    const auto fnp=torch::full({nz},0.5,opt);
    auto msftx=torch::ones({ny,nx},opt);
    auto msfty=torch::ones({ny,nx},opt);
    auto msfux=torch::ones({ny,nx+1},opt);
    auto msfvy=torch::ones({ny+1,nx},opt);
    if (case_id==9 || case_id==10) {
        std::vector<double> tx,ty,ux,vy;
        for (int y=0;y<ny;++y) for (int x=0;x<nx;++x) {
            const double px=2.0*pi*x/nx,py=pi*(y+0.5)/ny;
            tx.push_back(1.1+0.16*std::sin(px)+0.07*std::cos(py));
            ty.push_back(0.92+0.10*std::cos(px)+0.12*std::sin(py));
        }
        for (int y=0;y<ny;++y) for (int f=0;f<=nx;++f) {
            const double px=2.0*pi*(f-0.5)/nx,py=pi*(y+0.5)/ny;
            ux.push_back(1.2+0.10*std::cos(px)+0.05*std::cos(py));
        }
        for (int f=0;f<=ny;++f) for (int x=0;x<nx;++x) {
            const double px=2.0*pi*x/nx,py=pi*f/ny;
            vy.push_back(0.82+0.08*std::sin(px)+0.10*std::cos(py));
        }
        msftx=tensor(tx,{ny,nx});msfty=tensor(ty,{ny,nx});
        msfux=tensor(ux,{ny,nx+1});msfvy=tensor(vy,{ny+1,nx});
    }
    if (mutation=="unit-maps") {
        msftx=torch::ones_like(msftx);msfty=torch::ones_like(msfty);
        msfux=torch::ones_like(msfux);msfvy=torch::ones_like(msfvy);
    }
    if (mutation=="avg-product") {
        kh=kh*rho;
        rho=torch::ones_like(rho);
    }
    if (mutation=="uniform-depth")
        rdzw=torch::full_like(rdzw,1.0/(layer_depth/nz));
    const float rdx=case_id==5 ? 0.1f : 0.001f;
    const float rdy=case_id==5 ? 0.13f : 0.001f;
    const auto zx_input=mutation=="no-slope" ? torch::zeros_like(zx) : zx;
    const auto zy_input=mutation=="no-slope" ? torch::zeros_like(zy) : zy;
    const auto out=(tile.solver.*access(Option2ScalarTag{}))(
        q,kh,rho,zx_input,zy_input,rdzw,dnw,dn,fnm,fnp,
        2.0,-1.5,0.5,rdx,rdy,gravity,
        msftx,msfty,msfux,msfvy).to(torch::kFloat64).contiguous();
    const auto a=out.accessor<double,3>();
    for (int j=0;j<ny;++j) for (int k=0;k<nz;++k) for (int i=0;i<nx;++i)
        std::cout << "C_OPT2 " << case_id << ' ' << j+1 << ' ' << k+1 << ' ' << i+1
                  << ' ' << std::setprecision(17) << a[j][k][i] << '\n';
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

void dump_option1_momentum(torch::Dtype dtype,const std::string& component,int mass_case) {
    using wrf::sdirk3::test::TileCase;
    if (mass_case!=1 && mass_case!=2) throw std::invalid_argument("invalid mass case");
    TileCase tile(10.0f);
    const auto opt=torch::TensorOptions().dtype(dtype).device(torch::kCPU);
    const std::vector<float> c1f{.25f,.5f,.75f,1.0f,1.0f};
    const std::vector<float> c2f{3000.f,4000.f,5000.f,6000.f,6000.f};
    const std::vector<float> c1h{.25f,.5f,.75f,1.0f};
    const std::vector<float> c2h{3000.f,4000.f,5000.f,6000.f};
    (tile.solver.*access(CoordinateTag{}))(
        c1f.data(),c2f.data(),c1h.data(),c2h.data());
    const auto ix=torch::arange(nx,opt), iu=torch::arange(nx+1,opt);
    const auto jy=torch::arange(ny+1,opt);
    const auto u=torch::sin(iu*(2.0*pi/nx)).view({1,1,nx+1})
        .expand({ny,nz,nx+1}).clone();
    const auto v=torch::sin(jy*(pi/ny)).view({ny+1,1,1})
        .expand({ny+1,nz,nx}).clone();
    const auto w=torch::sin(ix*(2.0*pi/nx)).view({1,1,nx})
        .expand({ny,nz+1,nx}).clone();
    const auto kh=torch::full({ny,nz,nx},2.0,opt);
    const auto mut=torch::full({ny,nx},mass_case==1?80000.0:120000.0,opt);
    const auto result=(tile.solver.*access(Option1MomentumTag{}))(
        u,v,w,kh,mut,.1f,.07f);
    torch::Tensor selected;
    int jmax=ny,kmin=1;
    if (component=="U") selected=std::get<0>(result);
    else if (component=="V") {selected=std::get<1>(result);jmax=ny+1;}
    else if (component=="W") {selected=std::get<2>(result);kmin=2;}
    else throw std::invalid_argument("invalid momentum component");
    const auto out=selected.to(torch::kFloat64).contiguous();
    const auto a=out.accessor<double,3>();
    for (int j=1;j<=jmax;++j) for (int k=kmin;k<=nz;++k)
        for (int i=1;i<=nx;++i)
            std::cout << "C_MOM " << component << ' ' << j << ' ' << k << ' ' << i
                      << ' ' << std::setprecision(17) << a[j-1][k-1][i-1] << '\n';
}

void dump_option1_momentum_packed(torch::Dtype dtype,const std::string& component) {
    using wrf::sdirk3::test::TileCase;
    constexpr int n=nx-1,m=ny-1;
    TileCase tile(10.0f);
    tile.solver.setWRFIndices(1,nx,1,ny,1,nz, 1,nx,1,ny,1,nz+1,
                              -2,nx+4,-2,ny+4,1,nz+1);
    const auto opt=torch::TensorOptions().dtype(dtype).device(torch::kCPU);
    const std::vector<float> c1f{.25f,.5f,.75f,1.0f,1.0f};
    const std::vector<float> c2f{3000.f,4000.f,5000.f,6000.f,6000.f};
    const std::vector<float> c1h{.25f,.5f,.75f,1.0f};
    const std::vector<float> c2h{3000.f,4000.f,5000.f,6000.f};
    (tile.solver.*access(CoordinateTag{}))(
        c1f.data(),c2f.data(),c1h.data(),c2h.data());
    auto tensor=[&](const std::vector<double>& a,std::vector<int64_t> shape) {
        return torch::tensor(a,torch::TensorOptions().dtype(torch::kFloat64))
            .to(dtype).reshape(shape);
    };
    const auto pack_mass=[m](const torch::Tensor& q) {
        const auto x=torch::cat({q,q.slice(q.dim()-1,0,1)},q.dim()-1);
        return torch::cat({x,x.slice(0,m-1,m)},0);
    };
    const auto pack_u=[m](const torch::Tensor& q) {
        const auto x=torch::cat({q,q.slice(2,0,2)},2);
        return torch::cat({x,x.slice(0,m-1,m)},0);
    };
    const auto pack_v=[m](const torch::Tensor& q,bool odd_ghost) {
        const auto x=torch::cat({q,q.slice(q.dim()-1,0,1)},q.dim()-1);
        const auto ghost=x.slice(0,m-1,m);
        return torch::cat({x,odd_ghost?-ghost:ghost},0);
    };
    std::vector<double> khv,muv,uxv,vyv,wv,txv,tyv,u_xmap,u_ymap,v_xmap,v_ymap;
    for (int j=0;j<m;++j) for (int k=0;k<nz;++k) for (int i=0;i<n;++i)
        khv.push_back(2.0+i/8.0+j/16.0+k/32.0);
    for (int j=0;j<m;++j) for (int i=0;i<n;++i) {
        muv.push_back(80000.0+128.0*i+256.0*j);
        txv.push_back(1.0+i/32.0+j/64.0);
        tyv.push_back(1.0+i/64.0+j/32.0);
    }
    for (int j=0;j<m;++j) for (int k=0;k<nz;++k) for (int i=0;i<n;++i)
        uxv.push_back(std::sin(2*pi*i/n)*(1.0+j/16.0+k/32.0));
    for (int j=0;j<=m;++j) for (int k=0;k<nz;++k) for (int i=0;i<n;++i)
        vyv.push_back((j==0 || j==m ? 0.0 : std::sin(pi*j/m))*
                      (1.0+i/16.0+k/32.0));
    for (int j=0;j<m;++j) for (int k=0;k<=nz;++k) for (int i=0;i<n;++i)
        wv.push_back(std::sin(2*pi*i/n)*(1.0+j/16.0+k/32.0));
    for (int j=0;j<m;++j) for (int f=0;f<n;++f) {
        u_xmap.push_back(1.0+f/16.0+j/128.0);
        u_ymap.push_back(1.0+f/64.0+j/32.0);
    }
    for (int f=0;f<=m;++f) for (int i=0;i<n;++i) {
        v_xmap.push_back(1.0+i/32.0+f/128.0);
        v_ymap.push_back(1.0+i/128.0+f/16.0);
    }
    const auto u=pack_u(tensor(uxv,{m,nz,n}));
    const auto v=pack_v(tensor(vyv,{m+1,nz,n}),true);
    const auto w=pack_mass(tensor(wv,{m,nz+1,n}));
    const auto kh=pack_mass(tensor(khv,{m,nz,n}));
    const auto mut=pack_mass(tensor(muv,{m,n}));
    (tile.solver.*access(MapTxTag{}))=pack_mass(tensor(txv,{m,n}));
    (tile.solver.*access(MapTyTag{}))=pack_mass(tensor(tyv,{m,n}));
    (tile.solver.*access(MapUxTag{}))=pack_u(tensor(u_xmap,{m,1,n})).squeeze(1);
    (tile.solver.*access(MapUyTag{}))=pack_u(tensor(u_ymap,{m,1,n})).squeeze(1);
    (tile.solver.*access(MapVxTag{}))=pack_v(tensor(v_xmap,{m+1,n}),false);
    (tile.solver.*access(MapVyTag{}))=pack_v(tensor(v_ymap,{m+1,n}),false);
    const auto result=(tile.solver.*access(Option1MomentumTag{}))(
        u,v,w,kh,mut,.1f,.07f);
    const auto& ru=std::get<0>(result);
    const auto& rv=std::get<1>(result);
    const auto& rw=std::get<2>(result);
    TORCH_CHECK(torch::equal(ru.select(2,n),ru.select(2,0)) &&
                torch::equal(ru.select(2,n+1),ru.select(2,1)) &&
                torch::equal(ru.select(0,m),ru.select(0,m-1)) &&
                torch::equal(rv.select(2,n),rv.select(2,0)) &&
                torch::equal(rv.select(0,m+1),-rv.select(0,m-1)) &&
                torch::equal(rw.select(2,n),rw.select(2,0)) &&
                torch::equal(rw.select(0,m),rw.select(0,m-1)),
                "option-1 packed momentum output aliases disagree with Q");
    torch::Tensor selected;
    int jmax=m,kmin=1,imax=n;
    if (component=="U") {selected=std::get<0>(result);imax=n+1;}
    else if (component=="V") {selected=std::get<1>(result);jmax=m+1;}
    else if (component=="W") {selected=std::get<2>(result);kmin=2;}
    else throw std::invalid_argument("invalid packed momentum component");
    const auto out=selected.to(torch::kFloat64).contiguous();
    const auto a=out.accessor<double,3>();
    for (int j=1;j<=jmax;++j) for (int k=kmin;k<=nz;++k)
        for (int i=1;i<=imax;++i)
            std::cout << "C_PACK " << component << ' ' << j << ' ' << k << ' ' << i
                      << ' ' << std::setprecision(17) << a[j-1][k-1][i-1] << '\n';
}
} // namespace

// Flat periodic-X W Fourier mode used by the source-equation oracle. Emit the
// private helper's raw W tendency so its sign and amplitude can be checked
// before any later RHS scaling or state update.
int dump_option2_w_fourier_sign(bool zero_k=false) {
    constexpr int n_x=8,n_y=6,n_z=4,n_w=n_z+1;
    constexpr double pi=3.14159265358979323846;
    const auto opt=torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU);
    std::vector<float> inverse_spacing(n_z,1.0f),half(n_w,0.5f);
    TileSDIRK3UnifiedSolver tile(n_x,n_y,n_z,1.0f,1.0f,{1.0f},{1.0f},
                                 inverse_spacing,0);
    tile.setBoundaryConditions(true,false,false,false,true,true,
                               false,false,false,false);
    tile.setVerticalInterpolationCoefficients(half.data(),half.data(),
                                               1.0f,0.0f,0.0f);
    tile.getGridInfo()->g=1.0f;
    tile.getGridInfo()->rdn=torch::ones({n_z},opt);

    auto u=torch::zeros({n_y,n_z,n_x+1},opt);
    auto v=torch::zeros({n_y+1,n_z,n_x},opt);
    auto w=torch::zeros({n_y,n_w,n_x},opt);
    auto kh=torch::full({n_y,n_z,n_x},zero_k?0.0f:2.0f,opt);
    auto rho=torch::ones({n_y,n_z,n_x},opt);
    for(int j=0;j<n_y;++j) for(int i=0;i<n_x;++i)
        w[j][2][i]=std::cos(2.0*pi*i/n_x);
    const auto mx=torch::ones({n_y,n_x},opt);
    const auto my=torch::ones({n_y,n_x},opt);
    const auto zx=torch::zeros({n_y,n_w,n_x},opt);
    const auto zy=torch::zeros_like(zx);
    const auto rdzw=torch::ones({n_y,n_z,n_x},opt);
    const auto rdz=torch::ones({n_y,n_w,n_x},opt);
    const auto tendency=(tile.*access(Option2WHelperTag{}))(
        u,v,w,kh,rho,1.0f,1.0f,mx,my,torch::Tensor(),torch::Tensor(),
        zx,zy,rdzw,rdz).to(torch::kFloat64).contiguous();
    const auto a=tendency.accessor<double,3>();
    for(int j=0;j<n_y;++j) for(int k=0;k<n_w;++k) for(int i=0;i<n_x;++i)
        std::cout << "W_RAW " << j << ' ' << k << ' ' << i << ' '
                  << std::setprecision(17) << a[j][k][i] << '\n';
    return 0;
}

int main(int argc, char** argv) {
    if (argc==2 && std::string(argv[1])=="--option2-w-fourier-sign")
        return dump_option2_w_fourier_sign();
    if (argc==2 && std::string(argv[1])=="--option2-w-fourier-zero-k")
        return dump_option2_w_fourier_sign(true);
    if (argc==2 && std::string(argv[1])=="--option2-packed-u-rdz-seam")
        return run_packed_u_rdz_seam() ? 0 : 1;
    if (argc==2 && std::string(argv[1])=="--option2-momentum-stage-geometry") {
        dump_option2_momentum_geometry();
        return 0;
    }
    if ((argc==4 || argc==5) &&
        std::string(argv[1])=="--option2-scalar-fortran-parity") {
        const std::string mutation=argc==5 ? argv[4] : "";
        if (std::string(argv[2])=="fp32")
            dump_option2_scalar_fortran_parity(torch::kFloat32,std::stoi(argv[3]),mutation);
        else if (std::string(argv[2])=="fp64")
            dump_option2_scalar_fortran_parity(torch::kFloat64,std::stoi(argv[3]),mutation);
        else return 2;
        return 0;
    }
    if (argc==4 && std::string(argv[1])=="--option1-momentum-packed") {
        if (std::string(argv[2])=="fp32") dump_option1_momentum_packed(torch::kFloat32,argv[3]);
        else if (std::string(argv[2])=="fp64") dump_option1_momentum_packed(torch::kFloat64,argv[3]);
        else return 2;
        return 0;
    }
    if (argc==5 && std::string(argv[1])=="--option1-momentum-parity") {
        const std::string precision(argv[2]);
        if (precision=="fp32") dump_option1_momentum(torch::kFloat32,argv[3],std::stoi(argv[4]));
        else if (precision=="fp64") dump_option1_momentum(torch::kFloat64,argv[3],std::stoi(argv[4]));
        else return 2;
        return 0;
    }
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
    ok = run_packed_u_rdz_seam() && ok;
    ok = run_normal_stress(torch::kFloat32) && ok;
    ok = run_normal_stress(torch::kFloat64) && ok;
    ok = run_actual_rhs_contract() && ok;
    ok = run_option1_scalar_rhs_contract() && ok;
    ok = run_option2_scalar_rhs_layer_mass_contract() && ok;
    ok = run_scalar_zero_gate() && ok;
    ok = run_normal_rhs() && ok;
    ok = run_option1_momentum_rhs_basis_contract() && ok;
    ok = run_u_terrain_thickness(torch::kFloat32) && ok;
    ok = run_u_terrain_thickness(torch::kFloat64) && ok;
    ok = run_w_density_state() && ok;
    ok = run_u_terrain_fallback(torch::kFloat32) && ok;
    ok = run_u_terrain_fallback(torch::kFloat64) && ok;
    std::cout << (ok ? "scalar diffusion contract: PASS\n"
                     : "scalar diffusion contract: FAIL\n");
    return ok ? 0 : 1;
}
