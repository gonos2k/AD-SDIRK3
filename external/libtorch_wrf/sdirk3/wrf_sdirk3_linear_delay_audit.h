#pragma once
#ifdef WRF_SDIRK3_LINEAR_DELAY_AUDIT
#include <torch/torch.h>
#include <ATen/Parallel.h>
#include "wrf_sdirk3_pressure_stage_probe.h"
#include "wrf_sdirk3_state_layout.h"
#include <sys/resource.h>
#include <chrono>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>

namespace wrf::sdirk3::linear_delay_audit {
using Clock=std::chrono::steady_clock;
struct Stamp { Clock::time_point wall; rusage usage{}; bool ok=false; };
inline Stamp stamp() { Stamp s; s.wall=Clock::now(); s.ok=(getrusage(RUSAGE_SELF,&s.usage)==0); return s; }
inline double tv_ms(const timeval& t) { return 1000.0*t.tv_sec+0.001*t.tv_usec; }
inline double wall_ms(const Stamp& a,const Stamp& b) { return std::chrono::duration<double,std::milli>(b.wall-a.wall).count(); }
struct Handle {
    torch::Tensor value;
    const torch::Tensor* owner=nullptr; // Tile member handle; valid through this synchronous solve.
    int64_t version=-1;
    bool valid=false;
    void set(const torch::Tensor& x) {
        value=x; owner=nullptr; valid=!x.defined(); version=-1;
        if (x.defined()) { try { version=x._version(); valid=true; } catch (...) { valid=false; } }
    }
    void set_owner(const torch::Tensor& x) { set(x); owner=&x; }
    bool same_owner() const { return !owner || owner->is_same(value); }
    bool version_unchanged() const {
        if (!valid) return false;
        if (!value.defined()) return true;
        try { return value._version()==version; } catch (...) { return false; }
    }
    bool unchanged() const { return same_owner() && version_unchanged(); }
};
struct Frame {
    int stage=0,newton=0,ordinal=0,refinement_passes=1; bool selected=false,invalid=false,clock_ok=true,nested=false;
    bool in_linear=false,linear_started=false,base_call=false,fast_rhs=false,arnoldi=false,sample_active=false,sample_done=false;
    int rhs_all=0,rhs_linear=0,rhs_fast=0,ref_handle_changes=0,ref_version_changes=0;
    std::array<int,3> rhs_modes{},linear_rhs_modes{};
    int a_calls=0,m_calls=0,jvp_calls=0,a_depth=0,m_depth=0,jvp_depth=0; bool overlap=false;
    long long sampled_fallbacks=-1;
    double a_ms=0,m_ms=0,jvp_ms=0,jvp_user_ms=0,jvp_system_ms=0,capture_ms=0,capture_pre_ms=0;
    long long fallback_start=0; int m_native_start=0,guard_start=0,ad_start=0,fd_start=0;
    Stamp linear_begin{},linear_end{};
    torch::Tensor raw_U,raw_ref,raw_v,raw_jvp;
    Handle U,F,K,b,x0,S,Sinv,mask,ref_base,ref_first,ref_last,Fphys,Fexp,full_halo;
    bool have_base=false,have_first_ref=false,have_last_ref=false;
    float dt_stage=0; int split=0,n_moist=0; bool has_physics=false,prepared=false;
    std::array<int,20> topology{}; std::array<bool,16> flags{};
    std::vector<std::pair<std::string,Handle>> context;
    void capture(torch::Tensor& dst,const torch::Tensor& x) {
        const auto start=Clock::now();
        try { torch::NoGradGuard guard; TORCH_CHECK(x.defined() && x.is_cpu() && x.scalar_type()==torch::kFloat64 && x.dim()==1,
                "linear delay snapshot requires CPU FP64 packed tensor"); dst=x.detach().contiguous().clone(); }
        catch (...) { invalid=true; }
        const double ms=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
        if(in_linear)capture_ms+=ms;else capture_pre_ms+=ms;
    }
};
inline thread_local Frame* current=nullptr;
inline std::atomic<int> solve_ordinal{0};
inline bool selected_base() noexcept { return current && current->selected && current->base_call; }
inline bool capture_failure() noexcept {
    if (!selected_base()) return false;
    current->invalid=true;
    return true;
}
struct IterationScope {
    Frame* prior; Frame& frame;
    explicit IterationScope(Frame& f):prior(current),frame(f) { if(prior) { f.nested=true; f.invalid=true; prior->nested=true; prior->invalid=true; current=nullptr; } else current=&f; }
    ~IterationScope() noexcept { current=prior; }
};
struct LinearScope {
    Frame* frame; bool prior=false;
    explicit LinearScope(Frame& f):frame(&f),prior(f.in_linear) { if(prior) {f.nested=true;f.invalid=true;} f.in_linear=true; f.linear_started=true; f.ordinal=solve_ordinal.fetch_add(1,std::memory_order_relaxed)+1; f.linear_begin=stamp(); f.clock_ok &= f.linear_begin.ok; }
    ~LinearScope() noexcept { frame->linear_end=stamp(); frame->clock_ok &= frame->linear_end.ok; frame->in_linear=prior; }
};
struct WallTimer {
    enum Kind { A,M } kind; Frame* f; Clock::time_point start;
    explicit WallTimer(Kind k):kind(k),f(current && current->in_linear ? current : nullptr) {
        if(!f)return;
        if(k==A) { f->overlap |= f->a_depth>0 || f->m_depth>0; ++f->a_depth; ++f->a_calls; }
        else { f->overlap |= f->m_depth>0 || f->a_depth>0; ++f->m_depth; ++f->m_calls; }
        start=Clock::now();
    }
    ~WallTimer() noexcept {
        if(!f)return; const double ms=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
        if(kind==A) { f->a_ms+=ms; --f->a_depth; } else { f->m_ms+=ms; --f->m_depth; }
    }
};
// Preserve the original std::function on every observer allocation/copy failure.
// swap is noexcept; no target state changes until the wrapper is complete.
using Function=std::function<torch::Tensor(const torch::Tensor&)>;
inline bool wrap(Frame& frame,Function& target,WallTimer::Kind kind) noexcept {
    if (!target) return true;
    try {
        Function original=target;
        Function observed=[original=std::move(original),kind](const torch::Tensor& x) {
            WallTimer timer(kind);
            return original(x);
        };
        target.swap(observed);
        return true;
    } catch (...) { frame.invalid=true; return false; }
}
struct ArnoldiScope {
    Frame* f; bool old=false,old_sample=false;
    explicit ArnoldiScope(bool first):f(current && current->in_linear ? current : nullptr) {
        if(!f)return; old=f->arnoldi; old_sample=f->sample_active; f->arnoldi=true;
        f->sample_active=f->selected && first && !f->sample_done;
    }
    ~ArnoldiScope() noexcept { if(f){f->arnoldi=old;f->sample_active=old_sample;} }
};
struct JvpScope {
    Frame* f; Stamp start; bool stopped=false;
    explicit JvpScope():f(current && current->in_linear ? current : nullptr) { if(f){if(f->jvp_depth++>0){f->nested=true;f->invalid=true;}++f->jvp_calls;start=stamp();f->clock_ok &= start.ok;} }
    void tangent(const torch::Tensor& x) { if(f && f->sample_active && !f->sample_done) f->capture(f->raw_v,x); }
    void result(const torch::Tensor& x) { if(f && f->sample_active && !f->sample_done) { f->capture(f->raw_jvp,x); f->sample_done=true; } }
    void stop() {
        if(!f || stopped)return; const auto end=stamp();f->clock_ok &= end.ok; f->jvp_ms+=wall_ms(start,end);
        if(start.ok && end.ok) { f->jvp_user_ms+=tv_ms(end.usage.ru_utime)-tv_ms(start.usage.ru_utime); f->jvp_system_ms+=tv_ms(end.usage.ru_stime)-tv_ms(start.usage.ru_stime); }
        --f->jvp_depth; stopped=true;
    }
    ~JvpScope() noexcept { stop(); }
};
struct BoolScope { bool* value; bool prior; BoolScope(bool* p,bool on):value(p),prior(p?*p:false) {if(p)*p=on;} ~BoolScope(){if(value)*value=prior;} };
inline uint64_t audit_hash_tensor(const torch::Tensor& t) {
    uint64_t h=14695981039346656037ULL;
    auto bytes=[&](const void* p,size_t n){ const auto* b=static_cast<const unsigned char*>(p); for(size_t i=0;i<n;++i){h^=b[i];h*=1099511628211ULL;} };
    const uint8_t present=t.defined();bytes(&present,sizeof(present));if(!present)return h;
    TORCH_CHECK(t.is_cpu(),"linear delay hash requires CPU tensor"); auto x=t.detach().contiguous();
    const int32_t type=static_cast<int32_t>(x.scalar_type());const int64_t ndim=x.dim();bytes(&type,sizeof(type));bytes(&ndim,sizeof(ndim));
    for(auto size:x.sizes())bytes(&size,sizeof(size));const int64_t n=x.numel();bytes(&n,sizeof(n));bytes(x.const_data_ptr(),x.nbytes());return h;
}
inline void tensor_meta(std::ostream& out,const torch::Tensor& t) {
    if(!t.defined()) { out << "null"; return; }
    out << "{\"dtype_id\":" << static_cast<int>(t.scalar_type()) << ",\"shape\":[";
    for(int64_t i=0;i<t.dim();++i) { if(i)out << ','; out << t.size(i); }
    out << "]}";
}
inline bool write_npy(const char* path,const torch::Tensor& t) {
    if(!t.defined() || !t.is_cpu() || t.scalar_type()!=torch::kFloat64 || t.dim()!=1 || !t.is_contiguous())return false;
    const uint16_t endian=1;if(*reinterpret_cast<const unsigned char*>(&endian)!=1)return false;
    FILE* exists=std::fopen(path,"rb");if(exists){std::fclose(exists);return false;}
    FILE* out=std::fopen(path,"wb");if(!out)return false;
    std::string h="{'descr': '<f8', 'fortran_order': False, 'shape': ("+std::to_string(t.numel())+",), }";
    const size_t pad=(16-((10+h.size()+1)%16))%16;h.append(pad,' ');h.push_back('\n');
    if(h.size()>65535){std::fclose(out);return false;}
    const unsigned char prefix[8]={0x93,'N','U','M','P','Y',1,0};const uint16_t n=static_cast<uint16_t>(h.size());
    bool ok=std::fwrite(prefix,1,8,out)==8 && std::fwrite(&n,1,2,out)==2 && std::fwrite(h.data(),1,h.size(),out)==h.size() &&
        std::fwrite(t.const_data_ptr(),1,t.nbytes(),out)==t.nbytes();
    ok=std::fclose(out)==0 && ok;return ok;
}
struct SolveInfo {
    int64_t timestep=0,solver_id=0,generation=0;
    int ordinal=0,iterations=-1,restarts=-1,restart_budget=0,max_restarts=0;
    bool success=false;
    double tolerance=0,dt=0,gamma=0;
    int selected_ad_calls=0,selected_fd_calls=0,m_native_calls=0,guard_events=0;
    long long fallback_delta=0;
    const StateLayout* layout=nullptr;
};
// Called only after the measured linear scope. Hashes and four NPY writes are
// diagnostic overhead outside that scope; clones and timer calls remain inside.
inline void report(Frame& f,const SolveInfo& i,const pressure_stage_probe::PressureEosContext* eos,std::ostream& output) noexcept {
    try {
        torch::NoGradGuard guard;
        const double wall=wall_ms(f.linear_begin,f.linear_end);
        const double user=tv_ms(f.linear_end.usage.ru_utime)-tv_ms(f.linear_begin.usage.ru_utime);
        const double sys=tv_ms(f.linear_end.usage.ru_stime)-tv_ms(f.linear_begin.usage.ru_stime);
        const bool clock_valid=f.clock_ok && f.linear_begin.ok && f.linear_end.ok && wall>=0 && user>=0 && sys>=0;
        const double remainder=wall-f.a_ms-f.m_ms;
        const bool disjoint=!f.overlap && !f.nested && f.a_depth==0 && f.m_depth==0 && f.jvp_depth==0 && remainder>=0;
        bool context_valid=!f.selected;
        bool raw_written=false;
        std::ostringstream extra; extra << std::setprecision(17);
        if (f.selected) {
            context_valid=f.have_base && f.have_first_ref && f.have_last_ref && eos && eos->captured &&
                !eos->failed && !eos->duplicate && f.U.unchanged() && f.F.unchanged() && f.K.unchanged() &&
                f.b.unchanged() && f.x0.unchanged() && f.S.unchanged() && f.Sinv.unchanged() && f.mask.unchanged() &&
                f.ref_base.unchanged() && f.ref_first.unchanged() && f.ref_last.unchanged() &&
                f.ref_base.value.is_same(f.ref_first.value) && f.ref_first.value.is_same(f.ref_last.value) &&
                f.ref_handle_changes==0 && f.ref_version_changes==0 &&
                f.Fphys.unchanged() && f.Fexp.unchanged() && f.full_halo.unchanged();
            for (const auto& entry:f.context) {
                const bool scratch=entry.first.find("_work")!=std::string::npos ||
                    entry.first=="ru_tend" || entry.first=="rv_tend" || entry.first=="rw_tend" ||
                    entry.first=="t_tend" || entry.first=="mu_tend" || entry.first=="ph_tend" || entry.first=="qv_tend";
                if(!scratch && !entry.second.unchanged())context_valid=false;
            }
            const auto h=[&](const Handle& x) { return x.unchanged() ? audit_hash_tensor(x.value) : uint64_t(0); };
            const uint64_t U=h(f.U),F=h(f.F),K=h(f.K),b=h(f.b),x0=h(f.x0),S=h(f.S),Si=h(f.Sinv),mask=h(f.mask);
            const uint64_t rb=h(f.ref_base),rf=h(f.ref_first),rl=h(f.ref_last);
            const uint64_t fp=h(f.Fphys),fe=h(f.Fexp),fh=h(f.full_halo);
            extra << ",\"selected_context\":{\"context_stable\":" << (context_valid?"true":"false")
                  << ",\"state_fnv64\":\"" << std::hex << U << "\",\"F_fnv64\":\"" << F
                  << "\",\"K_fnv64\":\"" << K << "\",\"b_fnv64\":\"" << b
                  << "\",\"x0_fnv64\":\"" << x0 << "\",\"S_fnv64\":\"" << S
                  << "\",\"Sinv_fnv64\":\"" << Si << "\",\"mask_fnv64\":\"" << mask
                  << "\",\"reference_base_fnv64\":\"" << rb << "\",\"reference_first_fnv64\":\"" << rf
                  << "\",\"reference_last_fnv64\":\"" << rl << "\",\"Fphys_fnv64\":\"" << fp
                  << "\",\"Fexp_fnv64\":\"" << fe << "\",\"full_halo_fnv64\":\"" << fh << "\"" << std::dec
                  << ",\"reference_base_version\":" << f.ref_base.version
                  << ",\"reference_first_version\":" << f.ref_first.version
                  << ",\"reference_last_version\":" << f.ref_last.version
                  << ",\"reference_first_scope\":\"first_actual_rhs_in_linear_solve\""
                  << ",\"reference_sample_scope\":\"rhs_during_first_arnoldi_A_after_reference_update\""
                  << ",\"reference_handle_changes\":" << f.ref_handle_changes
                  << ",\"reference_version_changes\":" << f.ref_version_changes
                  << ",\"dt_stage\":" << f.dt_stage << ",\"split_mode\":" << f.split
                  << ",\"n_moist\":" << f.n_moist << ",\"has_physics\":" << (f.has_physics?"true":"false")
                  << ",\"prepared_rhs\":" << (f.prepared?"true":"false")
                  << ",\"Fphys_defined\":" << (f.Fphys.value.defined()?"true":"false")
                  << ",\"Fphys_used\":" << ((f.split<2 && f.has_physics)?"true":"false")
                  << ",\"Fexp_defined\":" << (f.Fexp.value.defined()?"true":"false")
                  << ",\"Fexp_used\":" << (f.split==1?"true":"false")
                  << ",\"full_halo_defined\":" << (f.full_halo.value.defined()?"true":"false")
                  << ",\"frozen_meta_order\":\"U,F,K,b,x0,S,Sinv,mask,ref_base,ref_first,ref_last,Fphys,Fexp,halo\",\"frozen_meta\":[";
            const std::array<torch::Tensor,14> frozen={f.U.value,f.F.value,f.K.value,f.b.value,f.x0.value,f.S.value,
                f.Sinv.value,f.mask.value,f.ref_base.value,f.ref_first.value,f.ref_last.value,
                f.Fphys.value,f.Fexp.value,f.full_halo.value};
            for(size_t j=0;j<frozen.size();++j){if(j)extra << ',';tensor_meta(extra,frozen[j]);}
            extra << "]"
                  << ",\"topology_order\":\"nx,ny,nz,nxu,nyv,nzw,its,ite,ids,ide,jts,jte,jds,jde,nprocx,nprocy,mypx,mypy,halo_width,y_policy\",\"topology\":[";
            for(size_t j=0;j<f.topology.size();++j) { if(j)extra << ',';extra << f.topology[j]; }
            extra << "],\"flag_order\":\"px,py,sxs,sxe,sys,sye,oxs,oxe,oys,oye,specified,nested,polar,ad_halo,omega_ref_update,packed\",\"flags\":[";
            for(size_t j=0;j<f.flags.size();++j) { if(j)extra << ',';extra << (f.flags[j]?1:0); }
            extra << "],\"source_handles\":[";
            for(size_t j=0;j<f.context.size();++j) {
                if(j)extra << ',';const auto& entry=f.context[j];const bool unchanged=entry.second.unchanged();
                extra << "{\"name\":\"" << entry.first << "\",\"unchanged\":" << (unchanged?"true":"false")
                      << ",\"same_owner\":" << (entry.second.same_owner()?"true":"false")
                      << ",\"version_unchanged\":" << (entry.second.version_unchanged()?"true":"false")
                      << ",\"version\":" << entry.second.version << ",\"meta\":";
                tensor_meta(extra,entry.second.value);
                extra << ",\"fnv64\":\"" << std::hex
                      << (unchanged?audit_hash_tensor(entry.second.value):uint64_t(0)) << "\"}" << std::dec;
            }
            extra << "],\"EOS_captured\":" << ((eos && eos->captured)?"true":"false");
            if(eos && eos->captured && !eos->failed && !eos->duplicate) {
                const auto& e=*eos; const auto& g=e.geometry;
                extra << ",\"EOS_fnv64\":[";
                const std::array<torch::Tensor,11> t={e.ph,e.t,e.mu,e.mu_base,e.alb,e.p_base,e.c1h,e.c2h,e.rdnw,e.alt,e.p_pert};
                for(size_t j=0;j<t.size();++j){if(j)extra << ',';extra << '\"' << std::hex << audit_hash_tensor(t[j]) << '\"' << std::dec;}
                extra << "],\"EOS_order\":\"ph,t,mu,mu_base,alb,p_base,c1h,c2h,rdnw,alt,p_pert\",\"EOS_meta\":[";
                for(size_t j=0;j<t.size();++j){if(j)extra << ',';tensor_meta(extra,t[j]);}
                extra << "],\"geometry_fnv64\":[";
                const std::array<torch::Tensor,10> gt={g.c1f,g.c2f,g.rdn,g.msftx,g.msfty,g.msfux,g.msfuy,g.msfvx,g.msfvy,g.ground_phi};
                for(size_t j=0;j<gt.size();++j){if(j)extra << ',';extra << '\"' << std::hex << audit_hash_tensor(gt[j]) << '\"' << std::dec;}
                extra << "],\"geometry_order\":\"c1f,c2f,rdn,msftx,msfty,msfux,msfuy,msfvx,msfvy,ground_phi\",\"geometry_meta\":[";
                for(size_t j=0;j<gt.size();++j){if(j)extra << ',';tensor_meta(extra,gt[j]);}
                extra << "]"
                      << ",\"rdy\":" << g.rdy << ",\"rdx\":" << g.rdx << ",\"gravity\":" << g.gravity
                      << ",\"whole_single_rank\":" << (g.whole_single_rank?"true":"false")
                      << ",\"packed_periodic_symmetric\":" << (g.packed_periodic_x_symmetric_y?"true":"false")
                      << ",\"rd\":" << e.rd << ",\"cv\":" << e.cv << ",\"cp\":" << e.cp
                      << ",\"p0\":" << e.p0 << ",\"t0\":" << e.t0;
            }
            extra << ",\"sample_u_eval_fnv64\":\"" << std::hex << (f.raw_U.defined()?audit_hash_tensor(f.raw_U):uint64_t(0))
                  << "\",\"sample_reference_fnv64\":\"" << (f.raw_ref.defined()?audit_hash_tensor(f.raw_ref):uint64_t(0))
                  << "\",\"sample_tangent_fnv64\":\"" << (f.raw_v.defined()?audit_hash_tensor(f.raw_v):uint64_t(0))
                  << "\",\"sample_jvp_fnv64\":\"" << (f.raw_jvp.defined()?audit_hash_tensor(f.raw_jvp):uint64_t(0))
                  << "\"" << std::dec
                  << ",\"uncovered\":\"transitive mutable caches, instrumentation and backend internals; no full-operator equality claim\"}";
            const auto bytes=f.raw_U.defined() && f.raw_ref.defined() && f.raw_v.defined() && f.raw_jvp.defined()
                ? f.raw_U.nbytes()+f.raw_ref.nbytes()+f.raw_v.nbytes()+f.raw_jvp.nbytes() : 0;
            bool names_free=true;
            for(const char* name : {"linear_delay_u_eval.npy","linear_delay_u_ref_first_jvp.npy",
                                    "linear_delay_first_tangent.npy","linear_delay_first_jvp.npy"}) {
                FILE* old=std::fopen(name,"rb"); if(old) { std::fclose(old); names_free=false; }
            }
            if(f.sample_done && names_free && bytes>0 && bytes+1024<=64ULL*1024*1024 &&
               f.raw_U.numel()==f.raw_ref.numel() && f.raw_U.numel()==f.raw_v.numel() && f.raw_U.numel()==f.raw_jvp.numel()) {
                raw_written=write_npy("linear_delay_u_eval.npy",f.raw_U) &&
                    write_npy("linear_delay_u_ref_first_jvp.npy",f.raw_ref) &&
                    write_npy("linear_delay_first_tangent.npy",f.raw_v) &&
                    write_npy("linear_delay_first_jvp.npy",f.raw_jvp);
            }
            if(!raw_written || !f.sample_done || i.ordinal!=5 || !f.have_base ||
               !f.have_first_ref || !f.have_last_ref || !eos || !eos->captured || eos->failed || eos->duplicate)
                f.invalid=true;
        }
        const bool eligible=clock_valid && !f.invalid && !f.nested && (!f.selected || context_valid);
        std::ostringstream line; line << std::setprecision(17) << "[LINEAR_DELAY] {\"stage\":" << f.stage << ",\"newton\":" << f.newton
            << ",\"timestep\":" << i.timestep << ",\"solver_id\":" << i.solver_id
            << ",\"generation\":" << i.generation << ",\"solve_ordinal\":" << i.ordinal
            << ",\"clock\":\"steady_and_getrusage_self_process_cpu\",\"clock_valid\":" << (clock_valid?"true":"false")
            << ",\"timing_eligible\":" << (eligible?"true":"false")
            << ",\"selected_stage4_newton0\":" << (f.selected?"true":"false")
            << ",\"wall_ms\":" << wall << ",\"process_user_ms\":" << user << ",\"process_system_ms\":" << sys
            << ",\"minor_faults\":" << f.linear_end.usage.ru_minflt-f.linear_begin.usage.ru_minflt
            << ",\"major_faults\":" << f.linear_end.usage.ru_majflt-f.linear_begin.usage.ru_majflt
            << ",\"voluntary_switches\":" << f.linear_end.usage.ru_nvcsw-f.linear_begin.usage.ru_nvcsw
            << ",\"involuntary_switches\":" << f.linear_end.usage.ru_nivcsw-f.linear_begin.usage.ru_nivcsw
            << ",\"A_calls\":" << f.a_calls << ",\"A_wall_ms\":" << f.a_ms
            << ",\"M_wrapped_calls\":" << f.m_calls << ",\"M_wrapped_wall_ms\":" << f.m_ms
            << ",\"A_M_overlap\":" << (f.overlap?"true":"false")
            << ",\"nested_scope\":" << (f.nested?"true":"false")
            << ",\"refinement_passes\":" << f.refinement_passes
            << ",\"nonoverlap_remainder_ms\":";
        if(disjoint)line << remainder;else line << "null";
        line << ",\"JVP_calls_in_linear\":" << f.jvp_calls << ",\"JVP_wall_ms\":" << f.jvp_ms
            << ",\"JVP_process_user_ms\":" << f.jvp_user_ms << ",\"JVP_process_system_ms\":" << f.jvp_system_ms
            << ",\"selected_ad_calls_outer\":" << i.selected_ad_calls << ",\"selected_fd_calls_outer\":" << i.selected_fd_calls
            << ",\"internal_fwad_to_fd_fallbacks\":" << i.fallback_delta
            << ",\"sampled_first_arnoldi_fallbacks\":" << f.sampled_fallbacks
            << ",\"rhs_calls_through_linear_end\":" << f.rhs_all << ",\"rhs_calls_linear\":" << f.rhs_linear
            << ",\"rhs_fast_calls_through_linear_end\":" << f.rhs_fast << ",\"rhs_mode_order\":\"full,implicit,explicit\",\"rhs_modes_through_linear_end\":["
            << f.rhs_modes[0] << ',' << f.rhs_modes[1] << ',' << f.rhs_modes[2] << "],\"linear_rhs_modes\":["
            << f.linear_rhs_modes[0] << ',' << f.linear_rhs_modes[1] << ',' << f.linear_rhs_modes[2] << ']'
            << ",\"capture_clone_ms_included\":" << f.capture_ms
            << ",\"pre_linear_clone_ms\":" << f.capture_pre_ms
            << ",\"sample_phase\":\"first_arnoldi_restart0_j0\",\"sample_captured\":" << (f.sample_done?"true":"false")
            << ",\"raw_npy_written\":" << (raw_written?"true":"false")
            << ",\"raw_files\":\"linear_delay_u_eval.npy,linear_delay_u_ref_first_jvp.npy,linear_delay_first_tangent.npy,linear_delay_first_jvp.npy\""
            << ",\"torch_intra_threads\":" << at::get_num_threads() << ",\"torch_inter_threads\":" << at::get_num_interop_threads()
            << ",\"iterations\":" << i.iterations << ",\"restarts\":" << i.restarts
            << ",\"restart_budget\":" << i.restart_budget << ",\"max_restarts\":" << i.max_restarts
            << ",\"tolerance\":" << i.tolerance << ",\"success\":" << (i.success?"true":"false")
            << ",\"native_preconditioner_calls\":" << i.m_native_calls << ",\"guard_events\":" << i.guard_events
            << ",\"dt\":" << i.dt << ",\"gamma\":" << i.gamma;
        if(i.layout) {
            line << ",\"layout\":[";for(size_t j=0;j<i.layout->blocks.size();++j){if(j)line << ',';const auto& b=i.layout->blocks[j];
                line << "{\"name\":\"" << b.name << "\",\"start\":" << b.start << ",\"size\":" << b.size << '}';}line << ']';
        }
        line << extra.str() << "}\n";
        output << line.str();
    } catch (...) {
        try { output << "[LINEAR_DELAY] {\"stage\":" << f.stage << ",\"newton\":" << f.newton
                     << ",\"status\":\"observer_failed\",\"timing_eligible\":false}\n"; } catch (...) {}
    }
}
}
#endif
