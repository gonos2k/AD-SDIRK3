/**
 * @file wrf_sdirk3_compressible_heating.h
 * @brief Compressible heating term handler for implicit SDIRK3 solver
 * 
 * This module implements proper implicit treatment of the compressible heating
 * term (μ/ρ)(∂p/∂t) which has circular dependencies in implicit formulations.
 */

#ifndef WRF_SDIRK3_COMPRESSIBLE_HEATING_H
#define WRF_SDIRK3_COMPRESSIBLE_HEATING_H

#include <torch/torch.h>
#include "wrf_sdirk3_types.h"
#include "wrf_sdirk3_unified_rhs.h"

namespace wrf {
namespace sdirk3 {

/**
 * @struct CompressibleHeatingDiagnostics
 * @brief Diagnostic information for compressible heating computation
 */
struct CompressibleHeatingDiagnostics {
    float max_heating_rate = 0.0f;      // Maximum heating/cooling rate (K/s)
    float min_denominator = 1.0f;       // Minimum denominator value
    int num_fallbacks = 0;              // Number of fallbacks to lagged method
    int num_limited = 0;                // Number of limited values
    float avg_heating_rate = 0.0f;      // Average heating rate magnitude
    float total_energy_change = 0.0f;   // Total energy change (J/m^3)
    
    void print() const;
    void reset();
};

/**
 * @class CompressibleHeatingHandler
 * @brief Handles compressible heating term computation for implicit solver
 * 
 * Implements multiple methods to handle the circular dependency:
 * 1. Analytical elimination (recommended)
 * 2. Lagged approach
 * 3. Disabled (for testing)
 */
class CompressibleHeatingHandler {
public:
    /**
     * @enum Method
     * @brief Available methods for compressible heating computation
     */
    enum Method {
        ANALYTICAL = 0,  ///< Analytical elimination of circular dependency
        LAGGED = 1,      ///< Use pressure tendency from previous iteration
        DISABLED = 2     ///< Disable compressible heating (for testing)
    };
    
    /**
     * @struct Config
     * @brief Configuration parameters for compressible heating
     */
    struct Config {
        Method method;                       ///< Computation method
        float safety_factor;                 ///< Safety threshold for denominator
        float max_heating_rate;              ///< Maximum allowed heating rate (K/s)
        bool enable_diagnostics;             ///< Enable diagnostic collection
        bool enable_limiter;                 ///< Enable heating rate limiter
        float energy_check_tol;              ///< Energy conservation tolerance
        
        Config() : method(ANALYTICAL), safety_factor(0.01f), max_heating_rate(100.0f),
                   enable_diagnostics(false), enable_limiter(true), energy_check_tol(1e-6f) {}
    };
    
    /**
     * @brief Constructor
     * @param config Configuration parameters
     */
    explicit CompressibleHeatingHandler(const Config& config = Config());
    
    /**
     * @brief Compute compressible heating contribution to theta tendency
     * 
     * @param theta_flux_div Theta flux divergence (without compressible heating)
     * @param mu_tend Dry air mass tendency
     * @param state Current WRF state
     * @param grid Grid information
     * @return Theta tendency including compressible heating
     */
    torch::Tensor computeHeating(
        const torch::Tensor& theta_flux_div,
        const torch::Tensor& mu_tend,
        const WRFStateComponents& state,
        const WRFGridInfo& grid
    );
    
    /**
     * @brief Update lagged pressure tendency values
     * 
     * Called at the end of each Newton iteration to store current pressure
     * tendency for use in the next iteration (lagged method only).
     * 
     * @param dp_dt Current pressure tendency
     */
    void updateLaggedValues(const torch::Tensor& dp_dt);
    
    /**
     * @brief Check if analytical method is safe for given conditions
     * 
     * @param mu Dry air mass
     * @param rho Density
     * @param p Pressure
     * @param theta Potential temperature
     * @param cp Specific heat at constant pressure
     * @param cv Specific heat at constant volume
     * @return true if safe to use analytical method
     */
    bool isSafeForAnalytical(
        float mu, float rho, float p, float theta,
        float cp, float cv
    ) const;
    
    /**
     * @brief Get diagnostic information
     * @return Current diagnostics
     */
    const CompressibleHeatingDiagnostics& getDiagnostics() const {
        return diagnostics_;
    }
    
    /**
     * @brief Reset diagnostic counters
     */
    void resetDiagnostics() {
        diagnostics_.reset();
    }
    
    /**
     * @brief Set computation method
     * @param method New method to use
     */
    void setMethod(Method method) {
        config_.method = method;
    }
    
    /**
     * @brief Get current configuration
     * @return Current configuration
     */
    const Config& getConfig() const {
        return config_;
    }
    
private:
    Config config_;                      ///< Configuration parameters
    torch::Tensor dp_dt_lagged_;        ///< Lagged pressure tendency
    CompressibleHeatingDiagnostics diagnostics_;  ///< Diagnostic information
    
    /**
     * @brief Compute heating using analytical elimination method
     */
    torch::Tensor computeAnalytical(
        const torch::Tensor& theta_flux_div,
        const torch::Tensor& mu_tend,
        const WRFStateComponents& state,
        const WRFGridInfo& grid
    );
    
    /**
     * @brief Compute heating using lagged pressure tendency
     */
    torch::Tensor computeLagged(
        const torch::Tensor& theta_flux_div,
        const torch::Tensor& mu_tend,
        const WRFStateComponents& state,
        const WRFGridInfo& grid
    );
    
    /**
     * @brief Apply heating rate limiter
     */
    float limitHeatingRate(float dtheta_dt) const;
    
    /**
     * @brief Update diagnostic statistics
     */
    void updateDiagnostics(
        float heating_rate,
        float denominator,
        bool used_fallback,
        bool was_limited
    );
};

/**
 * @brief Compute pressure tendency for diagnostics
 * 
 * Computes ∂p/∂t = (γp/θ)(∂θ/∂t) + (p/μ)(∂μ/∂t)
 * 
 * @param theta_tend Potential temperature tendency
 * @param mu_tend Dry air mass tendency
 * @param state Current state
 * @param grid Grid information
 * @return Pressure tendency
 */
torch::Tensor computePressureTendency(
    const torch::Tensor& theta_tend,
    const torch::Tensor& mu_tend,
    const WRFStateComponents& state,
    const WRFGridInfo& grid
);

/**
 * @brief Check energy conservation
 * 
 * Verifies that the compressible heating term maintains energy conservation
 * within specified tolerance.
 * 
 * @param state_old Previous state
 * @param state_new New state after time step
 * @param dt Time step
 * @param grid Grid information
 * @param tolerance Energy conservation tolerance
 * @return true if energy is conserved within tolerance
 */
bool checkEnergyConservation(
    const WRFStateComponents& state_old,
    const WRFStateComponents& state_new,
    float dt,
    const WRFGridInfo& grid,
    float tolerance = 1e-6f
);

} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_COMPRESSIBLE_HEATING_H