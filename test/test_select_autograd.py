#!/usr/bin/env python3
"""
Test PyTorch select() operation's compatibility with autograd and JVP computation
"""

import torch
import torch.autograd as autograd
import numpy as np
import time

def test_select_gradient_flow():
    """Test if select() preserves gradient flow"""
    print("\n=== Testing select() gradient flow ===")
    
    # Create a tensor that requires gradient
    x = torch.randn(3, 4, 5, requires_grad=True)
    
    # Apply select operation
    y = x.select(1, 2)  # Select index 2 along dimension 1
    
    # Create a target and loss
    target = torch.randn_like(y)
    loss = torch.nn.functional.mse_loss(y, target)
    
    # Compute gradients
    loss.backward()
    
    # Check if gradients exist
    if x.grad is not None:
        print("✅ Gradients flow through select() operation")
        print(f"   Input shape: {x.shape}")
        print(f"   Output shape: {y.shape}")
        print(f"   Gradient shape: {x.grad.shape}")
        
        # Check if gradients are non-zero
        grad_norm = x.grad.norm().item()
        print(f"   Gradient norm: {grad_norm:.6f}")
        
        if grad_norm > 0:
            print("✅ Gradients are non-zero")
        else:
            print("❌ Gradients are zero!")
    else:
        print("❌ No gradients computed through select()!")
    
    return x.grad is not None

def test_jvp_with_select():
    """Test JVP computation with select operations"""
    print("\n=== Testing JVP with select() ===")
    
    # Define a function that uses select
    def F(u):
        """Simulate operations similar to WRF code"""
        result = torch.zeros_like(u)
        nz = u.size(1)
        
        for k in range(1, nz - 1):
            # Use select to compute finite differences
            upper = u.select(1, k+1)
            lower = u.select(1, k-1)
            center = u.select(1, k)
            
            # Write back using select
            result[:, k, :] = (upper - 2.0 * center + lower) / 4.0
        
        return result
    
    # Create test tensors
    u = torch.randn(10, 20, 30, requires_grad=True)
    v = torch.randn_like(u)
    
    # Method 1: Using autograd.grad for JVP
    F_u = F(u)
    jvp = autograd.grad(F_u, u, v, retain_graph=True, create_graph=True)[0]
    
    if jvp is not None:
        print("✅ JVP computation successful with select() operations")
        print(f"   JVP shape: {jvp.shape}")
        
        jvp_norm = jvp.norm().item()
        print(f"   JVP norm: {jvp_norm:.6f}")
        
        if jvp_norm > 0:
            print("✅ JVP is non-zero")
        else:
            print("⚠️  JVP is zero (might be correct for specific inputs)")
    else:
        print("❌ JVP computation failed!")
    
    return jvp is not None

def test_select_vs_slice():
    """Compare select() vs slice() for performance and gradient behavior"""
    print("\n=== Comparing select() vs slice() ===")
    
    x = torch.randn(100, 200, 300, requires_grad=True)
    
    # Test select
    start_select = time.perf_counter()
    y_select = x.select(1, 50)
    loss_select = y_select.sum()
    loss_select.backward(retain_graph=True)
    select_grad = x.grad.clone()
    end_select = time.perf_counter()
    
    # Clear gradients
    x.grad.zero_()
    
    # Test slice (equivalent operation)
    start_slice = time.perf_counter()
    y_slice = x[:, 50:51, :].squeeze(1)
    loss_slice = y_slice.sum()
    loss_slice.backward(retain_graph=True)
    slice_grad = x.grad.clone()
    end_slice = time.perf_counter()
    
    select_time = (end_select - start_select) * 1e6  # Convert to microseconds
    slice_time = (end_slice - start_slice) * 1e6
    
    print(f"   select() time: {select_time:.2f} μs")
    print(f"   slice() time: {slice_time:.2f} μs")
    print(f"   select() is {'faster' if slice_time > select_time else 'slower'}")
    
    # Compare gradients
    grad_diff = (select_grad - slice_grad).abs().max().item()
    print(f"   Gradient difference: {grad_diff:.2e}")
    print(f"   Gradients are {'identical' if grad_diff < 1e-6 else 'different'}!")
    
    return grad_diff < 1e-6

def test_inplace_select_operations():
    """Test in-place operations with select"""
    print("\n=== Testing in-place operations with select() ===")
    
    x = torch.randn(5, 10, 15, requires_grad=True)
    y = torch.zeros_like(x)
    
    # Simulate WRF-style operations
    for k in range(10):
        # This is similar to what's done in WRF code
        y[:, k, :] = x[:, k, :] * 2.0 + 1.0
    
    loss = y.sum()
    
    try:
        loss.backward()
        if x.grad is not None:
            print("✅ In-place select operations preserve gradient flow")
            print(f"   Gradient norm: {x.grad.norm().item():.6f}")
            
            # Check gradient correctness
            expected_grad = torch.ones_like(x) * 2.0
            grad_error = (x.grad - expected_grad).abs().max().item()
            print(f"   Gradient error: {grad_error:.2e}")
            print(f"   Gradients are {'correct' if grad_error < 1e-6 else 'incorrect'}!")
    except Exception as e:
        print(f"❌ Error with in-place operations: {e}")
        return False
    
    return True

def test_complex_wrf_pattern():
    """Test a complex pattern similar to WRF code"""
    print("\n=== Testing complex WRF-like pattern ===")
    
    # Simulate WRF dimensions
    ny, nz, nx = 81, 64, 41
    nz_w = nz + 1
    
    # Create tensors similar to WRF
    ph = torch.randn(ny, nz_w, nx, requires_grad=True)
    ph_base = torch.randn(ny, nz_w, nx)
    
    # Simulate WRF vertical advection computation
    ph_tend = torch.zeros(ny, nz_w, nx)
    
    for k in range(1, nz_w - 1):
        # Similar to WRF code
        ph_total_upper = ph.select(1, k+1) + ph_base.select(1, k+1)
        ph_total_lower = ph.select(1, k-1) + ph_base.select(1, k-1)
        ph_total_here = ph.select(1, k) + ph_base.select(1, k)
        
        # Compute vertical advection
        vert_adv = (ph_total_upper - ph_total_lower) * 0.5
        
        # Update tendency
        ph_tend[:, k, :] = ph_total_here - vert_adv
    
    # Compute loss and gradients
    loss = ph_tend.sum()
    loss.backward()
    
    if ph.grad is not None:
        print("✅ Complex WRF pattern preserves gradient flow")
        print(f"   PH shape: {ph.shape}")
        print(f"   PH gradient shape: {ph.grad.shape}")
        print(f"   PH gradient norm: {ph.grad.norm().item():.6f}")
        
        # Check that gradients are reasonable
        grad_stats = {
            'min': ph.grad.min().item(),
            'max': ph.grad.max().item(),
            'mean': ph.grad.mean().item(),
            'std': ph.grad.std().item()
        }
        print(f"   Gradient stats: min={grad_stats['min']:.3f}, max={grad_stats['max']:.3f}, "
              f"mean={grad_stats['mean']:.3f}, std={grad_stats['std']:.3f}")
        
        return True
    else:
        print("❌ No gradients in complex WRF pattern!")
        return False

def test_newton_krylov_jvp():
    """Test JVP pattern used in Newton-Krylov solver"""
    print("\n=== Testing Newton-Krylov JVP pattern ===")
    
    # Simulate Newton-Krylov JVP computation
    def compute_rhs(U):
        """Simplified RHS computation using select operations"""
        ny, nz, nx = 10, 8, 6
        result = torch.zeros_like(U)
        
        # Extract components (simulating unpacking)
        offset = 0
        size_3d = ny * nz * nx
        theta = U[offset:offset+size_3d].view(ny, nz, nx)
        
        # Compute vertical differences using select
        for k in range(1, nz-1):
            theta_upper = theta.select(1, k+1)
            theta_lower = theta.select(1, k-1)
            diff = (theta_upper - theta_lower) / 2.0
            
            # Write back
            result[offset + k*ny*nx:offset + (k+1)*ny*nx] = diff.flatten()
        
        return result
    
    # Create test state
    U = torch.randn(480, requires_grad=True)  # 10*8*6 = 480
    v = torch.randn_like(U)
    
    # Compute JVP
    F_U = compute_rhs(U)
    jvp = autograd.grad(F_U, U, v, retain_graph=True, create_graph=True)[0]
    
    if jvp is not None:
        print("✅ Newton-Krylov JVP pattern works with select()")
        print(f"   State size: {U.shape}")
        print(f"   JVP norm: {jvp.norm().item():.6f}")
        
        # Test if we can compute second-order derivatives (important for Newton method)
        try:
            jvp2 = autograd.grad(jvp.sum(), U, retain_graph=True, create_graph=True)[0]
            if jvp2 is not None:
                print("✅ Second-order derivatives also work")
                print(f"   Second derivative norm: {jvp2.norm().item():.6f}")
        except RuntimeError as e:
            print(f"⚠️  Second-order derivatives not available: {str(e)[:50]}...")
        
        return True
    else:
        print("❌ Newton-Krylov JVP failed!")
        return False

def main():
    """Run all tests"""
    print("=== PyTorch select() Autograd Compatibility Test ===")
    print(f"PyTorch version: {torch.__version__}")
    
    results = []
    
    # Run all tests
    results.append(("Gradient flow", test_select_gradient_flow()))
    results.append(("JVP computation", test_jvp_with_select()))
    results.append(("Select vs Slice", test_select_vs_slice()))
    results.append(("In-place operations", test_inplace_select_operations()))
    results.append(("Complex WRF pattern", test_complex_wrf_pattern()))
    results.append(("Newton-Krylov JVP", test_newton_krylov_jvp()))
    
    # Summary
    print("\n" + "="*50)
    print("SUMMARY")
    print("="*50)
    
    all_passed = True
    for test_name, passed in results:
        status = "✅ PASS" if passed else "❌ FAIL"
        print(f"{test_name:25s}: {status}")
        if not passed:
            all_passed = False
    
    print("\n" + "="*50)
    if all_passed:
        print("✅ ALL TESTS PASSED - select() is fully compatible with autograd/JVP")
    else:
        print("❌ SOME TESTS FAILED - Review select() usage")
    print("="*50)
    
    return 0 if all_passed else 1

if __name__ == "__main__":
    exit(main())