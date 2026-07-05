/**
 * Cross-Platform Device Manager Header
 * Manages Apple MPS, NVIDIA CUDA, and CPU device abstraction
 */

#ifndef WRF_SDIRK3_DEVICE_MANAGER_H
#define WRF_SDIRK3_DEVICE_MANAGER_H

#include <torch/torch.h>
#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace wrf {
namespace sdirk3 {

// ============================================================================
// Enumerations and Structures
// ============================================================================

enum class DeviceType {
    MPS,    // Apple Metal Performance Shaders
    CUDA,   // NVIDIA CUDA
    CPU     // CPU fallback
};

enum class DevicePreference {
    AUTO,           // Automatic selection (MPS > CUDA > CPU)
    PREFER_MPS,     // Prefer MPS if available
    PREFER_CUDA,    // Prefer CUDA if available
    CPU_ONLY        // Force CPU execution
};

struct TileBounds {
    // Memory bounds (includes halos)
    int ims, ime, jms, jme, kms, kme;
    
    // Computation bounds (interior only)
    int its, ite, jts, jte, kts, kte;
    
    bool operator==(const TileBounds& other) const {
        return ims == other.ims && ime == other.ime &&
               jms == other.jms && jme == other.jme &&
               kms == other.kms && kme == other.kme &&
               its == other.its && ite == other.ite &&
               jts == other.jts && jte == other.jte &&
               kts == other.kts && kte == other.kte;
    }
};

// ============================================================================
// DeviceManager Class
// ============================================================================

class DeviceManager {
private:
    DevicePreference preference_;
    DeviceType device_type_;
    torch::Device torch_device_;
    
    // Memory tracking
    size_t allocated_memory_ = 0;
    size_t peak_memory_ = 0;
    size_t memory_pool_size_ = 0;
    
    // Performance tracking
    double transfer_time_ = 0.0;
    int transfer_count_ = 0;
    
    // Device capabilities
    bool has_unified_memory_ = false;
    int compute_capability_ = 0;
    
public:
    // Constructor
    explicit DeviceManager(DevicePreference preference = DevicePreference::AUTO);
    
    // Device detection and initialization
    void detectAndSelectDevice();
    void initializeDevice();
    
    // Memory allocation
    torch::Tensor allocateTensor(
        const std::vector<int64_t>& shape,
        torch::ScalarType dtype = torch::kFloat32,
        bool requires_grad = false
    );
    
    // Data transfer
    torch::Tensor toDevice(const torch::Tensor& tensor, bool non_blocking = false);
    torch::Tensor toCPU(const torch::Tensor& tensor, bool non_blocking = false);
    
    // Synchronization
    void synchronize();
    void clearCache();
    
    // Memory management
    size_t getCurrentMemory() const;
    size_t getPeakMemory() const;
    size_t getMemoryCapacity() const { return memory_pool_size_; }
    
    // Device information
    DeviceType getDeviceType() const { return device_type_; }
    torch::Device getTorchDevice() const { return torch_device_; }
    std::string getDeviceString() const;
    std::string getCUDADeviceName() const;
    
    // Statistics
    void printStats() const;
    
private:
    // Platform-specific initialization
    void initializeMPS();
    void initializeCUDA();
    void initializeCPU();
    void setupMemoryPool();
};

// ============================================================================
// MPSFallback Class - Handle unsupported MPS operations
// ============================================================================

class MPSFallback {
public:
    static torch::Tensor executeWithFallback(
        std::function<torch::Tensor()> mps_op,
        std::function<torch::Tensor()> cpu_op,
        const std::string& op_name
    );
};

// ============================================================================
// TileMemoryManager Class
// ============================================================================

class TileMemoryManager {
private:
    DeviceManager& device_manager_;
    TileBounds bounds_;
    
    // Tile dimensions
    int ni_, nj_, nk_;           // Computation dimensions
    int ni_mem_, nj_mem_, nk_mem_;  // Memory dimensions (with halos)
    
    // Pre-allocated workspace
    std::vector<torch::Tensor> workspace_K_;  // SDIRK3 stage vectors
    std::vector<torch::Tensor> temp_buffers_;
    
    // Precision control
    bool use_double_precision_ = false;
    
public:
    TileMemoryManager(DeviceManager& device_manager, const TileBounds& bounds);
    
    // Memory layout conversion
    torch::Tensor fortranToTorch(
        const float* fortran_data,
        const std::vector<int64_t>& var_shape,
        bool is_staggered = false,
        int stagger_dim = -1
    );
    
    torch::Tensor fortranToTorch2D(
        const float* fortran_data,
        const std::vector<int64_t>& var_shape
    );
    
    void torchToFortran(
        const torch::Tensor& tensor,
        float* fortran_data
    );
    
    void torchToFortran2D(
        const torch::Tensor& tensor,
        float* fortran_data
    );
    
    // Interior/halo operations
    torch::Tensor extractInterior(const torch::Tensor& full_tile);
    void updateInterior(torch::Tensor& full_tile, const torch::Tensor& interior);
    
    // Workspace access
    torch::Tensor& getStageVector(int stage) { return workspace_K_[stage]; }
    torch::Tensor& getTempBuffer(int index) { return temp_buffers_[index]; }
    
    // Bounds checking
    bool matchesBounds(const TileBounds& other) const {
        return bounds_ == other;
    }
    
private:
    void allocateWorkspace();
};

// ============================================================================
// WRF State Structure
// ============================================================================

struct WRFState {
    // Prognostic variables
    torch::Tensor U;   // U-momentum (coupled)
    torch::Tensor V;   // V-momentum (coupled)
    torch::Tensor W;   // W-momentum (coupled)
    torch::Tensor T;   // Potential temperature
    torch::Tensor PH;  // Geopotential
    torch::Tensor MU;  // Dry air mass
    torch::Tensor P;   // Pressure
    
    // Moisture variables
    torch::Tensor QV;  // Water vapor
    torch::Tensor QC;  // Cloud water
    torch::Tensor QR;  // Rain water
    
    // Pack into single tensor
    torch::Tensor pack() const;
    
    // Unpack from single tensor
    void unpack(const torch::Tensor& packed);
};

// ============================================================================
// SDIRK3 Solver (forward declaration)
// ============================================================================

class SDIRK3Solver {
private:
    DeviceManager& device_manager_;
    bool enable_diagnostics_;
    
    // Newton solver parameters
    float newton_tolerance_ = 1e-8f;
    int max_newton_iterations_ = 10;
    
    // Convergence tracking
    bool last_converged_ = false;
    int last_iterations_ = 0;
    float last_residual_ = 0.0f;
    
public:
    SDIRK3Solver(DeviceManager& device_manager, bool enable_diagnostics = false);
    
    // Time advancement
    WRFState advance(const WRFState& state, float dt);
    
    // Configuration
    void setNewtonTolerance(float tol) { newton_tolerance_ = tol; }
    void setMaxNewtonIterations(int max_iter) { max_newton_iterations_ = max_iter; }
    
    // Status
    bool getLastConvergenceStatus() const { return last_converged_; }
    int getLastIterationCount() const { return last_iterations_; }
    float getLastResidual() const { return last_residual_; }
};

} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_DEVICE_MANAGER_H