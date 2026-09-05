// Production-linked pressure-force conversion from coupled momentum to velocity.
#include "tile_test_fixture.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {
using namespace wrf::sdirk3::test;
using wrf::sdirk3::RhsMode;
struct RhsTag { using type = torch::Tensor (TileSDIRK3UnifiedSolver::*)(const torch::Tensor&, RhsMode); friend type access(RhsTag); };
struct CoeffTag { using type = void (TileSDIRK3UnifiedSolver::*)(const float*, const float*, const float*, const float*); friend type access(CoeffTag); };
template<class T, typename T::type M> struct Accessor { friend typename T::type access(T) { return M; } };
template struct Accessor<RhsTag, &TileSDIRK3UnifiedSolver::computeUnifiedRHS>;
template struct Accessor<CoeffTag, &TileSDIRK3UnifiedSolver::setVerticalCoordinateCoefficients>;

struct Case { std::string name; float sx, sy, ux, vx, vy; bool c2; };

void setup(TileCase& t, const Case& c, std::vector<float>& vy_map) {
    std::fill(t.mass_map.begin(), t.mass_map.end(), 1.0f);
    std::fill(t.u_map.begin(), t.u_map.end(), c.ux);
    std::fill(t.v_map.begin(), t.v_map.end(), c.vx);
    vy_map.assign(nv*nx, c.vy);
    t.solver.unifiedStep(t.u.data(),t.v.data(),t.w.data(),t.ph.data(),t.theta.data(),t.mu.data(),
        t.ru.data(),t.rv.data(),t.rw.data(),t.rph.data(),t.rt.data(),t.rm.data(),
        1.0f/t.spacing,1.0f/t.spacing,t.metric.data(),t.metric.data(),t.mass_map.data(),t.mass_map.data(),
        t.u_map.data(),t.u_map.data(),t.v_map.data(),vy_map.data(),t.one.data(),t.zero.data(),
        t.one.data(),t.zero.data(),t.half.data(),t.half.data(),1,1.0e-4f,nx,ny,nz,nu,nv,nw);
    TORCH_CHECK(t.solver.getLastStepOutcomeCode()==0, "fixture setup failed");
    std::vector<float> c1h(nw,1.0f), c1f(nw,1.0f), c2f(nw,0.0f), c2h(nw,0.0f);
    if (c.c2) for (int k=0;k<nw;++k) c2h[k]=1200.0f+250.0f*k;
    (t.solver.*access(CoeffTag{}))(c1f.data(),c2f.data(),c1h.data(),c2h.data());
}

double run_case(const Case& c, RhsMode mode, bool hevi) {
    wrf::sdirk3::g_sdirk3_config.hevi_split = hevi;
    TileCase t;
    std::vector<float> vy_map; // lifetime extends through RHS evaluation
    setup(t,c,vy_map);
    std::fill(t.u.begin(),t.u.end(),0.0f); std::fill(t.v.begin(),t.v.end(),0.0f);
    std::fill(t.w.begin(),t.w.end(),0.0f); std::fill(t.theta.begin(),t.theta.end(),0.0f);
    std::fill(t.mu.begin(),t.mu.end(),0.0f);
    for (int j=0;j<ny;++j) for (int k=0;k<nw;++k) for (int i=0;i<nx;++i)
        t.ph[(j*nw+k)*nx+i] = c.sx*i*t.spacing + c.sy*j*t.spacing;
    const auto rhs=(t.solver.*access(RhsTag{}))(t.state(),mode);
    auto U=rhs.slice(0,0,su).reshape({ny,nz,nu});
    auto V=rhs.slice(0,su,su+sv).reshape({nv,nz,nx});
    const auto mass_rhs = rhs.slice(0, su+sv+2*sw+st);
    TORCH_CHECK(torch::isfinite(rhs).all().item<bool>(), "nonfinite RHS");
    TORCH_CHECK(mass_rhs.abs().max().item<double>() == 0.0,
                "pressure-only fixture has nonzero mass tendency");
    TORCH_CHECK(torch::isfinite(U).all().item<bool>() && torch::isfinite(V).all().item<bool>(), "nonfinite PGF");
    // Fortran phi-only canonical velocity: -msfux*dPhi/dx and -msfvy*dPhi/dy.
    const bool horizontal_active = mode == RhsMode::Full ||
        (hevi ? mode == RhsMode::ExplicitOnly : mode == RhsMode::ImplicitOnly);
    const double eu = horizontal_active ? -double(c.ux)*c.sx : 0.0;
    const double ev = horizontal_active ? -double(c.vy)*c.sy : 0.0;
    double err=0.0;
    int u_count=0, v_count=0;
    for (int j=1;j<ny-1;++j) for (int k=1;k<nz-1;++k) for (int i=2;i<nx-1;++i) {
        err=std::max(err,std::abs(U[j][k][i].item<double>()-eu)); ++u_count;
    }
    for (int j=1;j<ny;++j) for (int k=1;k<nz-1;++k) for (int i=1;i<nx-1;++i) {
        err=std::max(err,std::abs(V[j][k][i].item<double>()-ev));
        ++v_count;
    }
    TORCH_CHECK(u_count > 0 && v_count > 0, "empty PGF sample");
    std::cout<<c.name<<" mode="<<static_cast<int>(mode)<<" hevi="<<hevi
             <<" oracle_u="<<eu<<" oracle_v="<<ev<<" max_abs_error="<<err<<" samples="<<u_count<<","<<v_count<<'\n';
    return err;
}
}

void check_w_pgf_coordinates();
void check_pgf_debug_contract();
void check_v_pgf_nonfinite_contract();
void check_u_norm_overflow_sanitize_contract();

int main() {
    torch::set_num_threads(1);
    auto& cfg = wrf::sdirk3::g_sdirk3_config;
    cfg = wrf::sdirk3::SDIRK3Config{};
    cfg.debug_level = 0;
    cfg.coriolis_f = 0.0f;
    cfg.imex_split_mode = 3;
    cfg.mass_coordinate_mode = 1;
    const std::vector<Case> cases = {
        {"unit", 2e-5f, -1.5e-5f, 1, 1, 1, false},
        {"maponly", 2e-5f, -1.5e-5f, 1.5f, .75f, .90f, false},
        {"c2only", 2e-5f, -1.5e-5f, 1, 1, 1, true},
        {"both", 2e-5f, -1.5e-5f, 1.5f, .75f, .90f, true}};
    for (const auto& c : cases) {
        const double scale = std::max(std::abs(double(c.ux)*c.sx),
                                      std::abs(double(c.vy)*c.sy));
        const double tolerance = 128*std::numeric_limits<float>::epsilon()*scale;
        for (bool hevi : {false, true}) {
            for (auto mode : {RhsMode::Full, RhsMode::ImplicitOnly, RhsMode::ExplicitOnly}) {
                const double error = run_case(c, mode, hevi);
                TORCH_CHECK(error <= tolerance, "PGF coordinate contract failed for ",
                            c.name, " error=", error, " tolerance=", tolerance);
            }
        }
    }
    check_w_pgf_coordinates();
    check_pgf_debug_contract();
    check_v_pgf_nonfinite_contract();
    check_u_norm_overflow_sanitize_contract();
    return 0;
}
