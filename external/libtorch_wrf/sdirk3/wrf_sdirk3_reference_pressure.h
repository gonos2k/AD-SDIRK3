#pragma once

#include <torch/torch.h>
#include <vector>

// Standalone flat unit-map reference, not the native WRF pressure Jacobian.
// Physical pressure tensors are CPU float64, in (j,k,i) order.
namespace wrf::sdirk3 {
class ReferencePressure {
 public:
  ReferencePressure(int64_t nx, int64_t ny, const std::vector<double>& q,
                    const std::vector<double>& aedge,
                    const std::vector<double>& horizontal_coeff,
                    double a, double gravity, double dx, double dy);
  torch::Tensor solve(const torch::Tensor& rhs) const;
  torch::Tensor apply_operator(const torch::Tensor& p) const;
 private:
  void check_field(const torch::Tensor& p) const;
  int64_t nx_, ny_, nz_;
  double a2g_, a2_, dx2_, dy2_;
  std::vector<double> q_, edge_, horizontal_;
  torch::Tensor cosine_;
  std::vector<torch::Tensor> inverse_pivot_, upper_factor_;
};
}
