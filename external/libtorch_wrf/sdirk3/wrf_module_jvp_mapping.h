#ifndef WRF_MODULE_JVP_MAPPING_H
#define WRF_MODULE_JVP_MAPPING_H

#include <string>
#include <unordered_map>
#include "wrf_sdirk3_jvp_convergence.h"

namespace wrf {
namespace sdirk3 {

/**
 * WRF Module-based JVP Method Mapping
 * 
 * Determines optimal JVP method (AD vs FD) for each WRF module
 * based on smoothness and differentiability characteristics
 */

enum class WRFModule {
    // Dynamics Core (smooth, differentiable)
    DYCORE_ADVECTION,       // Horizontal flux divergence
    DYCORE_PRESSURE_GRAD,   // Pressure gradient & acoustic coupling
    DYCORE_CORIOLIS,        // Coriolis & geometric terms
    DYCORE_DIFFUSION,       // Viscosity & diffusion (before PBL)
    
    // Boundary & Coupling (non-smooth)
    LATERAL_BC,             // Lateral boundary conditions
    COUPLER,               // Dycore-Physics coupling point
    
    // Physics - Surface (highly non-smooth)
    SURFACE_LAYER,         // Surface layer (z0, Cd, Ch)
    LAND_SURFACE,          // LSM (Noah/Noah-MP)
    
    // Physics - PBL (mixed)
    PBL_TURBULENCE,        // PBL schemes (YSU/MYNN)
    
    // Physics - Microphysics (non-smooth)
    MICROPHYSICS,          // WSM6/Thompson/etc.
    
    // Physics - Cumulus (non-smooth)
    CUMULUS,               // Convective parameterization
    
    // Physics - Radiation (mixed)
    RADIATION,             // RRTMG/etc.
    
    // Physics - Other
    GRAVITY_WAVE_DRAG,     // Orographic effects
    CHEMISTRY              // Chemistry/aerosol (optional)
};

struct ModuleJVPConfig {
    JVPMode preferred_mode;
    bool freeze_limiters;
    float eta_min_override;  // Module-specific EW lower bound
    std::string notes;
};

class WRFModuleJVPMapper {
private:
    std::unordered_map<WRFModule, ModuleJVPConfig> module_map;
    
    void initialize_mapping() {
        // Dynamics Core - Use AD-JVP (smooth, differentiable)
        module_map[WRFModule::DYCORE_ADVECTION] = {
            JVPMode::AD_JVP, false, 1e-8f,
            "Continuous flux divergence, AD recommended"
        };
        
        module_map[WRFModule::DYCORE_PRESSURE_GRAD] = {
            JVPMode::AD_JVP, false, 1e-8f,
            "Acoustic-pressure coupling, benefits from exact derivatives"
        };
        
        module_map[WRFModule::DYCORE_CORIOLIS] = {
            JVPMode::AD_JVP, false, 1e-8f,
            "Smooth geometric terms"
        };
        
        module_map[WRFModule::DYCORE_DIFFUSION] = {
            JVPMode::AD_JVP, false, 1e-8f,
            "Linear/quasi-linear diffusion"
        };
        
        // Boundary & Coupling - Use FD-JVP (non-smooth switches)
        module_map[WRFModule::LATERAL_BC] = {
            JVPMode::FD_CENTRAL, true, 5e-4f,
            "Boundary interpolation/relaxation switches"
        };
        
        module_map[WRFModule::COUPLER] = {
            JVPMode::FD_CENTRAL, true, 5e-4f,
            "Tendency aggregation with clipping"
        };
        
        // Surface Physics - Use FD-JVP (highly non-smooth)
        module_map[WRFModule::SURFACE_LAYER] = {
            JVPMode::FD_CENTRAL, true, 5e-4f,
            "Stability functions with clamps and branches"
        };
        
        module_map[WRFModule::LAND_SURFACE] = {
            JVPMode::FD_CENTRAL, true, 5e-4f,
            "Moisture/freeze thresholds, soil type switches"
        };
        
        // PBL - Mixed strategy
        module_map[WRFModule::PBL_TURBULENCE] = {
            JVPMode::FD_CENTRAL, true, 3e-4f,
            "Ri critical values, length-scale switches"
        };
        
        // Microphysics - Use FD-JVP (highly non-smooth)
        module_map[WRFModule::MICROPHYSICS] = {
            JVPMode::FD_CENTRAL, true, 1e-3f,
            "Autoconversion thresholds, saturation adjustments"
        };
        
        // Cumulus - Use FD-JVP (on/off triggers)
        module_map[WRFModule::CUMULUS] = {
            JVPMode::FD_CENTRAL, true, 1e-3f,
            "Convective triggers, closure assumptions"
        };
        
        // Radiation - Mixed, default to FD
        module_map[WRFModule::RADIATION] = {
            JVPMode::FD_CENTRAL, true, 3e-4f,
            "Table lookups, k-coefficient interpolation"
        };
        
        // Other physics
        module_map[WRFModule::GRAVITY_WAVE_DRAG] = {
            JVPMode::FD_CENTRAL, true, 5e-4f,
            "Threshold values and switches"
        };
        
        module_map[WRFModule::CHEMISTRY] = {
            JVPMode::FD_CENTRAL, true, 1e-3f,
            "Stiff reactions with switches"
        };
    }
    
public:
    WRFModuleJVPMapper() {
        initialize_mapping();
    }
    
    ModuleJVPConfig get_config(WRFModule module) const {
        auto it = module_map.find(module);
        if (it != module_map.end()) {
            return it->second;
        }
        // Default to safe FD-central with frozen limiters
        return {JVPMode::FD_CENTRAL, true, 5e-4f, "Default configuration"};
    }
    
    /**
     * Determine module from state vector indices
     * This is a simplified example - actual implementation needs
     * to map WRF's variable ordering
     */
    WRFModule identify_module(int var_idx, int total_vars) const {
        // Simplified mapping based on variable index
        // In practice, this needs to match WRF's state vector layout
        
        float ratio = static_cast<float>(var_idx) / total_vars;
        
        if (ratio < 0.4f) {
            // First 40% - dynamics variables (U, V, W)
            return WRFModule::DYCORE_ADVECTION;
        } else if (ratio < 0.5f) {
            // Pressure/geopotential
            return WRFModule::DYCORE_PRESSURE_GRAD;
        } else if (ratio < 0.7f) {
            // Thermodynamics
            return WRFModule::DYCORE_ADVECTION;
        } else {
            // Physics tendencies
            return WRFModule::MICROPHYSICS;  // Simplified
        }
    }
    
    /**
     * Get recommended JVP operator for a module
     */
    std::unique_ptr<JVPOperator> create_operator(
        WRFModule module, int nx, int ny, int nz) const {
        
        auto config = get_config(module);
        auto op = std::make_unique<JVPOperator>(config.preferred_mode, nx, ny, nz);
        op->freeze_limiters = config.freeze_limiters;
        op->convergence_params.eta_min = config.eta_min_override;
        return op;
    }
    
    /**
     * Check if module should use smoothing for AD-JVP
     */
    bool needs_smoothing(WRFModule module) const {
        // Modules that benefit from smoothing before AD
        switch(module) {
            case WRFModule::SURFACE_LAYER:
            case WRFModule::PBL_TURBULENCE:
            case WRFModule::RADIATION:
                return true;
            default:
                return false;
        }
    }
    
    /**
     * Dynamic mode switching based on convergence
     */
    JVPMode suggest_mode_switch(
        WRFModule module,
        JVPMode current_mode,
        float residual_ratio,
        int stagnation_count) const {
        
        auto config = get_config(module);
        
        // Only consider upgrading if module supports it
        if (config.preferred_mode == JVPMode::AD_JVP) {
            // Already at best mode
            return current_mode;
        }
        
        // Check for upgrade conditions
        if (current_mode == JVPMode::FD_FORWARD && stagnation_count >= 2) {
            return JVPMode::FD_CENTRAL;
        }
        
        if (current_mode == JVPMode::FD_CENTRAL && 
            stagnation_count >= 3 && residual_ratio > 0.98f) {
            // Consider AD if module has been smoothed
            if (needs_smoothing(module)) {
                return JVPMode::AD_JVP;  // Try AD with smoothing
            }
        }
        
        return current_mode;
    }
};

/**
 * Module-aware JVP operator that switches methods based on active module
 */
class ModuleAwareJVPOperator {
    WRFModuleJVPMapper mapper;
    std::unordered_map<WRFModule, std::unique_ptr<JVPOperator>> operators;
    int nx, ny, nz;
    
public:
    ModuleAwareJVPOperator(int nx_, int ny_, int nz_) 
        : nx(nx_), ny(ny_), nz(nz_) {}
    
    torch::Tensor compute_jvp(
        const torch::Tensor& U,
        const torch::Tensor& v,
        float dt,
        float gamma,
        WRFModule module,
        const std::function<torch::Tensor(const torch::Tensor&, bool)>& residual_func) {
        
        // Get or create operator for this module
        if (operators.find(module) == operators.end()) {
            operators[module] = mapper.create_operator(module, nx, ny, nz);
        }
        
        return operators[module]->compute_jvp(U, v, dt, gamma, residual_func);
    }
    
    void clear_cache() {
        for (auto& [mod, op] : operators) {
            op->clear_cache();
        }
    }
    
    float get_eta_min(WRFModule module) const {
        auto config = mapper.get_config(module);
        return config.eta_min_override;
    }
};

} // namespace sdirk3
} // namespace wrf

#endif // WRF_MODULE_JVP_MAPPING_H