# Divergence-damping sign review

Local timestamp: 2026-10-05 15:23 Asia/Tokyo

## Finding

The C++ implicit divergence term had the opposite Fourier sign to the WRF small-step U/V divergence filter. For positive damping coefficients it amplified longitudinal velocity modes. The active C++ defaults are `implicit_divergence=true` and `kdamp=0.2` in `wrf_sdirk3_config.h`; `computeUnifiedRHS()` invokes the helper for the implicit RHS when `split_explicit` is false. The inviscid native-wave fixture now disables this unrelated term and reads back both flags/coefficient.

## Source sign

In `dyn_em/module_small_step_em.F`, `advance_mu_t` forms positive mass-flux divergence in `dvdxi` (lines 1094-1098), multiplies it by negative `dnw` when accumulating `DMDT` (1099), and stores `MUDF = DMDT + MU_TEND` (1102-1105). Thus the divergence part of `MUDF` has the negative-divergence sign. `advance_uv` then forms `MUDF_XY = -emdiv * D(MUDF)` for U (809) and V (880), and adds `c1h * MUDF_XY` to the corresponding velocities (U 868, V 942).

For positive uniform layer mass, positive `emdiv`, and unit map factors, this gives U/V terms proportional to `+grad(div)`. On a C-grid Fourier mode, the centered derivative symbol is `iκ`, so `grad(div)` has symbol `-κ²`; the resulting linear energy rate is negative. This is the damping sign.

The C++ helper computes positive `div = Dx(U) + Dy(V)` and applies `ru_tend -= kdamp * Dx(div)` and `rv_tend -= kdamp * Dy(div)` (`wrf_sdirk3_tile_unified_impl.cpp`, lines 25627-25649 and 25652-25671 before the sign edit). That has symbol `+kdamp κ²` and grows the mode. The reviewed diff changes both subtractions to additions; W remains untouched, consistent with the existing comment that W receives no divergence damping.

## Scope and limits

This establishes sign parity in the uniform positive-mass, unit-map limit. It does not establish equal coefficient normalization or general metric parity: WRF passes mass tendency `MUDF` and coefficient `emdiv`, while the C++ helper applies `kdamp` directly to velocity divergence. The change has no ABI effect. It affects only the existing implicit divergence RHS path; split-explicit execution continues to skip this helper.

No model run or post-edit test was performed in this review. The root agent is relinking and running the targeted validation.
