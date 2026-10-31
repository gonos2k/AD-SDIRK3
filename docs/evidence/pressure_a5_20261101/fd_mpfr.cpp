#include <mpfr.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
using std::size_t; constexpr auto R=MPFR_RNDN;
struct Data {std::vector<int64_t> dims;std::vector<double> x;};
using Records=std::map<std::string,Data>;
void require(bool ok,const char* s){if(!ok)throw std::runtime_error(s);}
template<class T> T read(std::ifstream& f){T x;f.read(reinterpret_cast<char*>(&x),sizeof x);require(bool(f),"truncated");return x;}
Records load(const char* path){std::ifstream f(path,std::ios::binary);require(bool(f),"open failed");char magic[8];f.read(magic,8);require(bool(f)&&std::memcmp(magic,"SDIRKA53",8)==0,"bad magic");auto n=read<uint64_t>(f);require(n<1000,"record count");Records out;for(uint64_t a=0;a<n;a++){char name[32];f.read(name,32);require(bool(f)&&std::memchr(name,0,32),"bad name");auto sz=read<uint32_t>(f),nd=read<uint32_t>(f);require((sz==4||sz==8)&&nd<=4,"bad dtype/dims");int64_t d[4];for(auto& v:d)v=read<int64_t>(f);auto bytes=read<uint64_t>(f);for(unsigned k=nd;k<4;k++)require(d[k]==0,"nonzero unused dimension");uint64_t count=1;for(unsigned k=0;k<nd;k++){require(d[k]>0&&count<=UINT64_MAX/static_cast<uint64_t>(d[k]),"bad dimension");count*=d[k];}require(count<=UINT64_MAX/sz&&bytes==count*sz&&bytes<1000000000,"bad byte count");Data rec;rec.dims.assign(d,d+nd);rec.x.resize(count);for(auto& v:rec.x){v=sz==4?static_cast<double>(read<float>(f)):read<double>(f);require(std::isfinite(v),"nonfinite input");}require(out.emplace(name,std::move(rec)).second,"duplicate record");}require(f.peek()==std::ifstream::traits_type::eof(),"trailing bytes");return out;}
const Data& get(const Records& r,const char* n,std::initializer_list<int64_t> dims){auto it=r.find(n);require(it!=r.end(),"missing record");require(it->second.dims==std::vector<int64_t>(dims),"unexpected dimensions");return it->second;}
constexpr int ny=81,nx=41,nz=64,nv=82,nw=65;constexpr size_t su=217728,sv=215168,sw=215865,st=212544,sm=3321,total=su+sv+2*sw+st+sm;static_assert(total==1080491);
size_t m2(int j,int i){return size_t(j)*nx+i;}size_t m3(int j,int k,int i){return (size_t(j)*nz+k)*nx+i;}size_t w3(int j,int k,int i){return (size_t(j)*nw+k)*nx+i;}size_t v3(int j,int k,int i){return (size_t(j)*nz+k)*nx+i;}
struct Inputs {const Data &ph,&t,&mu,&mb,&alb,&pb,&c1,&c2,&rdnw,&vx,&vy,&cq,&cons,&z,&fw;Inputs(const Records& a,const Records& b):ph(get(a,"eos_ph",{ny,nw,nx})),t(get(a,"eos_t",{ny,nz,nx})),mu(get(a,"eos_mu",{ny,nx})),mb(get(a,"mu_base",{ny,nx})),alb(get(a,"alb",{ny,nz,nx})),pb(get(a,"p_base",{ny,nz,nx})),c1(get(a,"c1h",{nz})),c2(get(a,"c2h",{nz})),rdnw(get(a,"rdnw",{nz})),vx(get(a,"msfvx",{nv,nx})),vy(get(a,"msfvy",{nv,nx})),cq(get(a,"cqv_at_v",{nv,nz,nx})),cons(get(a,"constants",{9})),z(get(b,"Qz_physical",{total})),fw(get(b,"V_Jfwad",{nv,nz,nx})){} };
struct Eval {
 mpfr_t x[100];const Inputs& d;long long evaluations=0;
 Eval(const Inputs& in,int bits):d(in){for(auto& a:x)mpfr_init2(a,bits);}
 ~Eval(){for(auto& a:x)mpfr_clear(a);}
 void set(int a,double v){mpfr_set_d(x[a],v,R);}void add(int a,int b,int c){mpfr_add(x[a],x[b],x[c],R);}void sub(int a,int b,int c){mpfr_sub(x[a],x[b],x[c],R);}void mul(int a,int b,int c){mpfr_mul(x[a],x[b],x[c],R);}void div(int a,int b,int c){mpfr_div(x[a],x[b],x[c],R);}void pos(int a){require(mpfr_number_p(x[a])&&mpfr_sgn(x[a])>0,"source positive branch failed");}
 // Row result: ph bottom/top (base+direction*h), mu, al, alt, pressure perturbation.
 void row(int j,int k,int i,int h,int dest){size_t mi=m2(j,i),ti=m3(j,k,i),lo=w3(j,k,i),hi=w3(j,k+1,i);const size_t po=su+sv+sw,to=po+sw,mo=to+st;
  set(0,d.ph.x[lo]);set(1,d.z.x[po+lo]);mul(1,1,h);add(dest,0,1);
  set(0,d.ph.x[hi]);set(1,d.z.x[po+hi]);mul(1,1,h);add(dest+1,0,1);
  set(0,d.mu.x[mi]);set(1,d.z.x[mo+mi]);mul(1,1,h);add(dest+2,0,1);
  set(0,d.mb.x[mi]);add(2,0,dest+2);pos(2);set(3,d.c1.x[k]);set(4,d.c2.x[k]);mul(5,3,2);add(5,5,4);pos(5);
  set(6,d.alb.x[ti]);mul(7,6,3);mul(7,7,dest+2);sub(8,dest+1,dest);set(9,d.rdnw.x[k]);mul(8,9,8);sub(7,7,8);mpfr_neg(x[7],x[7],R);div(dest+3,7,5);add(dest+4,dest+3,6);pos(dest+4);
  set(0,d.t.x[ti]);set(1,d.z.x[to+ti]);mul(1,1,h);add(0,0,1);set(1,d.cons.x[4]);add(0,0,1);pos(0);set(1,d.cons.x[0]);mul(0,1,0);set(1,d.cons.x[3]);mul(2,1,dest+4);pos(2);div(0,0,2);pos(0);set(2,d.cons.x[2]/d.cons.x[1]);mpfr_pow(x[0],x[0],x[2],R);mul(0,1,0);set(1,d.pb.x[ti]);sub(dest+5,0,1);
 }
 void value(int j,int k,int i,double h,mpfr_ptr out){++evaluations;set(98,h);row(j-1,k,i,98,10);row(j,k,i,98,20);
  // phi top difference plus bottom difference, EOS alpha and pressure differences.
  sub(30,21,11);sub(31,20,10);add(30,30,31);add(31,24,14);sub(32,25,15);mul(31,31,32);add(30,30,31);add(31,23,13);set(32,d.pb.x[m3(j,k,i)]);set(33,d.pb.x[m3(j-1,k,i)]);sub(32,32,33);mul(31,31,32);add(30,30,31);
  // Recompute total layer mass and physical velocity mass from perturbed inputs.
  set(31,d.mb.x[m2(j-1,i)]);add(32,12,31);set(31,d.mb.x[m2(j,i)]);add(33,22,31);add(32,32,33);mpfr_div_ui(x[32],x[32],2,R);set(31,d.c1.x[k]);mul(32,31,32);set(31,d.c2.x[k]);add(32,32,31);pos(32);
  add(33,12,22);mpfr_div_ui(x[33],x[33],2,R);set(34,d.mb.x[m2(j-1,i)]);set(35,d.mb.x[m2(j,i)]);add(34,34,35);mpfr_div_ui(x[34],x[34],2,R);add(33,33,34);set(34,d.cons.x[7]);add(33,33,34);pos(33);
  set(34,d.vx.x[m2(j,i)]);set(35,d.vy.x[m2(j,i)]);require(std::abs(d.vx.x[m2(j,i)])>=static_cast<double>(1.0e-10f),"map floor");pos(34);pos(35);require(d.vx.x[m2(j,i)]==1.0 && d.vy.x[m2(j,i)]==1.0,"nonunit map");div(35,35,34);mul(35,35,32);set(36,d.cons.x[6]);mpfr_div_ui(x[36],x[36],2,R);mul(35,36,35);mul(35,35,30);set(36,d.cq.x[v3(j,k,i)]);require(d.cq.x[v3(j,k,i)]==1.0,"nonunit cq");mpfr_neg(x[36],x[36],R);mul(35,36,35);div(34,32,34);pos(34);div(36,33,34);mul(35,35,36);mpfr_div(out,x[35],x[33],R);require(mpfr_number_p(out),"nonfinite output");
 }
 void derivative(int j,int k,int i,double h,mpfr_ptr result){value(j,k,i,h,x[96]);value(j,k,i,-h,x[97]);sub(96,96,97);set(95,h);mpfr_mul_ui(x[95],x[95],2,R);mpfr_div(result,x[96],x[95],R);}
};
int main(int argc,char** argv){try{require(argc==3,"usage: fd_mpfr direction actions");auto a=load(argv[1]);auto b=load(argv[2]);Inputs in(a,b);
 const auto& flags=get(a,"flags",{8});const double expected_flags[8]={1,0,0,1,1,1,1,1};for(int i=0;i<8;i++)require(flags.x[i]==expected_flags[i],"captured flags mismatch");
 for(int i:{0,1,2,3,4,5,6,7,8})require(in.cons.x[i]>0,"nonpositive physical constant");for(double r:in.rdnw.x)require(r>0,"nonpositive rdnw");
 const auto& u=get(a,"U_eval",{total});const auto& zscaled=get(a,"z_scaled",{total});const auto& scale=get(a,"S_diag",{total});const size_t po=su+sv+sw,to=po+sw,mo=to+st;
 for(int j=0;j<80;j++)for(int i=0;i<40;i++){size_t m=m2(j,i);require(u.x[mo+m]==in.mu.x[m] && scale.x[mo+m]*zscaled.x[mo+m]==in.z.x[mo+m],"mu projection/scale mismatch");for(int k=0;k<64;k++){size_t t=m3(j,k,i);require(u.x[to+t]==in.t.x[t] && scale.x[to+t]*zscaled.x[to+t]==in.z.x[to+t],"t projection/scale mismatch");}for(int k=0;k<65;k++){size_t w=w3(j,k,i);require(u.x[po+w]==in.ph.x[w] && scale.x[po+w]*zscaled.x[po+w]==in.z.x[po+w],"phi projection/scale mismatch");}}
 std::cout<<"scope=160bit_real_formula_not_native_FP64 captured_flags_projection_scale=passed\n";std::set<std::array<int,3>> samples;for(int j:{1,20,40,60,79})for(int k:{0,31,63})for(int i:{0,20,39})samples.insert({j,k,i});double max=-1,min=std::numeric_limits<double>::infinity();std::array<int,3> imax{},imin{};for(int j=1;j<=79;j++)for(int k=0;k<64;k++)for(int i=0;i<40;i++){double v=std::abs(in.fw.x[v3(j,k,i)]);if(v>max){max=v;imax={j,k,i};}if(v>0&&v<min){min=v;imin={j,k,i};}}require(std::isfinite(min),"no nonzero fwad");samples.insert(imax);samples.insert(imin);std::cout<<std::setprecision(17)<<"samples="<<samples.size()<<" max_idx="<<imax[0]<<","<<imax[1]<<","<<imax[2]<<" max="<<max<<" min_idx="<<imin[0]<<","<<imin[1]<<","<<imin[2]<<" min="<<min<<"\n";for(auto s:samples)std::cout<<"sample "<<s[0]<<" "<<s[1]<<" "<<s[2]<<"\n";std::cout.flush();
 Eval e160(in,160),e256(in,256);mpfr_t d160,d256,delta,limit,t;mpfr_init2(d160,160);for(auto p:{&d256,&delta,&limit,&t})mpfr_init2(*p,256);std::array<double,3> hs{0.01,0.001,0.0001};std::array<uint64_t,3> fail{},stability{};std::array<double,3> maxerr{},sum2{},maxstability{};std::array<std::array<int,3>,3> arg{};
 for(int j=1;j<=79;j++)for(int k=0;k<64;k++)for(int i=0;i<40;i++){double ref=in.fw.x[v3(j,k,i)];for(int h=0;h<3;h++){e160.derivative(j,k,i,hs[h],d160);double actual=mpfr_get_d(d160,R),err=std::abs(actual-ref);if(err>1e-12+1e-5*std::abs(ref))fail[h]++;sum2[h]+=err*err;if(err>maxerr[h]){maxerr[h]=err;arg[h]={j,k,i};}if(samples.count({j,k,i})){e256.derivative(j,k,i,hs[h],d256);mpfr_sub(delta,d160,d256,R);mpfr_abs(delta,delta,R);mpfr_abs(t,d256,R);mpfr_set_d(limit,1e-12,R);mpfr_mul(limit,limit,t,R);mpfr_set_d(t,1e-20,R);mpfr_add(limit,limit,t,R);if(mpfr_cmp(delta,limit)>0)stability[h]++;maxstability[h]=std::max(maxstability[h],mpfr_get_d(delta,R));}}}
 for(int h=0;h<3;h++)std::cout<<"width="<<hs[h]<<" failing="<<fail[h]<<" count="<<79*64*40<<" max_abs="<<maxerr[h]<<" max_idx="<<arg[h][0]<<","<<arg[h][1]<<","<<arg[h][2]<<" l2="<<std::sqrt(sum2[h])<<" sample_stability_failing="<<stability[h]<<" sample_max_abs="<<maxstability[h]<<"\n";std::cout<<"evaluations160="<<e160.evaluations<<" evaluations256="<<e256.evaluations<<" branch_checks=passed\n";for(auto p:{&d160,&d256,&delta,&limit,&t})mpfr_clear(*p);return 0;}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
