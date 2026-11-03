#define WRF_SDIRK3_OPERATOR_GAP_PROBE
#define WRF_SDIRK3_LINEAR_DELAY_AUDIT
#include "../wrf_sdirk3_operator_gap_probe.h"
#include <cassert>
#include <cmath>
#include <regex>
#include <stdexcept>
#include <vector>

static std::vector<double> gaps(const std::string& json) {
    std::regex field("\\\"gap_scaled_l2\\\":([-+0-9.eE]+)");
    std::vector<double> values;
    for (auto i=std::sregex_iterator(json.begin(),json.end(),field); i!=std::sregex_iterator(); ++i)
        values.push_back(std::stod((*i)[1]));
    return values;
}
int main() {
    using namespace wrf::sdirk3::operator_gap_probe;
    torch::set_num_threads(1); torch::set_num_interop_threads(1);
    auto opt=torch::TensorOptions().dtype(torch::kFloat64);
    auto base=torch::tensor({1.,1.,2.},opt), k=torch::tensor({.1,.1,.2},opt);
    auto p=torch::tensor({2.,2.,3.},opt), scale=torch::ones_like(k);
    auto project=[](const Tensor& x) { return torch::stack({x[0],x[0],x[2]}); };
    auto rhs=[&](const Tensor& x) { auto y=project(x); return project(y*y*y); };
    const double a=.25;
    auto f=rhs(base+a*k).clone();
    auto apply=[&](const Tensor& d) {
        auto y=project(base+a*k); auto qd=project(d);
        return d-a*project(3*y*y*qd);
    };
    auto stable=[] {return true;};
    const auto result=measure(base,k,f,base+a*k,p,scale,{},a,project,apply,rhs,stable);
    const auto exact=gaps(result);
    assert(exact.size()==9);
    // Central difference of a cubic differs from its derivative by h^2 a^3 Q((Qd)^3).
    const double coefficient=std::pow(a,3)*project(torch::pow(project(p),3)).norm().item<double>();
    assert(std::abs(exact[0]-1e-4*coefficient)<1e-9);
    assert(std::abs(exact[1]-1e-6*coefficient)<1e-9);
    assert(exact[0]/exact[1]>90 && exact[0]/exact[1]<110);
    auto wrong=[&](const Tensor& d) { return apply(d)+d; };
    auto bad=gaps(measure(base,k,f,base+a*k,p,scale,{},a,project,wrong,rhs,stable));
    assert(bad.size()==9 && std::abs(bad[0]-p.norm().item<double>())<1e-3);
    // The same buffer backs every RHS and JVP output; preserving each result is essential.
    Tensor scratch=torch::zeros_like(k);
    auto buffered_rhs=[&](const Tensor& x) { scratch.copy_(rhs(x)); return scratch; };
    auto buffered_apply=[&](const Tensor& d) { scratch.copy_(apply(d)); return scratch; };
    auto aliased_f=buffered_rhs(base+a*k);
    auto buffered=gaps(measure(base,k,aliased_f,base+a*k,p,scale,{},a,project,buffered_apply,buffered_rhs,stable));
    assert(buffered.size()==9 && std::abs(buffered[0]-exact[0])<1e-12);
    Tensor projection_scratch=torch::zeros_like(k);
    auto buffered_project=[&](const Tensor& x) { projection_scratch.copy_(project(x)); return projection_scratch; };
    auto projected=gaps(measure(base,k,f,base+a*k,p,scale,{},a,buffered_project,apply,rhs,stable));
    assert(projected.size()==9 && std::abs(projected[0]-exact[0])<1e-12);
    bool threw=false;
    try { measure(base,k,f,base+a*k,p,scale,{},a,[](const Tensor&) -> Tensor {throw std::runtime_error("Q");},apply,rhs,stable); }
    catch (...) { threw=true; }
    assert(threw);
    threw=false;
    try { measure(base,k,f,base+a*k,p*1e-300,scale,{},a,project,apply,rhs,stable); }
    catch (...) { threw=true; }
    assert(threw);
    threw=false;
    int calls=0;
    try { measure(base,k,f,base+a*k,p,scale,{},a,project,apply,rhs,[&] { return ++calls<4; }); }
    catch (...) { threw=true; }
    assert(threw);
}
