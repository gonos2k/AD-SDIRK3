/**
 * @file test_select_autograd.cpp
 * @brief LibTorch select()/slice() autograd compatibility tests
 *
 * IMPORTANT PATTERN NOTE (2025-12-31 Batch41):
 * =============================================
 * In modern LibTorch, the following patterns are NO-OPs (do nothing):
 *   - tensor.select(dim, idx) = value;   // BROKEN - use .copy_() or .fill_()
 *   - tensor.slice(dim, s, e) = value;   // BROKEN - use .copy_()
 *
 * CORRECT patterns:
 *   - tensor.select(dim, idx).copy_(value);     // For tensor values
 *   - tensor.select(dim, idx).fill_(scalar);    // For scalar values
 *   - tensor.slice(dim, s, e).copy_(value);     // For tensor values
 *
 * PERIODIC CHECK:
 * Run ./test/check_libtorch_patterns.sh to verify no broken patterns exist.
 * This script should be run:
 *   - Before each commit (pre-commit hook)
 *   - As part of CI pipeline
 *   - After merging external LibTorch code
 */

#include <torch/torch.h>
#include <iostream>

// Test if select() preserves gradient flow
void test_select_gradient_flow() {
    std::cout << "\n=== Testing select() gradient flow ===" << std::endl;
    
    // Create a tensor that requires gradient
    torch::Tensor x = torch::randn({3, 4, 5}, torch::requires_grad(true));
    
    // Apply select operation
    torch::Tensor y = x.select(1, 2);  // Select index 2 along dimension 1
    
    // Create a target and loss
    torch::Tensor target = torch::randn_like(y);
    torch::Tensor loss = torch::mse_loss(y, target);
    
    // Compute gradients
    loss.backward();
    
    // Check if gradients exist
    if (x.grad().defined()) {
        std::cout << "✅ Gradients flow through select() operation" << std::endl;
        std::cout << "   Input shape: " << x.sizes() << std::endl;
        std::cout << "   Output shape: " << y.sizes() << std::endl;
        std::cout << "   Gradient shape: " << x.grad().sizes() << std::endl;
        
        // Check if gradients are non-zero
        float grad_norm = x.grad().norm().item<float>();
        std::cout << "   Gradient norm: " << grad_norm << std::endl;
        
        if (grad_norm > 0) {
            std::cout << "✅ Gradients are non-zero" << std::endl;
        } else {
            std::cout << "❌ Gradients are zero!" << std::endl;
        }
    } else {
        std::cout << "❌ No gradients computed through select()!" << std::endl;
    }
}

// Test JVP with select operations
void test_jvp_with_select() {
    std::cout << "\n=== Testing JVP with select() ===" << std::endl;
    
    // Define a function that uses select
    auto F = [](const torch::Tensor& u) -> torch::Tensor {
        // Simulate operations similar to WRF code
        torch::Tensor result = torch::zeros_like(u);
        int nz = u.size(1);
        
        for (int k = 1; k < nz - 1; ++k) {
            // Use select to compute finite differences
            auto upper = u.select(1, k+1);
            auto lower = u.select(1, k-1);
            auto center = u.select(1, k);
            
            // FIX 2025-12-31 Batch41: select()= is no-op, use copy_()
            result.select(1, k).copy_((upper - 2.0f * center + lower) / 4.0f);
        }
        
        return result;
    };
    
    // Create test tensors
    torch::Tensor u = torch::randn({10, 20, 30}, torch::requires_grad(true));
    torch::Tensor v = torch::randn_like(u);
    
    // Compute function value
    torch::Tensor F_u = F(u);
    
    // Compute JVP using autograd
    auto grad_result = torch::autograd::grad(
        {F_u},
        {u},
        {v},
        /*retain_graph=*/true,
        /*create_graph=*/true,
        /*allow_unused=*/false
    );
    
    if (!grad_result.empty() && grad_result[0].defined()) {
        std::cout << "✅ JVP computation successful with select() operations" << std::endl;
        std::cout << "   JVP shape: " << grad_result[0].sizes() << std::endl;
        
        float jvp_norm = grad_result[0].norm().item<float>();
        std::cout << "   JVP norm: " << jvp_norm << std::endl;
        
        if (jvp_norm > 0) {
            std::cout << "✅ JVP is non-zero" << std::endl;
        } else {
            std::cout << "⚠️  JVP is zero (might be correct for specific inputs)" << std::endl;
        }
    } else {
        std::cout << "❌ JVP computation failed!" << std::endl;
    }
}

// Test select vs slice performance and gradient behavior
void test_select_vs_slice() {
    std::cout << "\n=== Comparing select() vs slice() ===" << std::endl;
    
    torch::Tensor x = torch::randn({100, 200, 300}, torch::requires_grad(true));
    
    // Test select
    auto start_select = std::chrono::high_resolution_clock::now();
    torch::Tensor y_select = x.select(1, 50);
    torch::Tensor loss_select = y_select.sum();
    loss_select.backward();
    auto end_select = std::chrono::high_resolution_clock::now();
    
    // Clear gradients
    x.mutable_grad().zero_();
    
    // Test slice (equivalent operation)
    auto start_slice = std::chrono::high_resolution_clock::now();
    torch::Tensor y_slice = x.slice(1, 50, 51).squeeze(1);
    torch::Tensor loss_slice = y_slice.sum();
    loss_slice.backward();
    auto end_slice = std::chrono::high_resolution_clock::now();
    
    auto select_time = std::chrono::duration_cast<std::chrono::microseconds>(end_select - start_select).count();
    auto slice_time = std::chrono::duration_cast<std::chrono::microseconds>(end_slice - start_slice).count();
    
    std::cout << "   select() time: " << select_time << " μs" << std::endl;
    std::cout << "   slice() time: " << slice_time << " μs" << std::endl;
    std::cout << "   select() is " << (slice_time > select_time ? "faster" : "slower") << std::endl;
    
    // Both should produce gradients
    std::cout << "   select() produces gradients: " << (x.grad().defined() ? "✅" : "❌") << std::endl;
}

// Test in-place operations with select
void test_inplace_select_operations() {
    std::cout << "\n=== Testing in-place operations with select() ===" << std::endl;
    
    torch::Tensor x = torch::randn({5, 10, 15}, torch::requires_grad(true));
    torch::Tensor y = torch::zeros_like(x);
    
    // Simulate WRF-style operations
    for (int k = 0; k < 10; ++k) {
        // This is similar to what's done in WRF code
        // FIX 2025-12-31 Batch41: select()= is no-op, use copy_()
        y.select(1, k).copy_(x.select(1, k) * 2.0f + 1.0f);
    }
    
    torch::Tensor loss = y.sum();
    
    try {
        loss.backward();
        if (x.grad().defined()) {
            std::cout << "✅ In-place select operations preserve gradient flow" << std::endl;
            std::cout << "   Gradient norm: " << x.grad().norm().item<float>() << std::endl;
        }
    } catch (const std::exception& e) {
        std::cout << "❌ Error with in-place operations: " << e.what() << std::endl;
    }
}

int main() {
    std::cout << "=== PyTorch select() Autograd Compatibility Test ===" << std::endl;
    std::cout << "PyTorch version: " << TORCH_VERSION << std::endl;
    
    // Run all tests
    test_select_gradient_flow();
    test_jvp_with_select();
    test_select_vs_slice();
    test_inplace_select_operations();
    
    std::cout << "\n=== Test Complete ===" << std::endl;
    return 0;
}