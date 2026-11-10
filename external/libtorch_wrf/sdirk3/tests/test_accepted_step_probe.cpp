#define WRF_SDIRK3_ACCEPTED_STEP_PROBE
#define WRF_SDIRK3_LINEAR_DELAY_AUDIT
#define WRF_SDIRK3_PRESSURE_STAGE_PROBE
#include "../wrf_sdirk3_accepted_step_probe.h"
#include <cassert>
#include <limits>
#include <regex>

static double field(const std::string& json,const std::string& name) {
    std::smatch m;
    assert(std::regex_search(json,m,std::regex("\\\""+name+"\\\":([-+0-9.eE]+)")));
    return std::stod(m[1]);
}
int main() {
    using namespace wrf::sdirk3::accepted_step_probe;
    auto o=torch::TensorOptions().dtype(torch::kFloat64);
    auto k=torch::tensor({1.,2.,3.,4.},o), r=torch::tensor({2.,4.,6.,8.},o);
    auto scale=torch::tensor({1.,.5,1.,.5},o), after=k+1, ra=r*.5;
    auto mu=torch::tensor({{-3.,2.}},o), mub=torch::tensor({{10.,12.}},o);
    auto b=torch::tensor({0.,0.,-5.,6.},o);
    auto result=measure(k,r,scale,after,ra,after,ra,mu,mub,b,2,2,std::sqrt(60.f)/2,std::sqrt(60.f)/4);
    assert(std::abs(field(result,"native_scaled_rms_before")-std::sqrt(60.)/2)<1e-6);
    assert(std::abs(field(result,"native_scaled_rms_after")-std::sqrt(60.)/4)<1e-6);
    assert(field(result,"mu_min")==-3 && field(result,"mu_absmax")==3 &&
           field(result,"mub_min")==10 && field(result,"mub_absmax")==12 &&
           field(result,"B_mu_absmax")==6 && field(result,"K_after_mu_absmax")==5);
    bool threw=false;
    try { measure(k,r,scale,after,ra,k,ra,mu,mub,b,2,2,std::sqrt(60.f)/2,std::sqrt(60.f)/4); } catch (...) { threw=true; }
    assert(threw);
    threw=false; auto bad=mu.clone(); bad.index_put_({0,0},std::numeric_limits<double>::infinity());
    try { measure(k,r,scale,after,ra,after,ra,bad,mub,b,2,2,std::sqrt(60.f)/2,std::sqrt(60.f)/4); } catch (...) { threw=true; }
    assert(threw);
    using wrf::sdirk3::pressure_stage_probe::PressureEosContext;
    PressureEosContext e;
    e.captured=true; e.rd=287; e.cv=717; e.cp=1004; e.p0=100000; e.t0=300;
    e.t=torch::tensor({1.,2.,3.,4.},o).view({1,2,2});
    e.ph=torch::tensor({1.,2.,4.,6.,9.,12.},o).view({1,3,2});
    e.alb=torch::ones_like(e.t); e.alt=2*e.alb; e.p_pert=3*e.alb;
    e.mu_base=torch::ones({1,2},o); e.p_base=4*e.alb;
    e.c1h=torch::tensor({.5,1.},o); e.c2h=torch::tensor({2.,3.},o);
    e.rdnw=torch::tensor({10.,20.},o);
    auto trial=e; trial.t=e.t+10; trial.ph=e.ph+1; trial.alt=e.alt+.5;
    auto B=torch::arange(10,o); auto K0=B+1; auto K1=B+2;
    auto packet=eos_scalars(e,trial,B,K0,K1,0,6,6,4);
    assert(packet.find("\"theta_pert\":[{\"min\":1,\"max\":2")!=std::string::npos);
    assert(packet.find("\"dphi_pert_computed\":[{\"min\":3,\"max\":4")!=std::string::npos);
    assert(field(packet,"B_ph_absmax")==5 && field(packet,"K1_t_absmax")==11);
    threw=false; auto wrong=trial; wrong.c1h=trial.c1h+1;
    try { (void)eos_scalars(e,wrong,B,K0,K1,0,6,6,4); } catch (...) { threw=true; }
    assert(threw);
    threw=false; wrong=trial; wrong.ph=trial.ph.slice(1,0,2);
    try { (void)eos_scalars(e,wrong,B,K0,K1,0,6,6,4); } catch (...) { threw=true; }
    assert(threw);
}
