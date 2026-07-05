#ifndef TORCH_STANDALONE_H
#define TORCH_STANDALONE_H

// LibTorch standalone header for WRF SDIRK3
// Provides unified torch includes

#include <torch/torch.h>
#include <torch/script.h>

// Common typedefs for WRF integration
namespace wrf {
namespace sdirk3 {

using TensorOptions = torch::TensorOptions;
using Tensor = torch::Tensor;

} // namespace sdirk3
} // namespace wrf

#endif // TORCH_STANDALONE_H