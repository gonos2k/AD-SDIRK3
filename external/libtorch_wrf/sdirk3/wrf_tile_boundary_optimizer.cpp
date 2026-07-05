/**
 * @file wrf_tile_boundary_optimizer.cpp
 * @brief Implementation of tile boundary optimization
 */

#include "wrf_tile_boundary_optimizer.h"
#include "wrf_sdirk3_config.h"  // FIX Round161: For g_sdirk3_config.debug_level
#include <iostream>
#include <algorithm>

namespace wrf {
namespace sdirk3 {

// Global instance
std::unique_ptr<TileBoundaryManager> TileBoundaryManager::instance_;

// TileBoundaryOptimizer implementation
TileBoundaryOptimizer::TileBoundaryOptimizer(int tile_id, int nx, int ny, int nz)
    : tile_id_(tile_id), current_step_(0) {
    // nx, ny, nz parameters are available but not stored as members
    (void)nx; (void)ny; (void)nz; // Suppress unused parameter warnings
}

bool TileBoundaryOptimizer::can_reuse_boundary(int neighbor_tile_id, int step) const {
    auto it = boundary_cache_.find(neighbor_tile_id);
    if (it == boundary_cache_.end()) {
        return false;
    }
    
    // Check if data is valid and from current or previous step
    const auto& data = it->second;
    return data.is_valid && (step - data.computation_step <= 1);
}

const TileBoundaryData& TileBoundaryOptimizer::get_boundary_data(int neighbor_tile_id) const {
    static TileBoundaryData empty_data;
    
    auto it = boundary_cache_.find(neighbor_tile_id);
    if (it != boundary_cache_.end()) {
        cache_hits_++;
        return it->second;
    }
    
    cache_misses_++;
    return empty_data;
}

void TileBoundaryOptimizer::cache_boundary_data(
    int neighbor_tile_id,
    TileBoundaryData::BoundaryType type,
    const torch::Tensor& flux_x,
    const torch::Tensor& flux_y,
    const torch::Tensor& pressure_grad_x,
    const torch::Tensor& pressure_grad_y,
    const torch::Tensor& advection_tend) {
    
    TileBoundaryData data;
    data.neighbor_tile_id = neighbor_tile_id;
    data.type = type;
    data.flux_x = flux_x.clone();
    data.flux_y = flux_y.clone();
    data.pressure_grad_x = pressure_grad_x.clone();
    data.pressure_grad_y = pressure_grad_y.clone();
    data.advection_tend = advection_tend.clone();
    data.is_valid = true;
    data.computation_step = current_step_;
    
    boundary_cache_[neighbor_tile_id] = std::move(data);
}

void TileBoundaryOptimizer::compute_shared_boundary(
    const torch::Tensor& u,
    const torch::Tensor& v,
    const torch::Tensor& w,
    const torch::Tensor& p,
    int neighbor_tile_id,
    TileBoundaryData::BoundaryType type,
    float dx, float dy) {
    
    // Check if we can reuse existing computation
    if (can_reuse_boundary(neighbor_tile_id, current_step_)) {
        computations_saved_++;
        return;
    }
    
    // Extract boundary region
    auto u_boundary = extract_boundary_region(u, type, true, false);
    auto v_boundary = extract_boundary_region(v, type, false, true);
    auto w_boundary = extract_boundary_region(w, type);
    auto p_boundary = extract_boundary_region(p, type);
    
    // Compute fluxes - need to handle staggered dimensions properly
    torch::Tensor flux_x, flux_y;
    switch (type) {
        case TileBoundaryData::EAST:
        case TileBoundaryData::WEST:
            // For E/W boundaries, u and v have different x dimensions
            // u_boundary: [nz, ny, halo_x] where halo_x from nx_u = nx+1
            // v_boundary: [nz, ny+1, halo_x] where halo_x from nx
            // Need to interpolate v to u-points
            if (v_boundary.size(1) == u_boundary.size(1) + 1) {
                // Average v in y to match u dimensions
                auto v_at_u = 0.5f * (v_boundary.slice(1, 0, -1) + v_boundary.slice(1, 1));
                flux_x = u_boundary * u_boundary;
                flux_y = u_boundary * v_at_u;
            } else {
                // Dimensions already match (shouldn't happen but handle gracefully)
                flux_x = u_boundary * u_boundary;
                flux_y = u_boundary * v_boundary;
            }
            break;
        case TileBoundaryData::NORTH:
        case TileBoundaryData::SOUTH:
            // For N/S boundaries, u and v have different y dimensions
            // u_boundary: [nz, halo_y, nx+1] where halo_y from ny
            // v_boundary: [nz, halo_y, nx] where halo_y from ny_v = ny+1
            // Need to interpolate u to v-points
            if (u_boundary.size(2) == v_boundary.size(2) + 1) {
                // Average u in x to match v dimensions
                auto u_at_v = 0.5f * (u_boundary.slice(2, 0, -1) + u_boundary.slice(2, 1));
                flux_x = u_at_v * v_boundary;
                flux_y = v_boundary * v_boundary;
            } else {
                // Dimensions already match (shouldn't happen but handle gracefully)
                flux_x = u_boundary * v_boundary;
                flux_y = v_boundary * v_boundary;
            }
            break;
        default:
            // Corner cases - need both interpolations
            if (u_boundary.size(2) > v_boundary.size(2)) {
                auto u_at_mass = 0.5f * (u_boundary.slice(2, 0, -1) + u_boundary.slice(2, 1));
                flux_x = u_at_mass * u_at_mass;
            } else {
                flux_x = u_boundary * u_boundary;
            }
            if (v_boundary.size(1) > u_boundary.size(1)) {
                auto v_at_mass = 0.5f * (v_boundary.slice(1, 0, -1) + v_boundary.slice(1, 1));
                flux_y = v_at_mass * v_at_mass;
            } else {
                flux_y = v_boundary * v_boundary;
            }
    }
    
    // Compute pressure gradients
    torch::Tensor pressure_grad_x, pressure_grad_y;
    
    if (type == TileBoundaryData::EAST || type == TileBoundaryData::WEST) {
        // x-direction gradient at boundary
        pressure_grad_x = torch::zeros_like(p_boundary);
        pressure_grad_y = (p_boundary.roll({-1}, {0}) - p_boundary.roll({1}, {0})) / (2.0f * dy);
    } else {
        // y-direction gradient at boundary
        pressure_grad_x = (p_boundary.roll({-1}, {2}) - p_boundary.roll({1}, {2})) / (2.0f * dx);
        pressure_grad_y = torch::zeros_like(p_boundary);
    }
    
    // Compute advection tendency (simplified)
    auto advection_tend = -(flux_x / dx + flux_y / dy);
    
    // Cache the results
    cache_boundary_data(neighbor_tile_id, type,
                       flux_x, flux_y,
                       pressure_grad_x, pressure_grad_y,
                       advection_tend);
}

torch::Tensor TileBoundaryOptimizer::extract_boundary_region(
    const torch::Tensor& field,
    TileBoundaryData::BoundaryType type,
    bool is_staggered_x,
    bool is_staggered_y) const {
    
    // Field dimensions [nz, ny, nx]
    [[maybe_unused]] int nz = field.size(0);
    int ny = field.size(1);
    int nx = field.size(2);
    
    torch::Tensor boundary;
    
    switch (type) {
        case TileBoundaryData::EAST:
            // Extract eastern boundary with halo
            boundary = field.index({torch::indexing::Slice(),
                                   torch::indexing::Slice(),
                                   torch::indexing::Slice(nx - HALO_SIZE - 1, nx)});
            break;
            
        case TileBoundaryData::WEST:
            // Extract western boundary with halo
            boundary = field.index({torch::indexing::Slice(),
                                   torch::indexing::Slice(),
                                   torch::indexing::Slice(0, HALO_SIZE + 1)});
            break;
            
        case TileBoundaryData::NORTH:
            // Extract northern boundary with halo
            boundary = field.index({torch::indexing::Slice(),
                                   torch::indexing::Slice(ny - HALO_SIZE - 1, ny),
                                   torch::indexing::Slice()});
            break;
            
        case TileBoundaryData::SOUTH:
            // Extract southern boundary with halo
            boundary = field.index({torch::indexing::Slice(),
                                   torch::indexing::Slice(0, HALO_SIZE + 1),
                                   torch::indexing::Slice()});
            break;
            
        case TileBoundaryData::NORTH_EAST:
            // Corner region
            boundary = field.index({torch::indexing::Slice(),
                                   torch::indexing::Slice(ny - HALO_SIZE - 1, ny),
                                   torch::indexing::Slice(nx - HALO_SIZE - 1, nx)});
            break;
            
        case TileBoundaryData::NORTH_WEST:
            boundary = field.index({torch::indexing::Slice(),
                                   torch::indexing::Slice(ny - HALO_SIZE - 1, ny),
                                   torch::indexing::Slice(0, HALO_SIZE + 1)});
            break;
            
        case TileBoundaryData::SOUTH_EAST:
            boundary = field.index({torch::indexing::Slice(),
                                   torch::indexing::Slice(0, HALO_SIZE + 1),
                                   torch::indexing::Slice(nx - HALO_SIZE - 1, nx)});
            break;
            
        case TileBoundaryData::SOUTH_WEST:
            boundary = field.index({torch::indexing::Slice(),
                                   torch::indexing::Slice(0, HALO_SIZE + 1),
                                   torch::indexing::Slice(0, HALO_SIZE + 1)});
            break;
    }
    
    return boundary.contiguous();
}

void TileBoundaryOptimizer::apply_boundary_tendencies(
    torch::Tensor& u_tend,
    torch::Tensor& v_tend,
    torch::Tensor& w_tend,
    int neighbor_tile_id) {
    
    if (!can_reuse_boundary(neighbor_tile_id, current_step_)) {
        return;  // No cached data available
    }
    
    const auto& data = get_boundary_data(neighbor_tile_id);
    
    // Apply cached tendencies to boundary regions
    switch (data.type) {
        case TileBoundaryData::EAST:
            // Apply to eastern boundary
            u_tend.index({torch::indexing::Slice(),
                         torch::indexing::Slice(),
                         torch::indexing::Slice(-HALO_SIZE-1, -1)}) += 
                data.advection_tend * 0.5f;  // Weight for shared computation
            break;
            
        case TileBoundaryData::WEST:
            // Apply to western boundary
            u_tend.index({torch::indexing::Slice(),
                         torch::indexing::Slice(),
                         torch::indexing::Slice(0, HALO_SIZE+1)}) += 
                data.advection_tend * 0.5f;
            break;
            
        case TileBoundaryData::NORTH:
            // Apply to northern boundary
            v_tend.index({torch::indexing::Slice(),
                         torch::indexing::Slice(-HALO_SIZE-1, -1),
                         torch::indexing::Slice()}) += 
                data.advection_tend * 0.5f;
            break;
            
        case TileBoundaryData::SOUTH:
            // Apply to southern boundary
            v_tend.index({torch::indexing::Slice(),
                         torch::indexing::Slice(0, HALO_SIZE+1),
                         torch::indexing::Slice()}) += 
                data.advection_tend * 0.5f;
            break;
            
        default:
            // Corner cases - apply smaller weight
            break;
    }
}

void TileBoundaryOptimizer::cleanup_cache(int steps_to_keep) {
    // Remove old entries
    auto it = boundary_cache_.begin();
    while (it != boundary_cache_.end()) {
        if (current_step_ - it->second.computation_step > steps_to_keep) {
            it = boundary_cache_.erase(it);
        } else {
            ++it;
        }
    }
}

void TileBoundaryOptimizer::print_statistics() const {
    // FIX Round161: Gate statistics output with debug_level >= 2
    if (g_sdirk3_config.debug_level < 2) return;

    std::cout << "\n=== Tile " << tile_id_ << " Boundary Optimization Statistics ===" << std::endl;
    std::cout << "Cache hits: " << cache_hits_ << std::endl;
    std::cout << "Cache misses: " << cache_misses_ << std::endl;
    std::cout << "Hit rate: " << (get_cache_hit_rate() * 100) << "%" << std::endl;
    std::cout << "Computations saved: " << computations_saved_ << std::endl;
    std::cout << "Current cache size: " << boundary_cache_.size() << std::endl;
}

// TileBoundaryManager implementation
TileBoundaryManager& TileBoundaryManager::get_instance() {
    if (!instance_) {
        instance_ = std::make_unique<TileBoundaryManager>();
    }
    return *instance_;
}

void TileBoundaryManager::register_tile(int tile_id, int nx, int ny, int nz) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (tile_optimizers_.find(tile_id) == tile_optimizers_.end()) {
        tile_optimizers_[tile_id] = 
            std::make_unique<TileBoundaryOptimizer>(tile_id, nx, ny, nz);
    }
}

void TileBoundaryManager::add_connection(int tile1_id, int tile2_id,
                                        TileBoundaryData::BoundaryType type) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    connections_.push_back({tile1_id, tile2_id, type});
}

TileBoundaryOptimizer* TileBoundaryManager::get_tile_optimizer(int tile_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = tile_optimizers_.find(tile_id);
    if (it != tile_optimizers_.end()) {
        return it->second.get();
    }
    return nullptr;
}

void TileBoundaryManager::coordinate_boundary_exchange(int step) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    global_step_ = step;
    
    // Update all tile optimizers
    for (auto& [tile_id, optimizer] : tile_optimizers_) {
        optimizer->advance_step();
    }
    
    // Cleanup old cache entries periodically
    if (step % 10 == 0) {
        for (auto& [tile_id, optimizer] : tile_optimizers_) {
            optimizer->cleanup_cache();
        }
    }
}

void TileBoundaryManager::cleanup_all_caches() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& [tile_id, optimizer] : tile_optimizers_) {
        optimizer->cleanup_cache(0);  // Remove all
    }
}

void TileBoundaryManager::print_global_statistics() const {
    // FIX Round161: Gate statistics output with debug_level >= 2
    if (g_sdirk3_config.debug_level < 2) return;

    std::lock_guard<std::mutex> lock(mutex_);

    std::cout << "\n=== Global Tile Boundary Optimization Statistics ===" << std::endl;
    std::cout << "Number of tiles: " << tile_optimizers_.size() << std::endl;
    std::cout << "Number of connections: " << connections_.size() << std::endl;
    std::cout << "Global step: " << global_step_ << std::endl;

    size_t total_saved = 0;
    double avg_hit_rate = 0.0;

    for (const auto& [tile_id, optimizer] : tile_optimizers_) {
        total_saved += optimizer->get_computations_saved();
        avg_hit_rate += optimizer->get_cache_hit_rate();
    }

    if (!tile_optimizers_.empty()) {
        avg_hit_rate /= tile_optimizers_.size();
    }

    std::cout << "Total computations saved: " << total_saved << std::endl;
    std::cout << "Average cache hit rate: " << (avg_hit_rate * 100) << "%" << std::endl;
}

// BoundaryFluxComputer implementation
BoundaryFluxComputer::BoundaryFluxComputer(float dx, float dy, int nx, int ny, int nz)
    : dx_(dx), dy_(dy) {
    // FIX 2025-12-28: Removed unused interp_weights_x_/y_ initialization.
    // compute_boundary_flux() uses hardcoded 0.5f averaging inline.
}

std::pair<torch::Tensor, torch::Tensor> BoundaryFluxComputer::compute_boundary_flux(
    const torch::Tensor& u,
    const torch::Tensor& v,
    const torch::Tensor& rho,
    TileBoundaryData::BoundaryType type) {
    
    torch::Tensor flux_x, flux_y;
    
    switch (type) {
        case TileBoundaryData::EAST:
        case TileBoundaryData::WEST: {
            // Compute momentum flux at x-boundary
            flux_x = rho * u * u;
            
            // Interpolate v to u-points for cross flux
            auto v_at_u = (v.index({torch::indexing::Slice(),
                                   torch::indexing::Slice(0, -1),
                                   torch::indexing::Slice()}) +
                          v.index({torch::indexing::Slice(),
                                   torch::indexing::Slice(1, torch::indexing::None),
                                   torch::indexing::Slice()})) * 0.5f;
            flux_y = rho * u * v_at_u;
            break;
        }
            
        case TileBoundaryData::NORTH:
        case TileBoundaryData::SOUTH: {
            // Compute momentum flux at y-boundary
            // Interpolate u to v-points
            auto u_at_v = (u.index({torch::indexing::Slice(),
                                   torch::indexing::Slice(),
                                   torch::indexing::Slice(0, -1)}) +
                          u.index({torch::indexing::Slice(),
                                   torch::indexing::Slice(),
                                   torch::indexing::Slice(1, torch::indexing::None)})) * 0.5f;
            flux_x = rho * v * u_at_v;
            flux_y = rho * v * v;
            break;
        }
            
        default:
            // Corner cases - use simple average
            flux_x = rho * u * u;
            flux_y = rho * v * v;
    }
    
    return {flux_x, flux_y};
}

std::pair<torch::Tensor, torch::Tensor> BoundaryFluxComputer::compute_boundary_pressure_gradient(
    const torch::Tensor& p,
    const torch::Tensor& mu,
    TileBoundaryData::BoundaryType type) {
    
    torch::Tensor grad_x, grad_y;
    
    // Compute pressure gradient including metric terms
    switch (type) {
        case TileBoundaryData::EAST:
            grad_x = torch::zeros_like(p);  // Boundary normal
            grad_y = (p.roll({-1}, {1}) - p.roll({1}, {1})) / (2.0f * dy_);
            break;
            
        case TileBoundaryData::WEST:
            grad_x = torch::zeros_like(p);  // Boundary normal
            grad_y = (p.roll({-1}, {1}) - p.roll({1}, {1})) / (2.0f * dy_);
            break;
            
        case TileBoundaryData::NORTH:
            grad_x = (p.roll({-1}, {2}) - p.roll({1}, {2})) / (2.0f * dx_);
            grad_y = torch::zeros_like(p);  // Boundary normal
            break;
            
        case TileBoundaryData::SOUTH:
            grad_x = (p.roll({-1}, {2}) - p.roll({1}, {2})) / (2.0f * dx_);
            grad_y = torch::zeros_like(p);  // Boundary normal
            break;
            
        default:
            // Corner cases
            grad_x = (p.roll({-1}, {2}) - p.roll({1}, {2})) / (2.0f * dx_);
            grad_y = (p.roll({-1}, {1}) - p.roll({1}, {1})) / (2.0f * dy_);
    }
    
    // Include dry air mass (mu) in pressure gradient
    if (mu.defined() && mu.numel() > 0) {
        grad_x = grad_x / mu;
        grad_y = grad_y / mu;
    }
    
    return {grad_x, grad_y};
}

torch::Tensor BoundaryFluxComputer::compute_boundary_advection(
    const torch::Tensor& u,
    const torch::Tensor& v,
    const torch::Tensor& w,
    const torch::Tensor& scalar,
    TileBoundaryData::BoundaryType type) {
    
    // Compute advection tendency: -div(u*scalar)
    torch::Tensor advection;
    
    // Compute fluxes
    auto flux_x = u * scalar;
    auto flux_y = v * scalar;
    auto flux_z = w * scalar;
    
    // Compute divergence based on boundary type
    switch (type) {
        case TileBoundaryData::EAST:
        case TileBoundaryData::WEST:
            // Focus on x-direction flux divergence
            advection = -(flux_y.roll({-1}, {1}) - flux_y.roll({1}, {1})) / (2.0f * dy_) -
                       (flux_z.roll({-1}, {0}) - flux_z.roll({1}, {0})) / (2.0f * 1.0f);
            break;
            
        case TileBoundaryData::NORTH:
        case TileBoundaryData::SOUTH:
            // Focus on y-direction flux divergence
            advection = -(flux_x.roll({-1}, {2}) - flux_x.roll({1}, {2})) / (2.0f * dx_) -
                       (flux_z.roll({-1}, {0}) - flux_z.roll({1}, {0})) / (2.0f * 1.0f);
            break;
            
        default:
            // Full divergence for corners
            advection = -(flux_x.roll({-1}, {2}) - flux_x.roll({1}, {2})) / (2.0f * dx_) -
                       (flux_y.roll({-1}, {1}) - flux_y.roll({1}, {1})) / (2.0f * dy_) -
                       (flux_z.roll({-1}, {0}) - flux_z.roll({1}, {0})) / (2.0f * 1.0f);
    }
    
    return advection;
}

} // namespace sdirk3
} // namespace wrf