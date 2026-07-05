#ifndef TORCH_MINIMAL_H
#define TORCH_MINIMAL_H

// Minimal torch includes for CPU-only usage without Python
#include <ATen/ATen.h>
#include <torch/csrc/autograd/variable.h>
#include <torch/csrc/autograd/function.h>
#include <torch/csrc/autograd/edge.h>
#include <torch/csrc/autograd/grad_mode.h>
#include <torch/csrc/autograd/custom_function.h>

// Define torch namespace aliases
namespace torch {
    using Tensor = at::Tensor;
    using TensorOptions = at::TensorOptions;
    
    // Indexing
    namespace indexing {
        using Slice = at::indexing::Slice;
        using None = at::indexing::None;
        using Ellipsis = at::indexing::Ellipsis;
    }
    
    // Autograd functions we need
    namespace autograd {
        using Variable = torch::autograd::Variable;
        using AutogradContext = torch::autograd::AutogradContext;
        using Function = torch::autograd::Function;
        
        // Manual grad function declaration
        inline std::vector<torch::Tensor> grad(
            const std::vector<torch::Tensor>& outputs,
            const std::vector<torch::Tensor>& inputs,
            const std::vector<torch::Tensor>& grad_outputs = {},
            bool retain_graph = false,
            bool create_graph = false,
            bool allow_unused = false) {
            
            return torch::autograd::grad(
                {outputs}, {inputs}, {grad_outputs}, 
                retain_graph, create_graph, allow_unused
            );
        }
    }
    
    // NN Module stub for compatibility
    namespace nn {
        class Module {
        public:
            virtual ~Module() = default;
        };
    }
}

// Common functions
inline torch::Tensor torch_zeros(at::IntArrayRef sizes, const at::TensorOptions& options = {}) {
    return at::zeros(sizes, options);
}

inline torch::Tensor torch_ones(at::IntArrayRef sizes, const at::TensorOptions& options = {}) {
    return at::ones(sizes, options);
}

inline torch::Tensor torch_empty(at::IntArrayRef sizes, const at::TensorOptions& options = {}) {
    return at::empty(sizes, options);
}

#endif // TORCH_MINIMAL_H