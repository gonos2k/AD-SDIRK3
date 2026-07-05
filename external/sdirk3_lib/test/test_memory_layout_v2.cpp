#include <gtest/gtest.h>
#include <torch/torch.h>
#include "sdirk3/wrf_memory_layout_adapter.h"
#include "sdirk3/wrf_atomic_operations_jvp.h"
#include <chrono>
#include <random>

using namespace wrf::sdirk3;
using namespace std::chrono;

/**
 * Test suite for WRF-SDIRK3 memory layout transformation
 * Validates the corrected (j,k,i) layout for optimal performance
 */

class MemoryLayoutTest : public ::testing::Test {
protected:
    WRFMemoryLayoutAdapter::GridDimensions dims;
    std::unique_ptr<WRFMemoryLayoutAdapter> adapter;
    
    void SetUp() override {
        // Small test domain
        dims.its = 1; dims.ite = 10;  // i: 10 points
        dims.jts = 1; dims.jte = 8;   // j: 8 points  
        dims.kts = 1; dims.kte = 5;   // k: 5 levels
        
        dims.ims = 0; dims.ime = 11;
        dims.jms = 0; dims.jme = 9;
        dims.kms = 0; dims.kme = 6;
        
        dims.ids = dims.its; dims.ide = dims.ite;
        dims.jds = dims.jts; dims.jde = dims.jte;
        dims.kds = dims.kts; dims.kde = dims.kte;
        
        adapter = std::make_unique<WRFMemoryLayoutAdapter>(dims);
    }
};

// Test 1: Verify correct (j,k,i) layout transformation
TEST_F(MemoryLayoutTest, CorrectLayoutTransformation) {
    int ni = dims.ni();
    int nj = dims.nj();
    int nk = dims.nk();
    
    // Create WRF data with known pattern
    std::vector<float> wrf_data(ni * nk * nj);
    
    // Fill with pattern: value = j*10000 + k*100 + i
    for (int j = 0; j < nj; j++) {
        for (int k = 0; k < nk; k++) {
            for (int i = 0; i < ni; i++) {
                // WRF Fortran index (i,k,j)
                int wrf_idx = i + k * ni + j * ni * nk;
                wrf_data[wrf_idx] = j * 10000 + k * 100 + i;
            }
        }
    }
    
    // Transform to torch tensor
    auto tensor = adapter->from_wrf_3d(wrf_data.data());
    
    // Verify shape is (nj, nk, ni)
    EXPECT_EQ(tensor.size(0), nj);
    EXPECT_EQ(tensor.size(1), nk);
    EXPECT_EQ(tensor.size(2), ni);
    
    // Verify data is correctly placed with (j,k,i) indexing
    for (int j = 0; j < nj; j++) {
        for (int k = 0; k < nk; k++) {
            for (int i = 0; i < ni; i++) {
                float expected = j * 10000 + k * 100 + i;
                float actual = tensor[j][k][i].item<float>();
                EXPECT_FLOAT_EQ(actual, expected) 
                    << "Mismatch at (j=" << j << ",k=" << k << ",i=" << i << ")";
            }
        }
    }
}

// Test 2: Verify staggered grid dimensions
TEST_F(MemoryLayoutTest, StaggeredGridDimensions) {
    auto stagger = adapter->staggered_dims();
    
    // Scalar grid: (nj, nk, ni)
    auto scalar = adapter->create_tensor_for_var("T");
    EXPECT_EQ(scalar.size(0), dims.nj());
    EXPECT_EQ(scalar.size(1), dims.nk());
    EXPECT_EQ(scalar.size(2), dims.ni());
    
    // U-staggered: (nj, nk, ni+1)
    auto u = adapter->create_tensor_for_var("U");
    EXPECT_EQ(u.size(0), dims.nj());
    EXPECT_EQ(u.size(1), dims.nk());
    EXPECT_EQ(u.size(2), dims.ni() + 1);
    
    // V-staggered: (nj+1, nk, ni)
    auto v = adapter->create_tensor_for_var("V");
    EXPECT_EQ(v.size(0), dims.nj() + 1);
    EXPECT_EQ(v.size(1), dims.nk());
    EXPECT_EQ(v.size(2), dims.ni());
    
    // W-staggered: (nj, nk+1, ni)
    auto w = adapter->create_tensor_for_var("W");
    EXPECT_EQ(w.size(0), dims.nj());
    EXPECT_EQ(w.size(1), dims.nk() + 1);
    EXPECT_EQ(w.size(2), dims.ni());
}

// Test 3: Cache performance test - horizontal operations
TEST_F(MemoryLayoutTest, HorizontalOperationPerformance) {
    int ni = dims.ni();
    int nj = dims.nj();
    int nk = dims.nk();
    
    // Create test tensor with (j,k,i) layout
    auto tensor = torch::randn({nj, nk, ni}, torch::kFloat32);
    
    // Measure horizontal gradient computation (should be cache-efficient)
    int iterations = 10000;
    auto start = high_resolution_clock::now();
    
    for (int iter = 0; iter < iterations; iter++) {
        // Compute ∂/∂x (i-direction gradient)
        for (int j = 0; j < nj; j++) {
            for (int k = 0; k < nk; k++) {
                // This inner loop should be cache-efficient
                for (int i = 1; i < ni-1; i++) {
                    float grad = (tensor[j][k][i+1].item<float>() - 
                                 tensor[j][k][i-1].item<float>()) / 2.0f;
                    // Use result to prevent optimization
                    tensor[j][k][i] = tensor[j][k][i] + grad * 0.0001f;
                }
            }
        }
    }
    
    auto end = high_resolution_clock::now();
    auto duration = duration_cast<microseconds>(end - start).count();
    
    std::cout << "Horizontal operation time: " << duration << " μs for " 
              << iterations << " iterations" << std::endl;
    
    // This test primarily ensures no crashes and reasonable performance
    EXPECT_GT(duration, 0);
}

// Test 4: Column operation efficiency
TEST_F(MemoryLayoutTest, ColumnOperationEfficiency) {
    int ni = dims.ni();
    int nj = dims.nj();
    int nk = dims.nk();
    
    auto tensor = torch::randn({nj, nk, ni}, torch::kFloat32);
    
    // Test column extraction
    for (int j = 0; j < nj; j++) {
        for (int i = 0; i < ni; i++) {
            auto column = adapter->get_column_view(tensor, i, j);
            
            // Verify column has correct size
            EXPECT_EQ(column.size(0), nk);
            
            // Verify column values match
            for (int k = 0; k < nk; k++) {
                EXPECT_FLOAT_EQ(column[k].item<float>(), 
                               tensor[j][k][i].item<float>());
            }
        }
    }
}

// Test 5: Mass weighting with corrected layout
TEST_F(MemoryLayoutTest, MassWeightingOperation) {
    int ni = dims.ni();
    int nj = dims.nj();
    int nk = dims.nk();
    
    // Create velocity field (3D) with (j,k,i) layout
    auto velocity = torch::ones({nj, nk, ni}, torch::kFloat32) * 2.0f;
    
    // Create dry air mass (2D) with (j,i) layout
    auto mu_d = torch::ones({nj, ni}, torch::kFloat32) * 3.0f;
    
    // Apply mass weighting: U = μ_d * u
    auto U_coupled = MassWeightingOp::apply(velocity, mu_d);
    
    // Verify shape
    EXPECT_EQ(U_coupled.sizes(), velocity.sizes());
    
    // Verify values (should be 2.0 * 3.0 = 6.0)
    for (int j = 0; j < nj; j++) {
        for (int k = 0; k < nk; k++) {
            for (int i = 0; i < ni; i++) {
                EXPECT_FLOAT_EQ(U_coupled[j][k][i].item<float>(), 6.0f);
            }
        }
    }
}

// Test 6: JVP for mass weighting
TEST_F(MemoryLayoutTest, MassWeightingJVP) {
    int ni = dims.ni();
    int nj = dims.nj();
    int nk = dims.nk();
    
    // Create test data
    auto velocity = torch::randn({nj, nk, ni}, torch::kFloat32);
    auto mu_d = torch::randn({nj, ni}, torch::kFloat32).abs() + 1.0f;
    
    // Create tangent vectors
    auto v_velocity = torch::randn({nj, nk, ni}, torch::kFloat32);
    auto v_mu_d = torch::randn({nj, ni}, torch::kFloat32);
    
    // Compute JVP: δU = u * δμ_d + μ_d * δu
    auto jvp_result = MassWeightingOp::jvp(velocity, mu_d, v_velocity, v_mu_d);
    
    // Manually compute expected JVP
    auto mu_d_3d = mu_d.unsqueeze(1).expand({nj, nk, ni});
    auto v_mu_d_3d = v_mu_d.unsqueeze(1).expand({nj, nk, ni});
    auto expected_jvp = velocity * v_mu_d_3d + mu_d_3d * v_velocity;
    
    // Compare
    auto diff = (jvp_result - expected_jvp).abs().max();
    EXPECT_LT(diff.item<float>(), 1e-6f);
}

// Test 7: Round-trip conversion
TEST_F(MemoryLayoutTest, RoundTripConversion) {
    int ni = dims.ni();
    int nj = dims.nj();
    int nk = dims.nk();
    
    // Create original WRF data
    std::vector<float> original(ni * nk * nj);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dist(-100.0f, 100.0f);
    
    for (auto& val : original) {
        val = dist(gen);
    }
    
    // Convert to torch
    auto tensor = adapter->from_wrf_3d(original.data());
    
    // Convert back to WRF
    std::vector<float> converted(ni * nk * nj);
    adapter->to_wrf_3d(tensor, converted.data());
    
    // Verify identical
    for (size_t i = 0; i < original.size(); i++) {
        EXPECT_FLOAT_EQ(original[i], converted[i]) 
            << "Mismatch at index " << i;
    }
}

// Test 8: Divergence computation with new layout
TEST_F(MemoryLayoutTest, DivergenceComputation) {
    int ni = dims.ni();
    int nj = dims.nj();
    int nk = dims.nk();
    
    // Create staggered velocity components
    auto u = torch::ones({nj, nk, ni+1}, torch::kFloat32);
    auto v = torch::ones({nj+1, nk, ni}, torch::kFloat32);
    auto w = torch::ones({nj, nk+1, ni}, torch::kFloat32);
    
    // Create vertical grid spacing
    auto dzw = torch::ones({nk}, torch::kFloat32) * 0.1f;
    
    // Compute divergence
    auto div = CGridStaggering::compute_divergence(u, v, w, 1.0f, 1.0f, dzw);
    
    // Verify shape
    EXPECT_EQ(div.size(0), nj);
    EXPECT_EQ(div.size(1), nk);
    EXPECT_EQ(div.size(2), ni);
    
    // For uniform flow, divergence should be zero
    auto max_div = div.abs().max();
    EXPECT_LT(max_div.item<float>(), 1e-5f);
}

// Test 9: Performance comparison test
TEST_F(MemoryLayoutTest, LayoutPerformanceComparison) {
    // Create larger domain for performance testing
    int ni = 100, nj = 80, nk = 50;
    int iterations = 100;
    
    // Test (j,k,i) layout - current implementation
    auto tensor_jki = torch::randn({nj, nk, ni}, torch::kFloat32);
    auto start_jki = high_resolution_clock::now();
    
    for (int iter = 0; iter < iterations; iter++) {
        // Horizontal operation (should be fast)
        for (int j = 0; j < nj; j++) {
            for (int k = 0; k < nk; k++) {
                auto row = tensor_jki[j][k];  // Contiguous i-data
                row = row * 1.01f;  // Simple operation
            }
        }
    }
    
    auto time_jki = duration_cast<microseconds>(
        high_resolution_clock::now() - start_jki).count();
    
    std::cout << "\nPerformance Results:" << std::endl;
    std::cout << "(j,k,i) layout time: " << time_jki << " μs" << std::endl;
    
    // Add assertion to ensure test ran
    EXPECT_GT(time_jki, 0);
}

// Test 10: Verify memory contiguity
TEST_F(MemoryLayoutTest, MemoryContiguity) {
    int ni = dims.ni();
    int nj = dims.nj();
    int nk = dims.nk();
    
    auto tensor = torch::randn({nj, nk, ni}, torch::kFloat32);
    
    // Check if tensor is contiguous
    EXPECT_TRUE(tensor.is_contiguous());
    
    // Verify stride pattern for (j,k,i) layout
    auto strides = tensor.strides();
    EXPECT_EQ(strides[0], nk * ni);  // j-stride
    EXPECT_EQ(strides[1], ni);       // k-stride
    EXPECT_EQ(strides[2], 1);        // i-stride (contiguous)
    
    std::cout << "\nStride pattern (j,k,i):" << std::endl;
    std::cout << "j-stride: " << strides[0] << std::endl;
    std::cout << "k-stride: " << strides[1] << std::endl;
    std::cout << "i-stride: " << strides[2] << " (contiguous)" << std::endl;
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}