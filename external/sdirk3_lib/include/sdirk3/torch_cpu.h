#ifndef TORCH_CPU_H
#define TORCH_CPU_H

// Prevent Python.h inclusion
#define TORCH_API_INCLUDE_EXTENSION_H 0

// Include only the C++ API parts we need
#include <ATen/ATen.h>
#include <torch/csrc/api/include/torch/types.h>
#include <torch/csrc/api/include/torch/nn/module.h>
#include <torch/csrc/autograd/autograd.h>
#include <torch/csrc/autograd/variable.h>
#include <torch/csrc/autograd/function.h>
#include <torch/csrc/autograd/saved_variable.h>
#include <torch/csrc/autograd/custom_function.h>

// Bring common types into torch namespace
namespace torch {
    using Tensor = at::Tensor;
    using TensorOptions = at::TensorOptions;
    using ScalarType = at::ScalarType;
    using Device = at::Device;
    using DeviceType = at::DeviceType;
    using IntArrayRef = at::IntArrayRef;
    
    // Constants
    constexpr auto kCPU = at::kCPU;
    constexpr auto kFloat32 = at::kFloat;
    constexpr auto kFloat64 = at::kDouble;
    constexpr auto kInt32 = at::kInt;
    constexpr auto kInt64 = at::kLong;
    
    // Indexing
    namespace indexing {
        using Slice = at::indexing::Slice;
        using TensorIndex = at::indexing::TensorIndex;
    }
    
    // Functions
    inline Tensor zeros(IntArrayRef sizes, const TensorOptions& options = {}) {
        return at::zeros(sizes, options);
    }
    
    inline Tensor ones(IntArrayRef sizes, const TensorOptions& options = {}) {
        return at::ones(sizes, options);
    }
    
    inline Tensor empty(IntArrayRef sizes, const TensorOptions& options = {}) {
        return at::empty(sizes, options);
    }
    
    inline Tensor zeros_like(const Tensor& self, const TensorOptions& options = {}) {
        return at::zeros_like(self, options);
    }
    
    inline Tensor ones_like(const Tensor& self, const TensorOptions& options = {}) {
        return at::ones_like(self, options);
    }
    
    inline Tensor empty_like(const Tensor& self, const TensorOptions& options = {}) {
        return at::empty_like(self, options);
    }
    
    // FIX 2025-12-31 Batch38 Issue 5: Force CPU device for from_blob with host pointers
    // from_blob() with host pointers MUST use CPU device - this wrapper enforces it.
    // If caller passes options without device, CPU is forced. If caller passes different
    // device, it's overridden to CPU (host pointer can only be wrapped as CPU tensor).
    inline Tensor from_blob(void* data, IntArrayRef sizes, const TensorOptions& options = {}) {
        auto cpu_options = options.device(kCPU);
        return at::from_blob(data, sizes, cpu_options);
    }

    // Explicit CPU-only version for clarity (alias to standard from_blob with CPU enforcement)
    inline Tensor from_blob_cpu(void* data, IntArrayRef sizes, const TensorOptions& options = {}) {
        return from_blob(data, sizes, options);  // Already enforces CPU
    }
    
    inline Tensor norm(const Tensor& self, c10::optional<at::Scalar> p = c10::nullopt) {
        return at::norm(self, p);
    }
}

#endif // TORCH_CPU_H