#include "wrf_sdirk3_linear_delay_audit.h"
#include <sstream>
#include <filesystem>
#include <iostream>
namespace a=wrf::sdirk3::linear_delay_audit;
struct ThrowOnCopy {
 std::shared_ptr<bool> fail;
 explicit ThrowOnCopy(std::shared_ptr<bool> p):fail(std::move(p)) {}
 ThrowOnCopy(const ThrowOnCopy& x):fail(x.fail) {if(*fail)throw std::bad_alloc();}
 torch::Tensor operator()(const torch::Tensor& x) const {return x*3;}
};
a::Frame* peer_current();
torch::Tensor peer_dual(const torch::Tensor&,const torch::Tensor&);
void handles(a::Frame& f,const torch::Tensor& t) {
 for(auto* h:{&f.U,&f.F,&f.K,&f.b,&f.x0,&f.S,&f.Sinv,&f.mask,&f.ref_base,&f.ref_first,&f.ref_last,
              &f.Fphys,&f.Fexp,&f.full_halo}) h->set(t);
 f.have_base=f.have_first_ref=f.have_last_ref=true;
}
int main(){try {
 torch::set_num_threads(1);
 auto x=torch::tensor({1.,2.,3.,4.},torch::kFloat64),v=torch::tensor({.5,-1.,2.,-3.},torch::kFloat64);
 a::Frame f;f.stage=4;f.selected=true;handles(f,x);a::Handle context;context.set_owner(x);f.context.emplace_back("fixture",context);f.capture(f.raw_U,x);f.capture(f.raw_ref,x+1);
 a::solve_ordinal.store(4);
 {a::IterationScope it(f);TORCH_CHECK(peer_current()==&f,"TLS mismatch between translation units");
  {a::LinearScope ls(f);{a::WallTimer timer(a::WallTimer::A);auto y=peer_dual(x,v);TORCH_CHECK(torch::equal(y,x*x),"result modified");}}
  TORCH_CHECK(peer_current()==&f,"TLS lost");
 }
 TORCH_CHECK(!peer_current() && f.a_calls==1 && f.m_calls==1 && f.jvp_calls==1 && f.overlap &&
             f.clock_ok && f.linear_begin.ok && f.linear_end.ok && a::wall_ms(f.linear_begin,f.linear_end)>0 &&
             f.a_ms>0 && f.m_ms>0 && f.jvp_ms>0 && f.jvp_user_ms>=0 && f.jvp_system_ms>=0 &&
             f.sample_done && torch::equal(f.raw_v,v) && torch::equal(f.raw_jvp,2*x*v),"scope/capture counters or clocks failed");
 wrf::sdirk3::pressure_stage_probe::PressureEosContext eos;eos.captured=true;eos.ph=x;eos.geometry.c1f=x;
 a::SolveInfo info;info.ordinal=f.ordinal;info.success=true;std::ostringstream good;good.precision(3);good.setf(std::ios::scientific,std::ios::floatfield);const auto before_flags=good.flags();const auto before_precision=good.precision();a::report(f,info,&eos,good);
 TORCH_CHECK(good.flags()==before_flags && good.precision()==before_precision,"report changed caller stream formatting");
 TORCH_CHECK(!f.invalid && good.str().find("\"timing_eligible\":true")!=std::string::npos &&
             good.str().find("\"raw_npy_written\":true")!=std::string::npos,"valid report failed");
 std::cout<<"GOOD "<<good.str();
 std::ostringstream duplicate;a::report(f,info,&eos,duplicate);
 TORCH_CHECK(f.invalid && duplicate.str().find("\"timing_eligible\":false")!=std::string::npos &&
             duplicate.str().find("\"raw_npy_written\":false")!=std::string::npos,"overwrite not rejected");
 std::cout<<"DUPLICATE "<<duplicate.str();
 const auto original=std::filesystem::current_path();
 for(int k=0;k<2;++k) {
  const auto dir=original/(k==0?"missing_eos":"failed_eos");std::filesystem::create_directory(dir);std::filesystem::current_path(dir);
  auto probe=f;probe.invalid=false;std::ostringstream out;auto failed_eos=eos;failed_eos.failed=true;
  a::report(probe,info,k==0?nullptr:&failed_eos,out);
  TORCH_CHECK(probe.invalid && out.str().find("\"context_stable\":false")!=std::string::npos &&
              out.str().find("\"timing_eligible\":false")!=std::string::npos && torch::equal(x*x,torch::tensor({1.,4.,9.,16.},torch::kFloat64)),
              "missing/failed EOS accepted or function changed");
  std::cout<<(k==0?"MISSING_EOS ":"FAILED_EOS ")<<out.str();std::filesystem::current_path(original);
 }

 for(int k=0;k<2;++k) {
  const auto dir=original/(k==0?"rebound_owner":"mutated_version");std::filesystem::create_directory(dir);std::filesystem::current_path(dir);
  auto probe=f;probe.invalid=false;auto owned=x.clone();a::Handle handle;handle.set_owner(owned);probe.context.clear();probe.context.emplace_back("fixture",handle);
  if(k==0)owned=owned.clone();else owned.add_(1);
  std::ostringstream out;a::report(probe,info,&eos,out);
  TORCH_CHECK(out.str().find("\"context_stable\":false")!=std::string::npos &&
              out.str().find("\"raw_npy_written\":true")!=std::string::npos &&
              out.str().find("\"timing_eligible\":false")!=std::string::npos,
              "changed selected owner/version accepted");
  std::cout<<(k==0?"REBOUND ":"MUTATED ")<<out.str();std::filesystem::current_path(original);
 }
 a::Frame failed;{a::IterationScope it(failed);{a::LinearScope ls(failed);failed.capture(failed.raw_U,x.to(torch::kFloat32));}}
 std::ostringstream bad;a::report(failed,a::SolveInfo{},nullptr,bad);
 TORCH_CHECK(failed.invalid && bad.str().find("\"timing_eligible\":false")!=std::string::npos,"capture failure eligible");
 std::cout<<"CAPTURE_FAILURE "<<bad.str();
 a::Frame outer,inner;{a::IterationScope one(outer);TORCH_CHECK(peer_current()==&outer);{a::IterationScope two(inner);TORCH_CHECK(!peer_current()&&outer.invalid&&inner.invalid);}TORCH_CHECK(peer_current()==&outer);}TORCH_CHECK(!peer_current());
 auto changed=x.clone();a::Handle h;h.set_owner(changed);TORCH_CHECK(h.same_owner() && h.version_unchanged());
 changed=changed.clone();TORCH_CHECK(!h.same_owner() && h.version_unchanged() && !h.unchanged());
 a::Handle mutated;mutated.set_owner(changed);changed.add_(1);
 TORCH_CHECK(mutated.same_owner() && !mutated.version_unchanged() && !mutated.unchanged());
 a::Frame selected_failure;selected_failure.selected=true;
 TORCH_CHECK(!a::selected_base() && !a::capture_failure());
 {a::IterationScope it(selected_failure);
  TORCH_CHECK(!a::selected_base() && !a::capture_failure() && !selected_failure.invalid);
  {a::BoolScope base(&selected_failure.base_call,true);
   TORCH_CHECK(a::selected_base() && a::capture_failure() && selected_failure.invalid);}
  TORCH_CHECK(!a::selected_base());
 }
 TORCH_CHECK(!a::selected_base());
 auto fail_copy=std::make_shared<bool>(false);
 std::function<torch::Tensor(const torch::Tensor&)> original_fn=ThrowOnCopy(fail_copy);
 *fail_copy=true;a::Frame wrap_failure;
 TORCH_CHECK(!a::wrap(wrap_failure,original_fn,a::WallTimer::A) && wrap_failure.invalid &&
             torch::equal(original_fn(x),x*3),"transactional wrapping altered original on allocation failure");
 *fail_copy=false;
 a::Frame wrapped;{a::IterationScope it(wrapped);{a::LinearScope ls(wrapped);
  TORCH_CHECK(a::wrap(wrapped,original_fn,a::WallTimer::A) && torch::equal(original_fn(x),x*3));}}
 TORCH_CHECK(!wrapped.invalid && wrapped.a_calls==1,"successful wrapper timing failed");
 a::Frame nested_jvp;{a::IterationScope it(nested_jvp);{a::LinearScope ls(nested_jvp);a::JvpScope j1;{a::JvpScope j2;}}}
 TORCH_CHECK(nested_jvp.invalid && nested_jvp.nested,"nested JVP accepted");
 std::cout<<"OBSERVER_TEST_PASS\n";return 0;
 } catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
