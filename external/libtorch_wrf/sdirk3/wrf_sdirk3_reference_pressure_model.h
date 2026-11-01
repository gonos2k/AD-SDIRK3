#pragma once

#include "wrf_sdirk3_reference_pressure.h"
#include <memory>
#include <vector>

namespace wrf::sdirk3 {
// Flat reference P0 only; every field is an independent physical CPU FP64
// degree of freedom. No WRF packed aliases, halo or boundary identity rows.
struct ReferencePressureFields {
    torch::Tensor u;     // [ny,nz,nx], west x face of each periodic mass cell
    torch::Tensor v;     // [ny-1,nz,nx], interior y faces; walls are zero
    torch::Tensor w;     // [ny,nz,nx], W levels 1..nz, dynamic top included
    torch::Tensor phi;   // [ny,nz,nx], Phi levels 1..nz; Phi0 fixed at zero
    torch::Tensor theta; // [ny,nz,nx], perturbation theta
    torch::Tensor mu;    // [ny,nx], column mass
};

struct ReferencePressureProfiles {
    std::vector<double> q, etheta, emu, gz_edge, alpha, beta, H, nu, dnw;
};

class ReferencePressureModel {
public:
    ReferencePressureModel(int64_t nx, int64_t ny, ReferencePressureProfiles profiles,
                           double a, double gravity, double dx, double dy);
    ReferencePressureFields solve(const ReferencePressureFields& b) const;
    ReferencePressureFields apply(const ReferencePressureFields& x) const;
private:
    friend class DirectPressureV;
    void check(const ReferencePressureFields& x) const;
    torch::Tensor divergence_x(const torch::Tensor& u) const;
    torch::Tensor divergence_y(const torch::Tensor& v) const;
    torch::Tensor divergence(const torch::Tensor& u, const torch::Tensor& v) const;
    torch::Tensor horizontal_x_pressure(const torch::Tensor& p) const;
    torch::Tensor horizontal_y_pressure(const torch::Tensor& p) const;
    std::pair<torch::Tensor,torch::Tensor> horizontal_pressure(const torch::Tensor& p) const;
    torch::Tensor vertical_pressure(const torch::Tensor& p) const;
    torch::Tensor ephi(const torch::Tensor& phi) const;
    torch::Tensor inverse_ephi(const torch::Tensor& pressure) const;
    torch::Tensor pressure(const ReferencePressureFields& x) const;
    torch::Tensor cmu(const torch::Tensor& div) const;
    torch::Tensor pressure_row(const torch::Tensor& p, const torch::Tensor& div,
                               const torch::Tensor& w) const;
    struct Thermodynamics { torch::Tensor phi, theta, mu; };
    Thermodynamics recover_thermo(const torch::Tensor& p, const torch::Tensor& div,
                                  const ReferencePressureFields& b) const;
    ReferencePressureFields recover_pv(const torch::Tensor& p, const torch::Tensor& v,
                                       const ReferencePressureFields& b, torch::Tensor* div_out=nullptr) const;
    int64_t nx_,ny_,nz_;
    double a_,g_,dx_,dy_;
    torch::Tensor q_,et_,em_,edge_,alpha_,beta_,H_,nu_,dnw_;
    std::unique_ptr<ReferencePressure> schur_;
};
}
