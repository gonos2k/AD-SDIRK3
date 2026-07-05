/**
 * @file wrf_sdirk3_physics_kslab_base.h
 * @brief Base framework for physics k-slab operations
 * @date 2025-08-05
 * 
 * Provides base classes and utilities for implementing k-slab
 * optimizations in WRF physics parameterizations.
 * 
 * Design Principle: 현재 구조를 최대한 활용
 */

#ifndef WRF_SDIRK3_PHYSICS_KSLAB_BASE_H
#define WRF_SDIRK3_PHYSICS_KSLAB_BASE_H

#include "wrf_sdirk3_advanced_kslab.h"
#include "wrf_sdirk3_kslab_vectorized.h"
#include <memory>
#include <unordered_map>

namespace WRF_SDIRK3 {
namespace PhysicsKSlab {

/**
 * @brief Physics state variables for k-slab processing
 */
struct PhysicsState {
    // Prognostic variables
    torch::Tensor T;        // Temperature [K]
    torch::Tensor p;        // Pressure [Pa]
    torch::Tensor rho;      // Density [kg/m³]
    
    // Water species (mixing ratios in kg/kg)
    torch::Tensor qv;       // Water vapor
    torch::Tensor qc;       // Cloud water
    torch::Tensor qr;       // Rain
    torch::Tensor qi;       // Cloud ice
    torch::Tensor qs;       // Snow
    torch::Tensor qg;       // Graupel
    
    // Optional 2-moment variables
    torch::Tensor nc;       // Cloud droplet number
    torch::Tensor nr;       // Rain number
    torch::Tensor ni;       // Ice number
    torch::Tensor ns;       // Snow number
    torch::Tensor ng;       // Graupel number
    
    // Grid information
    torch::Tensor dz;       // Layer thickness [m]
    torch::Tensor rdz;      // 1/dz
    
    // Create from WRF arrays
    static PhysicsState from_wrf_arrays(
        int nx, int ny, int nz,
        const torch::TensorOptions& options = torch::kFloat32);
    
    // Validate state
    bool validate() const;
};

/**
 * @brief Physics tendencies for k-slab processing
 */
struct PhysicsTendency {
    // Temperature tendency [K/s]
    torch::Tensor dTdt;
    
    // Mixing ratio tendencies [kg/kg/s]
    torch::Tensor dqvdt;
    torch::Tensor dqcdt;
    torch::Tensor dqrdt;
    torch::Tensor dqidt;
    torch::Tensor dqsdt;
    torch::Tensor dqgdt;
    
    // Number concentration tendencies [#/kg/s]
    torch::Tensor dncdt;
    torch::Tensor dnrdt;
    torch::Tensor dnidt;
    torch::Tensor dnsdt;
    torch::Tensor dngdt;
    
    // Initialize to zero
    static PhysicsTendency zeros_like(const PhysicsState& state);
    
    // Apply tendencies to state
    void apply_to_state(PhysicsState& state, float dt) const;
};

/**
 * @brief Configuration for physics k-slab operations
 */
struct PhysicsKSlabConfig {
    // General physics options
    bool use_lookup_tables = true;
    bool enable_saturation_adjustment = true;
    float dt_physics = 1.0f;  // Physics timestep [s]
    
    // Scheme-specific options
    int num_hydrometeors = 6;  // Number of water species
    bool is_two_moment = false; // Two-moment microphysics
    
    // Performance options
    bool vectorize_saturation = true;
    bool vectorize_collection = true;
    bool fuse_processes = true;
    
    // Conservation options
    bool conserve_mass = true;
    bool conserve_number = true;
    float conservation_tol = 1e-10f;

    // Debug/validation options
    // FIX 2025-01-05: Optional strict checks for GPU (causes sync but catches bugs)
    bool strict_checks = false;  // Enable rho/dz positivity checks on GPU

    // Merge with k-slab config
    void merge_with_kslab(const AdvancedKSlab::AdaptiveKSlabConfig& kslab_config);
};

/**
 * @brief Hydrometeor species enum for fall velocity calculations
 * FIX 2025-01-02: Added to replace UB pointer comparison in species detection
 */
enum class HydrometeorSpecies {
    RAIN,
    SNOW,
    GRAUPEL
};

/**
 * @brief Base class for physics k-slab operations
 */
class PhysicsKSlabOperation : public AdvancedKSlab::KSlabOperation {
protected:
    PhysicsKSlabConfig phys_config_;
    
    // Common physics utilities (lazy initialized)
    mutable std::unique_ptr<class SaturationLookup> saturation_table_;
    mutable std::unique_ptr<class FallVelocityCalculator> fall_velocity_;
    
    // Performance metrics
    mutable size_t saturation_calls_ = 0;
    mutable size_t collection_calls_ = 0;
    mutable size_t sedimentation_calls_ = 0;
    
public:
    PhysicsKSlabOperation(const std::string& name, 
                         const PhysicsKSlabConfig& config)
        : KSlabOperation(name), phys_config_(config) {}
    
    virtual ~PhysicsKSlabOperation() = default;
    
    /**
     * @brief Process physics for a k-slab
     * 
     * This is the main entry point called by the k-slab processor.
     * Implementations should process the given k-slab of data.
     */
    virtual void process_slab(
        PhysicsState& state,
        PhysicsTendency& tendency,
        int k_start, int k_size,
        const AdvancedKSlab::AdaptiveKSlabConfig& kslab_config) = 0;
    
    /**
     * @brief Compute saturation adjustment for a k-slab
     * 
     * Adjusts temperature and water vapor to maintain saturation.
     * This is a common operation across many physics schemes.
     */
    virtual void compute_saturation_adjustment(
        torch::Tensor& T, torch::Tensor& qv,
        torch::Tensor& qc, torch::Tensor& qi,
        int k_start, int k_size);
    
    /**
     * @brief Compute fall velocities for hydrometeors
     *
     * Calculates terminal velocities for rain, snow, graupel, etc.
     * @param species The hydrometeor species (RAIN, SNOW, GRAUPEL)
     */
    virtual void compute_fall_velocities(
        torch::Tensor& vt_array,
        const torch::Tensor& q_array,
        const torch::Tensor& rho,
        int k_start, int k_size,
        HydrometeorSpecies species) = 0;
    
    /**
     * @brief Get performance metrics
     */
    virtual void get_metrics(std::unordered_map<std::string, double>& metrics) const {
        metrics[name_ + ".saturation_calls"] = static_cast<double>(saturation_calls_);
        metrics[name_ + ".collection_calls"] = static_cast<double>(collection_calls_);
        metrics[name_ + ".sedimentation_calls"] = static_cast<double>(sedimentation_calls_);
    }
    
    // Override from base class
    void apply(torch::Tensor& slab_data,
              int k_start, int k_size,
              const AdvancedKSlab::AdaptiveKSlabConfig& config) override {
        // This is called by the base k-slab processor
        // For physics, we need to unpack the data into PhysicsState
        // This is a placeholder - actual implementation depends on data packing
        throw std::runtime_error("Use process_slab() for physics operations");
    }
};

/**
 * @brief Saturation vapor pressure lookup table
 */
class SaturationLookup {
private:
    static constexpr int TABLE_SIZE = 10001;  // -100°C to 100°C at 0.02°C
    static constexpr float T_MIN = 173.15f;   // -100°C
    static constexpr float T_MAX = 373.15f;   // 100°C
    static constexpr float DT = 0.02f;        // Table resolution
    
    std::vector<float> es_table_;   // Saturation vapor pressure over water
    std::vector<float> esi_table_;  // Saturation vapor pressure over ice
    std::vector<float> des_table_;  // Derivative d(es)/dT
    std::vector<float> desi_table_; // Derivative d(esi)/dT
    
public:
    SaturationLookup();
    
    // Lookup functions (with linear interpolation)
    float es(float T) const;
    float esi(float T) const;
    float des_dt(float T) const;
    float desi_dt(float T) const;
    
    // Vectorized lookups
    void es_vectorized(const float* T, float* es, int n) const;
    void esi_vectorized(const float* T, float* esi, int n) const;
};

/**
 * @brief Fall velocity calculator for hydrometeors
 */
class FallVelocityCalculator {
private:
    // Fall velocity parameters (can be scheme-specific)
    struct FallParams {
        float a;  // Coefficient
        float b;  // Exponent
        float c;  // Density correction exponent
    };
    
    FallParams rain_params_ = {841.9f, 0.8f, 0.0f};
    FallParams snow_params_ = {11.72f, 0.41f, -0.25f};
    FallParams graupel_params_ = {330.0f, 0.8f, -0.25f};
    
public:
    // Scalar fall velocity
    float vt_rain(float qr, float rho) const;
    float vt_snow(float qs, float rho) const;
    float vt_graupel(float qg, float rho) const;
    
    // Vectorized fall velocity
    void vt_rain_vectorized(const float* qr, const float* rho, 
                           float* vt, int n) const;
    void vt_snow_vectorized(const float* qs, const float* rho,
                           float* vt, int n) const;
    void vt_graupel_vectorized(const float* qg, const float* rho,
                              float* vt, int n) const;
};

/**
 * @brief Physics k-slab processor with scheme registry
 */
class PhysicsKSlabProcessor {
private:
    // Registry of physics schemes
    std::unordered_map<std::string, std::unique_ptr<PhysicsKSlabOperation>> schemes_;
    
    // Base k-slab processor
    std::unique_ptr<AdvancedKSlab::AdvancedKSlabProcessor> kslab_processor_;
    
    // Configuration
    PhysicsKSlabConfig config_;
    
    // Performance tracking
    std::unordered_map<std::string, double> accumulated_metrics_;
    
public:
    PhysicsKSlabProcessor(int nx, int ny, int nz,
                         const PhysicsKSlabConfig& config = PhysicsKSlabConfig());
    
    /**
     * @brief Register a physics scheme
     */
    void register_scheme(const std::string& name,
                        std::unique_ptr<PhysicsKSlabOperation> scheme) {
        schemes_[name] = std::move(scheme);
    }
    
    /**
     * @brief Process physics using k-slab optimization
     * 
     * @param scheme_name Name of the physics scheme to use
     * @param state Current physics state
     * @param tendency Output tendencies
     * @param dt Physics timestep
     */
    void process_physics(
        const std::string& scheme_name,
        PhysicsState& state,
        PhysicsTendency& tendency,
        float dt);
    
    /**
     * @brief Get accumulated performance metrics
     */
    const std::unordered_map<std::string, double>& get_metrics() const {
        return accumulated_metrics_;
    }
    
    /**
     * @brief Print performance summary
     */
    void print_performance_summary() const;
    
    /**
     * @brief Get k-slab configuration
     */
    AdvancedKSlab::AdaptiveKSlabConfig& kslab_config() {
        return kslab_processor_->config();
    }
};

} // namespace PhysicsKSlab
} // namespace WRF_SDIRK3

#endif // WRF_SDIRK3_PHYSICS_KSLAB_BASE_H