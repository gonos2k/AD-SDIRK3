#ifndef WRF_SDIRK3_INTERFACE_PARAMS_H
#define WRF_SDIRK3_INTERFACE_PARAMS_H

#include <cstdint>
#include <cstddef>

// Parameter structs for Fortran-C++ interface
// These structs reduce the parameter count from 127 to ~70 to avoid gfortran BIND(C) limitations
// All structs use explicit packing to ensure ABI compatibility with Fortran BIND(C)

#pragma pack(push, 1)

// Index bounds: 18 integers (tile, domain, memory bounds)
struct SDIRK3_IndexBounds {
    int32_t its, ite;  // Tile bounds
    int32_t jts, jte;
    int32_t kts, kte;

    int32_t ids, ide;  // Domain bounds
    int32_t jds, jde;
    int32_t kds, kde;

    int32_t ims, ime;  // Memory bounds
    int32_t jms, jme;
    int32_t kms, kme;
};

// Dimensions: 7 integers (grid dimensions and RK step)
struct SDIRK3_Dimensions {
    int32_t nx, ny, nz;      // Mass point dimensions
    int32_t nx_u;            // U-staggered dimension (nx+1)
    int32_t ny_v;            // V-staggered dimension (ny+1)
    int32_t nz_w;            // W-staggered dimension (nz+1)
    int32_t rk_step;         // Current RK step (0-2 for SDIRK3)
};

// Scalar parameters: 7 floats (grid spacing, damping, diffusion, timestep)
struct SDIRK3_ScalarParams {
    float rdx, rdy;          // Reciprocal grid spacing (1/dx, 1/dy)
    float kdamp;             // Rayleigh damping coefficient
    float khdif;             // Horizontal diffusion coefficient
    float kvdif;             // Vertical diffusion coefficient
    float dtbc;              // Boundary condition timestep
    float dt;                // Physics timestep
};

// Boundary zone configuration: 4 integers
struct SDIRK3_BoundaryConfig {
    int32_t spec_bdy_width;  // Specified boundary width
    int32_t spec_zone;       // Specified zone width
    int32_t relax_zone;      // Relaxation zone width
    int32_t padding;         // Padding for alignment (unused)
};

#pragma pack(pop)

// FIX 2025-01-11 Round60/Round64/Round65: Optional FP64 scalar parameters for precision-critical paths
// FIX Round64: Moved OUTSIDE #pragma pack to ensure proper 8-byte alignment for doubles
// on non-x86 architectures (ARM, POWER, etc.). Fortran BIND(C) for REAL(C_DOUBLE) also
// requires natural 8-byte alignment, so this struct must NOT be packed.
//
// ABI EQUIVALENCE (FIX Round65):
//   - C++ impl:   SDIRK3_ScalarParams_FP64   (this file, alignas(8))
//   - C API:      SDIRK3_ScalarParams_FP64_C (wrf_sdirk3_interface.h, for extern "C")
//   - Fortran:    SDIRK3_ScalarParams_FP64   (no Fortran binding since the dormant bridge was removed; C ABI type retained)
//   All three MUST have identical layout: 4 × double (32 bytes total, 8-byte aligned).
//   Modification to any requires updating all three and re-verifying static_asserts.
//
// This struct is OPTIONAL - pass NULL from Fortran if FP64 precision not needed
// When provided, these values are used instead of float rdx/rdy for:
//   - Computing grid_info_->dx_fp64/dy_fp64 with full precision
//   - Constructing FP64 rdx/rdy tensors when compute_dtype is kFloat64
// Fortran usage: TYPE(C_PTR) :: fp64_scalars_ptr = C_NULL_PTR  ! or C_LOC(fp64_scalars)
struct alignas(8) SDIRK3_ScalarParams_FP64 {
    double rdx_fp64;         // Reciprocal grid spacing (1/dx) in double precision
    double rdy_fp64;         // Reciprocal grid spacing (1/dy) in double precision
    double dx_fp64;          // Grid spacing (dx) in double precision - optional, computed if 0
    double dy_fp64;          // Grid spacing (dy) in double precision - optional, computed if 0
};

// Static assertions to verify struct sizes match Fortran expectations
// Fortran INTEGER(C_INT) = 4 bytes, REAL(C_FLOAT) = 4 bytes
static_assert(sizeof(SDIRK3_IndexBounds) == 18 * 4,
              "SDIRK3_IndexBounds size mismatch: expected 72 bytes");
static_assert(sizeof(SDIRK3_Dimensions) == 7 * 4,
              "SDIRK3_Dimensions size mismatch: expected 28 bytes");
static_assert(sizeof(SDIRK3_ScalarParams) == 7 * 4,
              "SDIRK3_ScalarParams size mismatch: expected 28 bytes");
static_assert(sizeof(SDIRK3_BoundaryConfig) == 4 * 4,
              "SDIRK3_BoundaryConfig size mismatch: expected 16 bytes");
static_assert(sizeof(SDIRK3_ScalarParams_FP64) == 4 * 8,
              "SDIRK3_ScalarParams_FP64 size mismatch: expected 32 bytes");

// Verify field offsets for debugging
static_assert(offsetof(SDIRK3_IndexBounds, its) == 0, "its offset");
static_assert(offsetof(SDIRK3_IndexBounds, ite) == 4, "ite offset");
static_assert(offsetof(SDIRK3_IndexBounds, ims) == 48, "ims offset");
static_assert(offsetof(SDIRK3_IndexBounds, ime) == 52, "ime offset");

static_assert(offsetof(SDIRK3_Dimensions, nx) == 0, "nx offset");
static_assert(offsetof(SDIRK3_Dimensions, rk_step) == 24, "rk_step offset");

static_assert(offsetof(SDIRK3_ScalarParams, rdx) == 0, "rdx offset");
static_assert(offsetof(SDIRK3_ScalarParams, dt) == 24, "dt offset");

static_assert(offsetof(SDIRK3_BoundaryConfig, spec_bdy_width) == 0, "spec_bdy_width offset");

// FIX 2025-01-11 Round61: Verify FP64 scalar struct offsets for Fortran BIND(C) interop
static_assert(offsetof(SDIRK3_ScalarParams_FP64, rdx_fp64) == 0, "rdx_fp64 offset");
static_assert(offsetof(SDIRK3_ScalarParams_FP64, rdy_fp64) == 8, "rdy_fp64 offset");
static_assert(offsetof(SDIRK3_ScalarParams_FP64, dx_fp64) == 16, "dx_fp64 offset");
static_assert(offsetof(SDIRK3_ScalarParams_FP64, dy_fp64) == 24, "dy_fp64 offset");

#endif // WRF_SDIRK3_INTERFACE_PARAMS_H
