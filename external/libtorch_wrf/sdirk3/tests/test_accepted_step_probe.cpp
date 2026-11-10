#define WRF_SDIRK3_ACCEPTED_STEP_PROBE
#define WRF_SDIRK3_LINEAR_DELAY_AUDIT
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
}
