# A4 first-direction defect: native mode-3 V-row audit

Read-only source/math audit at `/private/tmp/pressure-a4-main-20261031`, HEAD `b0be4a94578739db91c77ee4d80dcc739483e03c`. The cached A4 graph's tile, Newton and reference-model source hashes match this worktree. The acoustic, nonhydrostatic, EOS and mass-flux headers were absent from that bounded graph; `vrow_graph.json` and `vrow_source_pins.json` add a 22-node/48-edge AST navigation extract. C++ lambda, runtime branch and Fortran edges remain possible extraction gaps; equations below were checked against source, not inferred from graph edges. No code change, new test or model was run.

## What the saved direction actually shows

The saved pair is the same first Arnoldi V0 in scaled coordinates and `A(M⁻¹V0)` before D weighting/orthogonalization, transformed to physical rate coordinates; A0 is raw and B0 is P0. Only V0 and A(M⁻¹V0) were saved, not the two different preconditioned vectors `z=M⁻¹V0`. Hence their `e=V0−A z` comparison locates a defect but does not separate operator terms or identify which coefficient causes it.

For this one direction, V defect RMS falls from `3.3097246e−7` to `2.1115254e−7 m/s²`; the reported shape error falls `0.8706393→0.7453989`. Its high-Y reconstructed defect RMS falls `1.9922086e−7→1.5730045e−7 m/s²`, while the *fraction* of that V defect's squared norm in the high band rises `0.3623→0.5550`; these are different statements. W over all 64 owned W faces, including the dynamic top, improves `4.8337648e−5→5.7875341e−6 m/s²` (8.35× lower). Partitioning afterward, top alone changes `3.8629068e−4→6.6735334e−6`, with its share of W squared defect `99.7878%→2.0775%`; the remaining 63 interior faces change `2.2442629e−6→5.7723743e−6`, or **2.572× larger** under P0. The partition is diagnostic, not a replacement acceptance criterion. A0/B0 use different z even though V0 matches. Neither this pattern nor the A4 timing NO-GO establishes a cause for the 36-hour W bands.

## Native V-face primary pressure-gradient derivative

At interior V face `(j,k,i)` let `Δ_y f=f[j,k,i]−f[j−1,k,i]`, and let an overbar be the corresponding two-mass-cell average. `p′` is pressure perturbation, `pb` fixed base pressure, `α=alt` full inverse density, `α′=al` its perturbation, and `φ′_m=(φ′_k+φ′_(k+1))/2`. The actual primary coupled expression is assembled by `horizontal_pgf_primary` (`wrf_sdirk3_acoustic_substep.h:270–279`) and called at `tile_unified_impl.cpp:21050–21218`; the canonical velocity conversion is at `:21317–21325,26157–26180`.

For the dry WRFParity branch with fixed map factors and nonzero valid mass, its primary **physical** V acceleration simplifies algebraically to

    fV_primary = −msfvy*rdy [ Δ_y φ′_m + αbar Δ_y p′ + α′bar Δ_y pb ].

The apparent hybrid layer-mass/map denominator in the coupled accumulator cancels with its canonical physical-rate conversion; this statement covers the primary PGF contribution, not the later mass product-rule conversion. Its frozen-stage directional derivative is

    δfV_primary = −msfvy*rdy [ Δ_y δφ′_m
                                + αbar Δ_y δp′
                                + δαbar Δ_y(p′+pb) ].

EOS at the same projected stage supplies `δp′=Eφ δφ′+Eθ δt′+Eμ δμ′` and, with fixed base/coefficient arrays, `δα=(|rdnw|/H) Δ_z δφ′ − (α c1h/H)δμ′` (`wrf_hydrostatic_pressure.h:83–111`). Thus the primary derivative is not just a constant coefficient times `G_y δp`: it also contains a direct horizontal geopotential-gradient term, actual face-varying α multiplying Δδp, and `δαbar` multiplying the frozen *full* pressure gradient. Whether the latter is large in the captured first direction is unmeasured. At the periodic-X/symmetric-Y physical core, V unknowns are interior Y faces; both wall-normal faces are constrained, and the packed aliases/projection must follow the existing adapter and native projection, not a collocated centered gradient.

## Native nonhydrostatic coordinate term and mass/Ω paths

When `non_hydrostatic_ && canonical_horizontal`, the native RHS also adds `native_nonhydrostatic_pressure_core` (`tile_unified_impl.cpp:20472–20508`, `wrf_sdirk3_nonhydrostatic_pressure.h:119–196`). Define `php=(φb_k+φb_(k+1)+φ′_k+φ′_(k+1))/2`, `Z_v=rdnw_signed*(dpn_(k+1)−dpn_k)−c1h*mu′_v`, and `L_v=c1h*(mub+mu′)_v+c2h`. With fixed unit maps/cq=1 its converted V tendency contains

    fV_NH4 = −rdy (Δ_y php) Z_v/L_v,
    δfV_NH4 = −rdy [ (Δ_y δphp) Z_v/L_v
                    + (Δ_y php)(δZ_v/L_v − Z_v δL_v/L_v²) ].

Here `δL_v=c1h*δmu′_v` and `δZ_v=rdnw_signed*Δ_z δdpn_v−c1h*δmu′_v`; `δdpn_v` is the source's face average/vertical interpolation of δp′, with its configured bottom and top extrapolations. Ground flatness does not imply `Δ_y php=0` aloft: horizontally varying geopotential/eta surfaces and backgrounds remain possible. Nor does it guarantee this term materially explains A4. Evaluate the actual frozen factors/direction before assigning it.

WRFParity Fast continuity is the *column sum* of current hybrid mass fluxes, and Ω is its vertical recurrence (`wrf_sdirk3_ww_cp.h:285–315`, tile `:18714–18722`): `δcu=(H_u δu+c1h u δM_u)/msfuy` and similarly at V faces, with signed dnw and map factors. This couples δu/δv and δμ to μdot and Ω. The canonical physical conversion contains `−v*c1h*stagger(μdot)/L_v` (`tile:26157–26180`); its derivative contains both `−δv*rate_v` and `−v*δrate_v`, including `stagger(δμdot)` and δL_v. Phi's Fast horizontal and Ω/vertical transport (`:19527–19535,20152–20170,20239`) also feeds δφ and hence δp. These are separate from the V primary PGF row and from the proposed P0 `Cμ/Ct` reference profile. In mode 3, coupled momentum/theta advection is Slow; do not accidentally attribute mode-4 implicit transport to this Fast comparison.

## One bounded next candidate

Test **one native-primary V-row defect correction** on the same frozen A and the actual preconditioned first direction, before changing pressure inversion or adding multigrid. Let `z=P0⁻¹ r` in physical rate coordinates. Evaluate a direct frozen analytic action `Jv_primary(z)` using the three source-consistent terms above and live EOS E/δα at the same stage, and the already-defined P0 V action `Jv_P0(z)=−αref G_y(Eref z)`. Define only

    ΔJv(z)=Jv_primary(z)−Jv_P0(z),
    z_v,new = z_v + a ΔJv(z),    all other z blocks initially unchanged.

This is a proposed one-pass V-row approximate correction with the sign coming from `A=I−aJ`, not an exact block-triangular solve, `P0⁻¹` applied to a full residual correction, or a new damping coefficient. It does not yet include NH4, μ-conversion, Ω or horizontal W-row effects; their omission must be reported. It does not assert that ΔJv dominates the saved V defect. First capture the actual z and exact same U_eval/context (the present first-direction files do not contain z), verify this analytic action against a narrowly isolated native FWAD/central-FD V-primary action, then report signed correlations and component norms of `(A−P0)z` by V term, plus induced changes in all other row residuals. Verify full packed projection/complement and `S⁻¹P⁻¹S` scaling rather than applying a physical-row formula directly to Krylov coordinates. If source-consistent primary action does not explain a meaningful part of that *same* defect, stop this candidate rather than add terms speculatively. If it does, compare equal-budget FGMRES true residual and complete stage cost including the correction. The present A4 already failed its registered performance gate; no faster result, native Schur equivalence, or forecast-noise explanation is implied.
