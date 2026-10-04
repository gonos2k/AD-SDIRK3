#include "tile_test_fixture.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

namespace {
using namespace wrf::sdirk3::test;
using wrf::sdirk3::RhsMode;

constexpr double rd=287.0, cp=1004.5, cv=717.5, p0=100000.0, t0=300.0;
constexpr double pressure_amplitude=500.0;

void configure() {
    auto& cfg=wrf::sdirk3::g_sdirk3_config;
    cfg=wrf::sdirk3::SDIRK3Config{};
    cfg.debug_level=0;
    cfg.imex_split_mode=3;
    cfg.hevi_split=false;
    cfg.split_explicit=false;
    cfg.mass_coordinate_mode=1;
    cfg.non_hydrostatic=false;
    cfg.do_curvature=false;
    cfg.coriolis_f=0.0f;
    cfg.diffusion_option=2;
    cfg.khdif=cfg.kvdif=0.0f;
    cfg.rhs_bc_parity=false;
    cfg.mass_pgf_bc_guard=false;
}

void configure_packed(TileCase& tile) {
    tile.solver.setWRFIndices(1,nx,1,ny,1,nz,
                              1,nx,1,ny,1,nz,
                              1,nu,1,nv,1,nw);
    TORCH_CHECK(tile.packedPeriodicLayout(),"packed pressure-gradient fixture was not recognized");
}

void run_layout(bool packed) {
    configure();
    constexpr float dt=1.0e-4f;
    TileCase tile(5000.0f,0.0f);
    if(packed) configure_packed(tile);
    else TORCH_CHECK(!tile.packedPeriodicLayout(),"physical pressure-gradient fixture became packed");

    // The setup-only fixture skips the caller's initial ARK evaluation. Prime
    // that ordinary runtime path, then install the prescribed diagnostic state.
    tile.step(dt);
    const auto base_pressure=tile.basePressure().to(torch::kFloat64).contiguous();
    const auto base_theta=tile.baseTheta().to(torch::kFloat64).contiguous();
    // Scalar transcription of the WRF base EOS, kept in FP64 for the oracle.
    const auto alpha_base=(rd*base_theta/p0)*torch::pow(base_pressure/p0,-cv/cp);
    TORCH_CHECK(base_pressure.sizes()==torch::IntArrayRef({ny,nz,nx}) &&
                base_theta.sizes()==base_pressure.sizes() && alpha_base.sizes()==base_pressure.sizes(),
                "base EOS fields do not match the pressure-gradient fixture");

    const int mass_nx=packed?nx-1:nx;
    const double pi=std::acos(-1.0);
    auto state=torch::zeros({total},torch::kFloat32);
    auto theta_pert=state.slice(0,su+sv+2*sw,su+sv+2*sw+st).view({ny,nz,nx});
    const auto pb=base_pressure.accessor<double,3>();
    const auto thb=base_theta.accessor<double,3>();
    const auto alb=alpha_base.accessor<double,3>();
    for(int j=0;j<ny;++j) for(int k=0;k<nz;++k) for(int i=0;i<nx;++i)
        TORCH_CHECK(pb[j][k][i]==pb[0][k][0] && thb[j][k][i]==thb[0][k][0] &&
                    alb[j][k][i]==alb[0][k][0],
                    "analytic pressure-gradient fixture requires a horizontally uniform base column");
    for(int j=0;j<ny;++j) for(int k=0;k<nz;++k) for(int i=0;i<nx;++i) {
        const int unique_i=(packed && i==mass_nx)?0:i;
        const double target_pressure=pb[j][k][i]+pressure_amplitude*
            std::cos(2.0*pi*unique_i/mass_nx);
        TORCH_CHECK(target_pressure>0.0,"prescribed pressure became non-positive");
        // Invert the dry EOS at fixed alpha (PH and MU perturbations remain zero).
        const double target_theta=p0*alb[j][k][i]/rd*
            std::pow(target_pressure/p0,cv/cp);
        theta_pert.index_put_({j,k,i},static_cast<float>(target_theta-t0));
    }
    if(packed) state=tile.projectControlState(state).contiguous();
    tile.set(state);
    state=tile.state();
    const auto rhs=tile.rhsAt(state,RhsMode::Full,dt,state).contiguous();
    TORCH_CHECK(torch::isfinite(rhs).all().item<bool>(),"actual RHS contains NaN or Inf");
    const auto rhs_u=rhs.slice(0,0,su).view({ny,nz,nu});
    const auto theta_actual=state.slice(0,su+sv+2*sw,su+sv+2*sw+st).view({ny,nz,nx});
    const auto theta_values=theta_actual.accessor<float,3>();
    const int owned_y=packed?ny-1:ny;
    double max_signal=0.0,max_error=0.0,west_signal=0.0,last_owned_signal=0.0;
    double west_error=0.0,last_owned_error=0.0;
    int samples=0;
    for(int j=0;j<owned_y;++j) for(int k=0;k<nz;++k) for(int i=0;i<mass_nx;++i) {
        const int right=i;
        const int left=i==0?mass_nx-1:i-1;
        const double alpha_right=alb[j][k][right];
        const double alpha_left=alb[j][k][left];
        const double pressure_right=p0*std::pow(
            rd*(t0+static_cast<double>(theta_values[j][k][right]))/(p0*alpha_right),cp/cv);
        const double pressure_left=p0*std::pow(
            rd*(t0+static_cast<double>(theta_values[j][k][left]))/(p0*alpha_left),cp/cv);
        // Source-equation reference for WRF horizontal_pressure_gradient:
        // ru_tend -= .5*rdx*(c1h*muu+c2h)*(alpha_r+alpha_l)*(p_r-p_l).
        // Here PH'=MU'=0, base pressure is horizontally constant, c1h=1,
        // c2h=0, maps are unity, and the caller divides by the same layer mass;
        // only the alpha-weighted pressure difference remains in dU/dt.
        const double expected=-0.5/static_cast<double>(tile.spacing)*
            (alpha_right+alpha_left)*(pressure_right-pressure_left);
        const double actual=rhs_u[j][k][i].item<double>();
        const double error=std::abs(actual-expected);
        max_signal=std::max(max_signal,std::abs(expected));
        max_error=std::max(max_error,error);
        if(i==0) {
            west_signal=std::max(west_signal,std::abs(expected));
            west_error=std::max(west_error,error);
        }
        if(i==mass_nx-1) {
            last_owned_signal=std::max(last_owned_signal,std::abs(expected));
            last_owned_error=std::max(last_owned_error,error);
        }
        ++samples;
    }
    const double budget=256.0*std::numeric_limits<float>::epsilon()*max_signal;
    TORCH_CHECK(samples==owned_y*nz*mass_nx && max_signal>1.0e-5 &&
                west_signal>0.1*max_signal && last_owned_signal>0.1*max_signal,
                "pressure-gradient signal did not resolve both periodic seam faces");
    TORCH_CHECK(west_signal>budget && last_owned_signal>budget,
                "zeroed-west-seam/last-owned-face mutation would not be detected");
    TORCH_CHECK(west_error<=budget && last_owned_error<=budget,
                "actual RHS fails the explicit west-seam/last-owned-face oracle: packed=",packed,
                " west_error=",west_error," last_error=",last_owned_error," budget=",budget);
    TORCH_CHECK(max_error<=budget,
                "actual horizontal pressure-gradient RHS disagrees with independent EOS/Fortran equation: packed=",
                packed," max_error=",max_error," budget=",budget," signal=",max_signal);
    std::cout<<"ACTUAL_HPG layout="<<(packed?"packed":"physical")
             <<" mass_x="<<mass_nx<<" owned_u_faces="<<mass_nx
             <<" samples="<<samples<<" pressure_amplitude_Pa="<<pressure_amplitude
             <<" west_seam_signal="<<west_signal
             <<" last_owned_face_signal="<<last_owned_signal
             <<" zero_west_mutant_error="<<west_signal
             <<" zero_last_face_mutant_error="<<last_owned_signal
             <<" west_error="<<west_error<<" last_error="<<last_owned_error
             <<" max_signal_m_s2="<<max_signal<<" max_error_m_s2="<<max_error
             <<" budget_m_s2="<<budget<<" PASS\n";
}
} // namespace

int main() {
    torch::set_num_threads(1);
    run_layout(false);
    run_layout(true);
    return 0;
}
