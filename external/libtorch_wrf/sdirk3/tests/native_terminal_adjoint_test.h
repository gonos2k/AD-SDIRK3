#pragma once

#ifdef WRF_SDIRK3_NATIVE_TERMINAL_TEST

#include <torch/torch.h>

namespace wrf::sdirk3::native_terminal_test {

// Called from the production bridge only in the separately compiled test build.
void capture_gradient64(const torch::Tensor& gradient);
void capture_theta_omega(const torch::Tensor& omega);

}  // namespace wrf::sdirk3::native_terminal_test

extern "C" {
int sdirk3_native_terminal_prepare(float* packed_x0, int n,
                                  int nx, int ny, int nz,
                                  int nx_u, int ny_v, int nz_w,
                                  float* direction, float* terminal,
                                  int* mode);
int sdirk3_native_terminal_before_step(int host_step);
int sdirk3_native_terminal_after_step(int host_step);
int sdirk3_native_terminal_record_endpoint(const float* packed_terminal, int n);
int sdirk3_native_terminal_finish(const float* direction,
                                  const float* terminal,
                                  const float* initial_gradient_f32,
                                  int n, int mode, int have_gradient);
}

#endif  // WRF_SDIRK3_NATIVE_TERMINAL_TEST
