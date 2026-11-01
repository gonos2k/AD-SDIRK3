#include "wrf_sdirk3_linear_delay_audit.h"
#include <torch/csrc/autograd/forward_grad.h>
#include <tuple>
namespace a=wrf::sdirk3::linear_delay_audit;
a::Frame* peer_current() { return a::current; }
torch::Tensor peer_dual(const torch::Tensor& x,const torch::Tensor& v) {
    a::WallTimer m(a::WallTimer::M);
    a::ArnoldiScope first(true);
    a::JvpScope j;
    const auto level=torch::autograd::forward_ad::enter_dual_level();
    try {
        auto dual=torch::_make_dual(x,v,static_cast<int64_t>(level));
        j.tangent(v);
        auto y=dual*dual;
        auto parts=torch::_unpack_dual(y,static_cast<int64_t>(level));
        auto primal=std::get<0>(parts), tangent=std::get<1>(parts);
        TORCH_CHECK(torch::equal(primal,x*x) && torch::equal(tangent,2*x*v),"dual changed by capture");
        j.result(tangent);
        j.stop(); j.stop();
        torch::autograd::forward_ad::exit_dual_level(level);
        return primal;
    } catch (...) {torch::autograd::forward_ad::exit_dual_level(level);throw;}
}
