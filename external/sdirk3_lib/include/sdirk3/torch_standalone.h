#ifndef TORCH_STANDALONE_H
#define TORCH_STANDALONE_H

// Prevent Python.h dependency
#ifndef Py_PYTHON_H
#define Py_PYTHON_H
typedef struct _object PyObject;
#endif

// Include core ATen functionality
#include <ATen/ATen.h>

// Include minimal torch autograd headers
#include <torch/csrc/autograd/variable.h>
#include <torch/csrc/autograd/function.h>
#include <torch/csrc/autograd/autograd.h>

// Core torch namespace
namespace torch {
    // Re-export ATen types
    using Tensor = at::Tensor;
    using TensorOptions = at::TensorOptions;
    using Scalar = at::Scalar;
    using ScalarType = at::ScalarType;
    using Device = at::Device;
    using DeviceType = at::DeviceType;
    using IntArrayRef = at::IntArrayRef;
    
    // Re-export tensor creation functions
    using at::tensor;
    using at::empty;
    using at::zeros;
    using at::ones;
    using at::full;
    using at::arange;
    // Note: from_blob is NOT re-exported directly - use wrapper below for CPU safety

    // FIX Round194: Force CPU device for from_blob with host pointers
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

    // Re-export math functions
    using at::add;
    using at::sub;
    using at::mul;
    using at::div;
    using at::matmul;
    using at::dot;
    using at::norm;
    using at::sum;
    using at::mean;
    using at::cat;
    using at::stack;
    using at::isnan;
    using at::any;
    using at::zeros_like;
    using at::ones_like;
    
    // Indexing
    namespace indexing {
        using at::indexing::Slice;
        using at::indexing::None;
        using at::indexing::Ellipsis;
    }
    
    // Thread management
    inline void set_num_threads(int num_threads) {
        // Stub implementation
    }
    
    inline int get_num_threads() {
        return 1;
    }
    
    // Minimal nn module definition for compatibility
    namespace nn {
        class Module {
        public:
            Module(const std::string& name = "Module") : name_(name) {}
            virtual ~Module() = default;
            
            virtual at::Tensor forward(const at::Tensor& input) {
                throw std::runtime_error("forward() not implemented");
            }
            
        protected:
            std::string name_;
        };
    }
    
    // CUDA namespace
    namespace cuda {
        inline bool is_available() {
            return false;  // CPU only
        }
    }
}

// Scalar type constants in torch namespace
namespace torch {
    constexpr auto kFloat32 = at::ScalarType::Float;
    constexpr auto kFloat64 = at::ScalarType::Double;
    constexpr auto kInt32 = at::ScalarType::Int;
    constexpr auto kInt64 = at::ScalarType::Long;
    constexpr auto kBool = at::ScalarType::Bool;
    constexpr auto kCPU = at::kCPU;
    constexpr auto kCUDA = at::kCUDA;
}

#endif // TORCH_STANDALONE_H