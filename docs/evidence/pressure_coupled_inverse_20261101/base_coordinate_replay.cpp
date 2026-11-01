#include "wrf_sdirk3_reference_pressure_model.h"
#include "wrf_sdirk3_pressure_packed_adapter.h"
#include <torch/torch.h>
#include <fstream>
#include <map>
#include <cstring>
#include <iostream>
#include <iomanip>
using namespace wrf::sdirk3;
std::map<std::string,torch::Tensor> read(const char* path) {
 std::ifstream f(path,std::ios::binary);char magic[8];uint64_t count;f.read(magic,8);f.read((char*)&count,8);TORCH_CHECK(f && !std::memcmp(magic,"SDIRKA53",8));
 std::map<std::string,torch::Tensor> out;
 for(uint64_t i=0;i<count;i++){char name[32];uint32_t size,nd;int64_t dims[4];uint64_t bytes;
 f.read(name,32);f.read((char*)&size,4);f.read((char*)&nd,4);f.read((char*)dims,32);f.read((char*)&bytes,8);
 TORCH_CHECK(f && nd<=4 && (size==4||size==8));std::vector<int64_t> shape(dims,dims+nd);
 auto t=torch::empty(shape,torch::TensorOptions().dtype(size==8?torch::kFloat64:torch::kFloat32));TORCH_CHECK(bytes==t.numel()*size);
 f.read((char*)t.data_ptr(),bytes);TORCH_CHECK(f);out.emplace(std::string(name),t); }
 return out;
}
bool same(const torch::Tensor&a,const torch::Tensor&b){auto x=a.contiguous(),y=b.contiguous();return x.sizes()==y.sizes()&&x.scalar_type()==y.scalar_type()&&!memcmp(x.const_data_ptr(),y.const_data_ptr(),x.nbytes());}
int main(int argc,char** argv) {TORCH_CHECK(argc==3);torch::set_num_threads(1);auto c=read(argv[1]),d=read(argv[2]);
 auto v=c.at("v_scaled"),s=c.at("S_diag"),constants=c.at("constants");
 auto vec=[&](const char* n){auto t=c.at(n).to(torch::kFloat64).contiguous();auto p=t.data_ptr<double>();return std::vector<double>(p,p+t.numel());};
 auto t=c.at("eos_t");auto ny=t.size(0),nz=t.size(1),nx=t.size(2);
 ReferencePressureProfiles p{vec("profile_q"),vec("profile_etheta"),vec("profile_emu"),vec("profile_gz_edge"),vec("profile_alpha"),vec("profile_beta"),vec("profile_H"),vec("profile_nu"),vec("profile_dnw")};
 auto a=constants[8].item<double>(),rdx=constants[5].item<double>(),rdy=constants[6].item<double>();
 ReferencePressureModel model(nx-1,ny-1,p,a,static_cast<double>(9.81f),1.0/rdx,1.0/rdy);
 PressurePackedAdapter adapter(nx-1,ny-1,nz);auto rhs=s*v;auto base=adapter.solve(model,rhs);auto control=s.reciprocal()*base;auto candidate=base/s;
 auto z=c.at("z_scaled"),observed=d.at("base_z_scaled");auto mismatch=[](const torch::Tensor&a,const torch::Tensor&b){return (a!=b).sum().item<int64_t>();};
 std::cout<<std::setprecision(17)<<"{\"control_bytes_equal\":"<<(same(control,z)?"true":"false")<<",\"candidate_base_bytes_equal\":"<<(same(candidate,observed)?"true":"false")<<",\"control_numeric_mismatches\":"<<mismatch(control,z)<<",\"candidate_numeric_mismatches\":"<<mismatch(candidate,observed)<<",\"control_max_abs\":"<<(control-z).abs().max().item<double>()<<",\"candidate_max_abs\":"<<(candidate-observed).abs().max().item<double>()<<",\"gravity\":"<<static_cast<double>(9.81f)<<",\"a\":"<<a<<",\"threads\":1}"<<std::endl;
 return same(control,z)&&same(candidate,observed)?0:1;
}
