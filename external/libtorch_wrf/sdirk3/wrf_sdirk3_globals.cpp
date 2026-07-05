// Global variables for SDIRK3 solver
#include <unordered_map>
#include <memory>
#include <mutex>
#include <iostream>
#include <cstdio>

namespace wrf {
namespace sdirk3 {

// Forward declaration - sufficient for raw pointers
class TileSDIRK3Solver;

// Global map of tile solvers - using raw pointer instead of unique_ptr
// The actual ownership is managed elsewhere
std::unordered_map<void*, TileSDIRK3Solver*> g_tile_solvers;

// Global mutex for thread safety
std::mutex g_solver_mutex;

} // namespace sdirk3
} // namespace wrf