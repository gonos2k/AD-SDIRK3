#pragma once

#include <torch/torch.h>
#include <array>
#include <cstddef>

namespace wrf::sdirk3::matched_pressure {

// Streaming linear combinations of the actual Arnoldi A inputs and outputs.
// The caller decides which calls are Arnoldi calls and checks these combinations
// against the native operator after the prepared tape has been destroyed.
class ActionSketch {
public:
    void add(const torch::Tensor& x, const torch::Tensor& y) {
        torch::NoGradGuard guard;
        TORCH_CHECK(x.defined() && y.defined() && x.is_floating_point() &&
                    y.is_floating_point() && x.sizes() == y.sizes() &&
                    x.scalar_type() == y.scalar_type() && x.device() == y.device() &&
                    torch::isfinite(x).all().item<bool>() && torch::isfinite(y).all().item<bool>(),
                    "matched pressure sketch requires finite matching tensors");
        if (count_ == 0) {
            for (int i = 0; i < 2; ++i) {
                inputs_[i] = x.detach().clone();
                outputs_[i] = y.detach().clone();
            }
        } else {
            TORCH_CHECK(x.sizes() == inputs_[0].sizes() &&
                        x.scalar_type() == inputs_[0].scalar_type() &&
                        x.device() == inputs_[0].device(), "matched pressure sketch shape changed");
            inputs_[0].add_(x.detach());
            outputs_[0].add_(y.detach());
            const double sign = count_ % 2 ? -1.0 : 1.0;
            inputs_[1].add_(x.detach(), sign);
            outputs_[1].add_(y.detach(), sign);
        }
        ++count_;
        TORCH_CHECK(torch::isfinite(inputs_[0]).all().item<bool>() &&
                    torch::isfinite(inputs_[1]).all().item<bool>() &&
                    torch::isfinite(outputs_[0]).all().item<bool>() &&
                    torch::isfinite(outputs_[1]).all().item<bool>(),
                    "matched pressure sketch accumulation is nonfinite");
    }

    std::size_t count() const { return count_; }
    bool usable() const { return count_ >= 2; }
    const torch::Tensor& input(std::size_t i) const { return inputs_.at(i); }
    const torch::Tensor& output(std::size_t i) const { return outputs_.at(i); }

private:
    std::array<torch::Tensor, 2> inputs_, outputs_;
    std::size_t count_ = 0;
};

} // namespace wrf::sdirk3::matched_pressure

#ifdef WRF_SDIRK3_MATCHED_PRESSURE_PROBE
#include "wrf_sdirk3_terminal_jvp_probe.h"
#include "wrf_sdirk3_linear_delay_audit.h"
#include "wrf_sdirk3_jvp_autograd.h"
#include "wrf_sdirk3_native_pressure_reference.h"
#include "wrf_sdirk3_v_primary_probe.h"
#include "wrf_sdirk3_pressure_v_direct.h"
#include "wrf_sdirk3_newton_solver.h"
#include "wrf_sdirk3_first_failure.h"
#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <cstdio>

namespace wrf::sdirk3::matched_pressure {
using Tensor = torch::Tensor;
using wrf_terminal_jvp::Timing;
struct Inputs {
    Tensor U, F, b, x0, S, Sinv;
    StateLayout layout;
    const UnifiedPreconditioner* raw = nullptr;
    const pressure_stage_probe::PressureEosContext* eos = nullptr;
    double a = 0, dt = 0, gamma = 0;
    int restart = 0, max_restarts = 0;
    float tol = 0, ru_share = 0;
    bool px = false, py = false;
    std::function<Tensor(const Tensor&)> rhs;
    std::function<Tensor(const Tensor&, const Tensor&, Tensor*)> native_jvp;
    std::function<long long()> fallback_count, rhs_count;
    std::function<std::string(bool)> guard;
    std::unique_ptr<PreparedJvp>** active_slot = nullptr;
    bool* rhs_interleaved = nullptr;
};
struct Stopwatch {
    std::chrono::steady_clock::time_point wall = std::chrono::steady_clock::now();
    double cpu = wrf_terminal_jvp::cpu_seconds();
    Timing elapsed() const { return {std::chrono::duration<double>(std::chrono::steady_clock::now()-wall).count(),
                                    wrf_terminal_jvp::cpu_seconds()-cpu}; }
};
inline Timing plus(Timing a, Timing b) { return {a.wall_seconds+b.wall_seconds,a.cpu_seconds+b.cpu_seconds}; }
inline Timing minus(Timing a, Timing b) { return {a.wall_seconds-b.wall_seconds,a.cpu_seconds-b.cpu_seconds}; }
inline std::string timing_json(Timing t) {
    return "{\"wall\":"+wrf_terminal_jvp::json_number(t.wall_seconds)+",\"cpu\":"+
        wrf_terminal_jvp::json_number(t.cpu_seconds)+",\"clock_valid\":"+
        (wrf_terminal_jvp::clock_valid(t) ? "true}" : "false}");
}
struct Arm {
    bool direct = false, solved = false, native_checked = false;
    std::unique_ptr<NativePressureReference> ref;
    std::unique_ptr<v_primary_probe::PreparedPrimary> primary;
    std::unique_ptr<DirectPressureV> p1;
    ActionSketch sketch;
    WRFNewtonKrylovSolver::GMRESResult result{};
    Tensor x;
    Timing setup{}, solve{}, native_residual{}, a_time{}, m_time{}, sketch_time{}, guard_time{};
    int a_calls = 0, m_calls = 0, inner = 0, subsolves = 0, primary_calls = 0, extensions = 0, guard_events = 0;
    double direct_max_residual = 0, native_scaled_rel = -1, native_physical_rel = -1;
    double sketch_max_abs = 0, sketch_max_rel = 0, sketch_cosine = 0;
    std::vector<double> block_residual_norms;
};
inline std::string arm_json(const Arm& r, Timing common, Timing tape) {
    const auto raw = plus(plus(plus(common,tape),r.setup),plus(r.solve,r.native_residual));
    const auto overhead = plus(r.sketch_time,r.guard_time);
    std::ostringstream o;
    o << "{\"kind\":" << wrf_terminal_jvp::quote(r.direct ? "P1" : "P0")
      << ",\"solved\":" << (r.solved?"true":"false") << ",\"native_checked\":" << (r.native_checked?"true":"false")
      << ",\"setup\":" << timing_json(r.setup) << ",\"solve\":" << timing_json(r.solve)
      << ",\"native_residual\":" << timing_json(r.native_residual)
      << ",\"A_time\":" << timing_json(r.a_time) << ",\"M_time\":" << timing_json(r.m_time)
      << ",\"sketch_overhead\":" << timing_json(r.sketch_time) << ",\"guard_overhead\":" << timing_json(r.guard_time)
      << ",\"charged_total\":" << timing_json(raw) << ",\"accounted_total\":" << timing_json(minus(raw,overhead))
      << ",\"A_time_includes_guard\":true,\"guard_events\":" << r.guard_events << ",\"A_calls\":" << r.a_calls << ",\"M_calls\":" << r.m_calls << ",\"inner_arnoldi\":" << r.inner
      << ",\"pressure_subsolves\":" << r.subsolves << ",\"primary_calls\":" << r.primary_calls
      << ",\"extensions\":" << r.extensions << ",\"direct_max_residual\":" << wrf_terminal_jvp::json_number(r.direct_max_residual)
      << ",\"iterations\":" << r.result.iterations << ",\"restarts\":" << r.result.restarts
      << ",\"arnoldi_spent\":" << r.result.arnoldi_spent << ",\"arnoldi_allowed\":" << r.result.arnoldi_allowed
      << ",\"success\":" << (r.result.success?"true":"false") << ",\"stopping_metric\":" << r.result.stopping_metric
      << ",\"termination_reason\":" << static_cast<int>(r.result.termination_reason)
      << ",\"tolerance\":" << wrf_terminal_jvp::json_number(r.result.tolerance_applied)
      << ",\"rho_S\":" << wrf_terminal_jvp::json_number(r.result.rho_S_final)
      << ",\"rho_D\":" << wrf_terminal_jvp::json_number(r.result.rho_D_final)
      << ",\"native_scaled_rel\":" << wrf_terminal_jvp::json_number(r.native_scaled_rel)
      << ",\"native_physical_rel\":" << wrf_terminal_jvp::json_number(r.native_physical_rel)
      << ",\"block_scaled_residual_norms\":[";
    for (size_t i=0; i<r.block_residual_norms.size(); ++i) { if(i) o << ","; o << wrf_terminal_jvp::json_number(r.block_residual_norms[i]); }
    o << "]" << ",\"sketch_count\":" << r.sketch.count() << ",\"sketch_max_abs\":" << wrf_terminal_jvp::json_number(r.sketch_max_abs)
      << ",\"sketch_max_rel\":" << wrf_terminal_jvp::json_number(r.sketch_max_rel)
      << ",\"sketch_cosine\":" << wrf_terminal_jvp::json_number(r.sketch_cosine) << "}";
    return o.str();
}
// Throws on any global or per-block disagreement, and returns maximum errors.
inline wrf_terminal_jvp::Comparison check_action(const Tensor& actual, const Tensor& expected,
                                                 const StateLayout& layout) {
    torch::NoGradGuard no_grad;
    auto check = [](const Tensor& x, const Tensor& y) {
        auto c=wrf_terminal_jvp::compare(x,y);
        TORCH_CHECK(x.defined() && y.defined() && torch::isfinite(x).all().item<bool>() &&
                    torch::isfinite(y).all().item<bool>() &&
                    torch::allclose(x,y,1e-10,1e-12), "operator action mismatch");
        return c;
    };
    auto total=check(actual,expected);
    for (const auto& b : layout.blocks) {
        const auto c=check(actual.narrow(0,b.start,b.size),expected.narrow(0,b.start,b.size));
        total.max_abs=std::max(total.max_abs,c.max_abs); total.relative=std::max(total.relative,c.relative);
    }
    return total;
}
inline bool write_exclusive(const std::string& path, const std::string& data) {
    const int fd=::open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL,0644);
    if(fd<0) return false;
    size_t pos=0; while(pos<data.size()) { const auto n=::write(fd,data.data()+pos,data.size()-pos); if(n<=0) break; pos+=n; }
    const int closed=::close(fd); return pos==data.size() && closed==0;
}
[[noreturn]] inline void run_terminal(const Inputs& in) {
    torch::NoGradGuard no_grad;
    std::array<Arm,4> arms;
    arms[1].direct=arms[2].direct=true;
    Timing common{}, tape_time{}, oracle_time{}, boundary_guard{}, tape_validation_time{};
    int tape_validation_calls=0, tape_validation_errors=0;
    double validation_max_abs=0, validation_max_rel=0;
    long long native_calls=0, tape_constructor_rhs=0;
    std::array<uint64_t,6> receipt_hashes{};
    std::string phase="input", error, receipt_context;
    bool passed=false, cost_screen=false;
    std::unique_ptr<PreparedJvp> tape;
    std::unique_ptr<PreparedJvp>* previous_slot=nullptr;
    try {
        Stopwatch common_clock;
        TORCH_CHECK(in.raw && in.eos && in.rhs && in.native_jvp && in.fallback_count && in.rhs_count &&
                    in.guard && in.active_slot && in.rhs_interleaved && !*in.active_slot && !*in.rhs_interleaved &&
                    in.layout.is_valid() && in.layout.is_exact && in.tol>0 && in.restart>0 && in.max_restarts>0 &&
                    std::isfinite(in.a) && in.a>0, "invalid inputs");
        const auto clone=[](const Tensor& t) {
            TORCH_CHECK(t.defined() && t.is_cpu() && t.scalar_type()==torch::kFloat64 && t.dim()==1 &&
                        t.numel()>0 && torch::isfinite(t).all().item<bool>(), "invalid input tensor");
            return t.detach().clone();
        };
        const auto U=clone(in.U), F=clone(in.F), b=clone(in.b), x0=clone(in.x0), S=clone(in.S), Si=clone(in.Sinv);
        TORCH_CHECK(U.numel()==in.layout.total_size && F.sizes()==U.sizes() && b.sizes()==U.sizes() &&
                    x0.sizes()==U.sizes() && S.sizes()==U.sizes() && Si.sizes()==U.sizes() &&
                    (S>0).all().item<bool>() && torch::allclose(S*Si,torch::ones_like(S),1e-12,1e-12) &&
                    b.norm().item<double>()>0, "invalid layout or scaling");
        const auto hashes=std::array<uint64_t,6>{wrf_terminal_jvp::hash(U),wrf_terminal_jvp::hash(F),
            wrf_terminal_jvp::hash(b),wrf_terminal_jvp::hash(x0),wrf_terminal_jvp::hash(S),wrf_terminal_jvp::hash(Si)};
        receipt_hashes=hashes;
        const auto full=in.guard(true), fast=in.guard(false);
        receipt_context=full;
        TORCH_CHECK(!full.empty() && !fast.empty(), "empty context guard");
        auto boundary=[&]() {
            Stopwatch t;
            TORCH_CHECK(in.guard(true)==full && in.guard(false)==fast &&
                wrf_terminal_jvp::hash(U)==hashes[0] && wrf_terminal_jvp::hash(F)==hashes[1] &&
                wrf_terminal_jvp::hash(b)==hashes[2] && wrf_terminal_jvp::hash(x0)==hashes[3] &&
                wrf_terminal_jvp::hash(S)==hashes[4] && wrf_terminal_jvp::hash(Si)==hashes[5], "context changed");
            boundary_guard=plus(boundary_guard,t.elapsed());
        };
        common=common_clock.elapsed();
        const auto normalize=[](const Tensor& x) { const auto n=x.norm().item<double>();
            TORCH_CHECK(std::isfinite(n) && n>0,"degenerate probe"); return x/n; };
        const auto indices=torch::arange(U.numel(),U.options());
        const std::array<Tensor,3> probes={normalize(b),normalize(torch::where(indices.remainder(2)==0,
            torch::ones_like(indices),-torch::ones_like(indices))),
            normalize(torch::sin(0.017*indices)+0.5*torch::cos(0.013*indices))};
        auto native=[&](const Tensor& x) {
            const auto calls=in.rhs_count(), fallback=in.fallback_count();
            Tensor primal;
            const auto physical=S*x;
            auto j=in.native_jvp(U,in.a*physical,&primal);
            ++native_calls;
            TORCH_CHECK(in.rhs_count()==calls+1 && in.fallback_count()==fallback && primal.defined() &&
                        torch::equal(primal,F) && j.defined() && torch::isfinite(j).all().item<bool>(),
                        "native JVP changed path or primal");
            return Si*(physical-j);
        };
        phase="native_preprobe";
        std::array<Tensor,3> references;
        { Stopwatch t; for(int i=0;i<3;++i) references[i]=native(probes[i]); oracle_time=plus(oracle_time,t.elapsed()); }
        boundary();
        phase="model_setup";
        for(auto& arm:arms) {
            Stopwatch t;
            arm.ref=NativePressureReference::create(*in.eos,*in.raw,in.layout,U,in.a);
            if(arm.direct) {
                arm.primary=std::make_unique<v_primary_probe::PreparedPrimary>(U,in.layout,*in.eos,in.eos->v_primary);
                auto* p=arm.primary.get();
                arm.p1=std::make_unique<DirectPressureV>(arm.ref->model_for_direct(),S,
                    [p](const Tensor& ph,const Tensor& th,const Tensor& mu){return p->apply_core(ph,th,mu);});
            }
            arm.setup=t.elapsed(); boundary();
        }
        phase="prepare_tape";
        const auto prepare_rhs_before=in.rhs_count(), prepare_fd_before=in.fallback_count();
        { Stopwatch t; tape=std::make_unique<PreparedJvp>(in.rhs,U); tape_time=t.elapsed(); }
        tape_constructor_rhs=in.rhs_count()-prepare_rhs_before;
        TORCH_CHECK(tape_constructor_rhs==1 && in.fallback_count()==prepare_fd_before &&
                    torch::equal(tape->primal(),F) && !*in.rhs_interleaved, "tape primal or path mismatch");
        previous_slot=*in.active_slot; *in.active_slot=&tape;
        const auto rhs_tape=in.rhs_count(), fd_tape=in.fallback_count();
        auto tape_alive=[&]() { TORCH_CHECK(tape && *in.active_slot==&tape && !*in.rhs_interleaved &&
                        in.rhs_count()==rhs_tape && in.fallback_count()==fd_tape,"tape invalidated"); };
        auto prepared_A=[&](const Tensor& x) { tape_alive(); const auto physical=S*x; auto j=tape->apply(in.a*physical); tape_alive(); return Si*(physical-j); };
        auto check_probes=[&]() {
            Stopwatch t;
            try { for(int i=0;i<3;++i) {
                ++tape_validation_calls;
                const auto c=check_action(prepared_A(probes[i]),references[i],in.layout);
                validation_max_abs=std::max(validation_max_abs,c.max_abs);
                validation_max_rel=std::max(validation_max_rel,c.relative);
            } } catch (...) { ++tape_validation_errors; tape_validation_time=plus(tape_validation_time,t.elapsed()); throw; }
            tape_validation_time=plus(tape_validation_time,t.elapsed());
        };
        phase="tape_preprobe"; check_probes(); boundary();
        auto* frame=linear_delay_audit::current;
        TORCH_CHECK(frame && !frame->in_linear && !frame->invalid, "missing linear audit frame");
        phase="solve";
        for(auto& arm:arms) {
            boundary(); check_probes();
            auto A=[&](const Tensor& x) {
                Stopwatch t; ++arm.a_calls;
                { Stopwatch g; TORCH_CHECK(in.guard(false)==fast,"fast context changed"); arm.guard_time=plus(arm.guard_time,g.elapsed()); }
                auto y=prepared_A(x);
                arm.a_time=plus(arm.a_time,t.elapsed());
                if(frame->arnoldi) { Stopwatch s; arm.sketch.add(x,y); arm.sketch_time=plus(arm.sketch_time,s.elapsed()); }
                return y;
            };
            auto M=[&](const Tensor& v) {
                Stopwatch t; ++arm.m_calls;
                const auto r=S*v;
                Tensor z;
                if(arm.direct) {
                    const auto result=arm.p1->solve(r);
                    arm.inner+=result.arnoldi; arm.subsolves+=result.pressure_subsolves;
                    arm.primary_calls+=result.primary_core_calls; arm.extensions+=result.full_packed_extensions;
                    TORCH_CHECK(result.converged && result.x.defined() && std::isfinite(result.true_rel),"direct inverse failed: ",result.reason);
                    arm.direct_max_residual=std::max(arm.direct_max_residual,result.true_rel); z=result.x;
                } else z=arm.ref->apply(r);
                TORCH_CHECK(torch::isfinite(z).all().item<bool>(),"nonfinite preconditioner");
                const float rn=r.norm().item<float>(), zn=z.norm().item<float>();
                const float ratio=rn>1e-12f ? zn/rn : 1.f;
                if (!(std::isfinite(ratio) && std::isfinite(zn) && ratio<=100.f && ratio>=1e-6f)) {
                    ++arm.guard_events; TORCH_CHECK(false,"preconditioner ratio guard");
                }
                auto out=Si*z; arm.m_time=plus(arm.m_time,t.elapsed()); return out;
            };
            Tensor d_inv;
            { linear_delay_audit::LinearScope scope(*frame);
              Stopwatch t;
              arm.result=krylov_methods::solve_fgmres(A,b,x0.clone(),2,in.ru_share,in.restart,in.tol,in.max_restarts,
                    M,&in.layout,nullptr,in.px,in.py,nullptr,nullptr,&S,&d_inv);
              arm.solve=t.elapsed(); }
            tape_alive();
            TORCH_CHECK(!frame->invalid && arm.result.x.defined() && arm.result.success &&
                        arm.result.stopping_metric==static_cast<int>(KrylovStoppingMetric::IdentityS) &&
                        !d_inv.defined() && arm.sketch.usable() && arm.result.arnoldi_spent>1 &&
                        arm.sketch.count()==static_cast<size_t>(arm.result.arnoldi_spent),
                        "unsupported FGMRES result or Arnoldi capture mismatch");
            arm.x=arm.result.x.detach().clone(); arm.solved=true;
            check_probes(); boundary();
        }
        phase="destroy_tape";
        tape.reset(); *in.active_slot=previous_slot; previous_slot=nullptr;
        boundary();
        phase="native_postcheck";
        for(auto& arm:arms) {
            boundary();
            { Stopwatch t; const auto residual=b-native(arm.x);
              arm.native_scaled_rel=residual.norm().item<double>()/b.norm().item<double>();
              arm.native_physical_rel=(S*residual).norm().item<double>()/(S*b).norm().item<double>();
              for (const auto& block:in.layout.blocks) arm.block_residual_norms.push_back(residual.narrow(0,block.start,block.size).norm().item<double>());
              arm.native_residual=t.elapsed(); }
            TORCH_CHECK(std::isfinite(arm.native_scaled_rel) && std::isfinite(arm.native_physical_rel) &&
                        arm.native_scaled_rel<in.tol,"native residual failed");
            { Stopwatch t;
              const auto n0=arm.sketch.input(0).norm().item<double>();
              const auto n1=arm.sketch.input(1).norm().item<double>();
              TORCH_CHECK(std::isfinite(n0) && std::isfinite(n1) && n0>0 && n1>0, "degenerate Arnoldi mixtures");
              arm.sketch_cosine=torch::dot(arm.sketch.input(0)/n0,arm.sketch.input(1)/n1).item<double>();
              TORCH_CHECK(std::isfinite(arm.sketch_cosine) && std::abs(arm.sketch_cosine)<1-1e-12,
                          "collinear Arnoldi mixtures");
              for(int i=0;i<2;++i) {
                  TORCH_CHECK(arm.sketch.input(i).norm().item<double>()>0,"degenerate Arnoldi mixture");
                  const auto c=check_action(native(arm.sketch.input(i)),arm.sketch.output(i),in.layout);
                  arm.sketch_max_abs=std::max(arm.sketch_max_abs,c.max_abs);
                  arm.sketch_max_rel=std::max(arm.sketch_max_rel,c.relative);
              }
              oracle_time=plus(oracle_time,t.elapsed()); }
            boundary(); arm.native_checked=true;
        }
        TORCH_CHECK(native_calls==15 && tape_constructor_rhs==1, "oracle or tape preparation call count mismatch");
        phase="cost_screen";
        const auto total=[&](int i,bool adjusted) {
            auto t=plus(plus(plus(common,tape_time),arms[i].setup),plus(arms[i].solve,arms[i].native_residual));
            return adjusted ? minus(t,plus(arms[i].sketch_time,arms[i].guard_time)) : t;
        };
        TORCH_CHECK(wrf_terminal_jvp::clock_valid(common) && wrf_terminal_jvp::clock_valid(tape_time) &&
                    wrf_terminal_jvp::clock_valid(oracle_time) && wrf_terminal_jvp::clock_valid(boundary_guard) &&
                    wrf_terminal_jvp::clock_valid(tape_validation_time), "invalid common timing");
        for (const auto& arm:arms) {
            TORCH_CHECK(wrf_terminal_jvp::clock_valid(arm.setup) && wrf_terminal_jvp::clock_valid(arm.solve) &&
                        wrf_terminal_jvp::clock_valid(arm.native_residual) && wrf_terminal_jvp::clock_valid(arm.a_time) &&
                        wrf_terminal_jvp::clock_valid(arm.m_time) && wrf_terminal_jvp::clock_valid(arm.guard_time) &&
                        wrf_terminal_jvp::clock_valid(arm.sketch_time), "invalid arm timing");
        }
        cost_screen=true;
        for(bool adjusted:{false,true}) for(const auto& pair: {std::pair<int,int>{0,1}, {3,2}}) {
            const auto p0=total(pair.first,adjusted), p1=total(pair.second,adjusted);
            TORCH_CHECK(wrf_terminal_jvp::clock_valid(p0) && wrf_terminal_jvp::clock_valid(p1), "invalid adjusted timing");
            cost_screen &= p0.wall_seconds>0 && p1.wall_seconds>0 && p0.cpu_seconds>0 && p1.cpu_seconds>0 &&
                           p0.wall_seconds>=2*p1.wall_seconds && p0.cpu_seconds>=2*p1.cpu_seconds;
        }
        passed=true; phase="complete";
    } catch(const std::exception& e) { error=e.what(); }
      catch(...) { error="unknown exception"; }
    tape.reset();
    if(in.active_slot && *in.active_slot) *in.active_slot=previous_slot;
    std::ostringstream out;
    out << "{\"status\":" << wrf_terminal_jvp::quote(passed?"matched_prepared_diagnostic_passed":"matched_prepared_diagnostic_failed")
        << ",\"phase\":" << wrf_terminal_jvp::quote(phase) << ",\"error\":" << wrf_terminal_jvp::quote(error)
        << ",\"forecast_complete\":false,\"stage\":2,\"newton_iteration\":0,\"timestep\":0"
        << ",\"order\":[\"P0\",\"P1\",\"P1\",\"P0\"],\"single_tape\":true,\"no_rhs_interleaving_required\":true"
        << ",\"a\":" << wrf_terminal_jvp::json_number(in.a) << ",\"dt\":" << wrf_terminal_jvp::json_number(in.dt)
        << ",\"gamma\":" << wrf_terminal_jvp::json_number(in.gamma) << ",\"tolerance\":" << wrf_terminal_jvp::json_number(in.tol)
        << ",\"restart\":" << in.restart << ",\"max_restarts\":" << in.max_restarts << ",\"N\":" << in.layout.total_size
        << ",\"frozen_hashes\":[" << receipt_hashes[0] << ',' << receipt_hashes[1] << ',' << receipt_hashes[2] << ','
        << receipt_hashes[3] << ',' << receipt_hashes[4] << ',' << receipt_hashes[5] << "]"
        << ",\"hash_order\":[\"U\",\"F\",\"b\",\"x0\",\"S\",\"Sinv\"]"
        << ",\"observed_native_context\":" << wrf_terminal_jvp::quote(receipt_context)
        << ",\"context_scope\":\"one immutable prepared graph; native closure attested only on captured subset\""
        << ",\"rhs_interleaved\":" << (in.rhs_interleaved && *in.rhs_interleaved ? "true" : "false")
        << ",\"native_calls\":" << native_calls << ",\"tape_constructor_rhs_calls\":" << tape_constructor_rhs
        << ",\"tape_validation_calls\":" << tape_validation_calls << ",\"tape_validation_errors\":" << tape_validation_errors
        << ",\"tape_validation_max_abs\":" << wrf_terminal_jvp::json_number(validation_max_abs)
        << ",\"tape_validation_max_rel\":" << wrf_terminal_jvp::json_number(validation_max_rel)
        << ",\"tape_validation_time\":" << timing_json(tape_validation_time)
        << ",\"validation_scope\":\"bounded probes and streamed Arnoldi mixtures, not proof for all directions\""
        << ",\"primal_comparison\":\"torch::equal numeric equality, signed zeros equivalent\""
        << ",\"action_comparison\":\"elementwise allclose rtol=1e-10 atol=1e-12, global and each block\""
        << ",\"scope\":\"common_prepared_A_plus_M_not_native_FWAD_cost\",\"cost_screen_only\":true,\"cost_screen\":" << (cost_screen?"true":"false")
        << ",\"accounting\":\"modeled per-candidate cost: common and tape fully charged per arm; not actual ABBA runtime; validation probes and mixture oracle checks excluded; overhead subtraction is an estimate\""
        << ",\"common\":" << timing_json(common) << ",\"tape_preparation\":" << timing_json(tape_time)
        << ",\"oracle_verification\":" << timing_json(oracle_time) << ",\"boundary_guards\":" << timing_json(boundary_guard) << ",\"arms\":[";
    for(size_t i=0;i<arms.size();++i) { if(i) out << ','; out << arm_json(arms[i],common,tape_time); }
    out << "]}\n";
    const auto json=out.str(); std::cerr << "[MATCHED_PRESSURE] " << json;
    const bool written=write_exclusive("matched_pressure_probe.json",json);
    std::cerr.flush(); std::cout.flush(); std::_Exit(passed && written ? 88 : 89);
}
[[noreturn]] inline void run(const Inputs& in) { run_terminal(in); }
[[noreturn]] inline void fail_exit(const char* phase, const char* message) noexcept {
    try {
        const std::string json = "{\"status\":\"matched_prepared_diagnostic_failed\",\"forecast_complete\":false,\"phase\":" +
            wrf_terminal_jvp::quote(phase ? phase : "unknown") + ",\"error\":" +
            wrf_terminal_jvp::quote(message ? message : "unknown") + ",\"arms\":[]}\n";
        std::cerr << "[MATCHED_PRESSURE] " << json;
        write_exclusive("matched_pressure_probe.json",json);
        std::cerr.flush();
    } catch (...) {
        constexpr char fallback[]="[MATCHED_PRESSURE] {\"status\":\"matched_prepared_diagnostic_failed\",\"forecast_complete\":false,\"error\":\"failure_reporting_exception\"}\n";
        (void)::write(STDERR_FILENO,fallback,sizeof(fallback)-1);
    }
    std::_Exit(89);
}
} // namespace wrf::sdirk3::matched_pressure
#endif
