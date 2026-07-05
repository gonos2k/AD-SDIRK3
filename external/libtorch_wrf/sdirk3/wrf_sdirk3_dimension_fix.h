#ifndef WRF_SDIRK3_DIMENSION_FIX_H
#define WRF_SDIRK3_DIMENSION_FIX_H

// This file documents the critical dimension ordering fix for WRF SDIRK3
// 
// PROBLEM: The current implementation uses incorrect tensor dimension ordering
// - Current (WRONG): tensors shaped as [x][y][z] accessed as tensor[i][j][k]
// - Correct (per WRF): tensors shaped as [y][z][x] accessed as tensor[j][k][i]
//
// WRF Memory Layout (from wrf_memory_layout.h):
// - Fortran arrays: (i,k,j) with i fastest varying (column-major)
// - C++ tensors: [j,k,i] with i fastest varying (row-major)
//
// This means:
// - i (x-direction) should be the LAST dimension in C++ tensors
// - j (y-direction) should be the FIRST dimension in C++ tensors
// - k (z-direction) should be the MIDDLE dimension in C++ tensors

namespace wrf {
namespace sdirk3 {

// Correct tensor shapes for WRF staggered grid variables
struct CorrectTensorShapes {
    // For grid with nx, ny, nz mass points:
    
    // 3D variables at mass points
    // Shape: [ny, nz, nx]
    // Access: tensor[j][k][i]
    
    // U-staggered (staggered in x)
    // Shape: [ny, nz, nx+1]
    // Access: u[j][k][i] where i goes from 0 to nx
    
    // V-staggered (staggered in y) 
    // Shape: [ny+1, nz, nx]
    // Access: v[j][k][i] where j goes from 0 to ny
    
    // W-staggered (staggered in z)
    // Shape: [ny, nz+1, nx]
    // Access: w[j][k][i] where k goes from 0 to nz
    
    // 2D variables at mass points
    // Shape: [ny, nx]
    // Access: tensor[j][i]
};

// Correct interpolation patterns
struct CorrectInterpolations {
    // Mass to U-staggered (average in x-direction = last dimension)
    // mu_at_u[j][k][i] = 0.5 * (mu[j][k][i-1] + mu[j][k][i])
    
    // Mass to V-staggered (average in y-direction = first dimension)
    // mu_at_v[j][k][i] = 0.5 * (mu[j-1][k][i] + mu[j][k][i])
    
    // Mass to W-staggered (average in z-direction = middle dimension)
    // mu_at_w[j][k][i] = 0.5 * (mu[j][k-1][i] + mu[j][k][i])
    
    // U to mass points (average in x-direction = last dimension)
    // u_mass[j][k][i] = 0.5 * (u[j][k][i] + u[j][k][i+1])
    
    // V to mass points (average in y-direction = first dimension)
    // v_mass[j][k][i] = 0.5 * (v[j][k][i] + v[j+1][k][i])
    
    // W to mass points (average in z-direction = middle dimension)
    // w_mass[j][k][i] = 0.5 * (w[j][k][i] + w[j][k+1][i])
};

// Correct derivative patterns
struct CorrectDerivatives {
    // X-derivative (operates on last dimension)
    // dudx[j][k][i] = (u[j][k][i+1] - u[j][k][i]) * rdx
    
    // Y-derivative (operates on first dimension)
    // dudy[j][k][i] = (u[j+1][k][i] - u[j][k][i]) * rdy
    
    // Z-derivative (operates on middle dimension)
    // dudz[j][k][i] = (u[j][k+1][i] - u[j][k][i]) * rdz
};

// Map scale factor ordering (these are already correct)
struct MapScaleFactors {
    // 2D arrays use [j][i] ordering (y then x)
    // msfux[j][i], msfuy[j][i], msfvx[j][i], msfvy[j][i]
    // msftx[j][i], msfty[j][i]
};

} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_DIMENSION_FIX_H