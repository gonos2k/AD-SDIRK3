/**
 * wrf_sdirk3_mpi_safety_impl.cpp
 *
 * FIX 2025-01-26: Provides extern "C" symbol definitions for Fortran linkage.
 *
 * NOTE: Fortran name mangling varies by platform:
 *   - Linux GFortran: adds trailing underscore (sdirk3_notify_halo_fresh_)
 *   - macOS GFortran: NO trailing underscore (sdirk3_notify_halo_fresh)
 *
 * This file provides BOTH versions for cross-platform compatibility.
 */

#include "wrf_sdirk3_mpi_safety.h"

extern "C" {

// =============================================================================
// Primary implementations (without trailing underscore - for macOS GFortran)
// =============================================================================

void sdirk3_notify_halo_fresh(void) {
    wrf::sdirk3::mpi_safety::HaloFreshnessGuard::markHaloFresh();
}

void sdirk3_set_timestep(int64_t* timestep) {
    wrf::sdirk3::mpi_safety::PeriodicBCGuard::newTimestep(
        static_cast<uint64_t>(*timestep)
    );
}

void sdirk3_set_timestep_i4(int* timestep) {
    wrf::sdirk3::mpi_safety::PeriodicBCGuard::newTimestep(
        static_cast<uint64_t>(*timestep)
    );
}

void sdirk3_mpi_safety_init(void) {
    wrf::sdirk3::mpi_safety::initializeMPISafety();
}

// =============================================================================
// Alternate symbols (with trailing underscore - for Linux GFortran)
// =============================================================================

void sdirk3_notify_halo_fresh_(void) {
    wrf::sdirk3::mpi_safety::HaloFreshnessGuard::markHaloFresh();
}

void sdirk3_set_timestep_(int64_t* timestep) {
    wrf::sdirk3::mpi_safety::PeriodicBCGuard::newTimestep(
        static_cast<uint64_t>(*timestep)
    );
}

void sdirk3_set_timestep_i4_(int* timestep) {
    wrf::sdirk3::mpi_safety::PeriodicBCGuard::newTimestep(
        static_cast<uint64_t>(*timestep)
    );
}

void sdirk3_mpi_safety_init_(void) {
    wrf::sdirk3::mpi_safety::initializeMPISafety();
}

} // extern "C"
