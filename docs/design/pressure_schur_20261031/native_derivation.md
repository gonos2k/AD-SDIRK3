# Native pressure principal and Gate A1

Target `/private/tmp/pressure-schur-stage-20261031` is at `be66161fa03b50d5b0006b5faec016135517d6da`. The native-build source and target have identical SHA-256 for the preconditioner (`1f2dcc492f3c8441efc8b470027687640e8861b309862f6cdc372f7c892b0979`), EOS (`f926092be30df9a21b2e9967f7f4bf7d18b1fe2790cc8d2145d316aa9f73591b`), mass-flux helper (`664be7f52560ba6bfc8ef7d8fce9dc17911f78530388dc04f9c289f9d0d4a0f7`), tile implementation (`1447fe7d382178a0c146b5c475ce273ef9c1f50798d5a1d2f05af8f563be323d`) and Newton solver (`a17e951c8e09a53e08226cab02a8f09c403484c8d541fd99f25b77b6acf2dad6`). The cached integrated graph pins match the latter two; the former three were absent. A bounded extraction here adds 95 nodes/285 edges. Closure, overload and Fortran edges may be absent: these are navigation aids, and equations were checked directly against source.

The completed OFF native case is one rank, 40×80×64 mass cells, dx=dy=100 km, h=600 s, mode 3, WRFParity, periodic X/symmetric Y, internal FP64 and carry on, prepared reuse off and preconditioner type 2. Its summary reports convergence at stages 2, 3 and 4; it does not save their Newton operands. Stage operand diagnostics were off. Initial/output fields are not a saved `U_eval`, frozen F, S, b, P or reference context and cannot reconstruct Gate A by themselves. The supplied 340 m/s and rigid-top example is synthetic and is not used here.

## EOS tangent

At mass cell k let M=mub+mu′ [Pa], H=c1h M+c2h [Pa], R=|rdnw| (inverse eta), Θ=t0+t′ [K], Δφ=φ′(k+1)−φ′(k) [m²/s²], and αb=alb [m³/kg]. The actual `wrf_hydrostatic_pressure.h:83–111` computes

    α = αb − (αb c1h mu′ − R Δφ)/H
      = [αb(c1h mub+c2h)+R Δφ]/H
    p = p0 [Rd Θ/(p0 α)]^κ, κ=cp/cv.

At a fixed positive stage state, with fixed base and metric coefficients, the exact smooth tangent is

    δp_k = −Q_k (δφ′_(k+1)−δφ′_k) + T_k δt′_k + M_k δmu′
    Q_k=κ p_k R_k/(α_k H_k),  T_k=κ p_k/Θ_k,  M_k=κ p_k c1h_k/H_k.

The units are Q: Pa/(m²/s²), T: Pa/K and M: Pa/Pa. One column mu′ is shared across all vertical levels. The `p_pert=p−p_base` tangent is the same for fixed base. Evaluate these coefficients at the actual stage p, α, H and Θ, not at an assumed sound speed. Respect source casts, projection and any sanitization in comparisons.

## Existing raw vertical block

`wrf_sdirk3_unified_preconditioner.cpp:5421–5635` recomputes Q and T from the bound full stage state and `grid_info_->alb`. The RHS computes its base inverse density from `th_base_` and `p_base_` before calling the EOS (`tile_unified_impl.cpp:20410–20417`); actual equality with the preconditioner's imported `alb` must be checked at the stage rather than assumed. For interior W level l=1…nz−1, write Ll=c1f_l M+c2f_l and al=g rdn_l/Ll, where the raw contract requires positive stored rdn. Its physical-rate principal is

    K_l δφ = al [Q_(l−1) δφ_(l−1) − (Q_l+Q_(l−1)) δφ_l + Q_l δφ_(l+1)]
    B_l δt = al [T_(l−1) δt_(l−1) − T_l δt_l].

For the physical top W level nz, `atop=2 g |rdnw_(nz−1)|/(c1f_nz M+c2f_nz)`, and

    K_top δφ = atop Q_(nz−1) (δφ_(nz−1)−δφ_nz)
    B_top δt = atop T_(nz−1) δt_(nz−1).

This is the native one-sided **dynamic top row**, not rigid W or a Neumann pressure lid. With active-pair G=gI, W0 and φ0 remain identity rows. For a=hγ the model equations `δw−aKδφ−aBδt=bw`, `δφ−aGδw=bφ`, `δt=bt` reduce to `(I−a²KG)δw=bw+aK bφ+aB bt`, then `δφ=bφ+aGδw`. The code omits EOS Emu-to-W and other background/nonprincipal terms. It stores its coefficients and applies Thomas in FP32 even for an FP64 state (`:5500–5532,5640–5732`); the actual floating-point apply is not an exact double-precision inverse.

At h=600, the source ARK324 implicit diagonal is `1767732205903/4055673282236`, so the mathematical a is 261.5199129050754 s. Newton takes double dt and gamma; `update_time_coefficients(float dt,float gamma)` forms its raw h in FP32 (`:5844–5864`), approximately 261.5199279785156 s for direct rounding. Gate A must record actual stage-bound bytes rather than assume those values coincide.

## Horizontal coupling and a pressure Schur candidate

The actual mass-flux helper (`wrf_sdirk3_ww_cp.h:180–315`) forms

    cu_k=(c1h_k M_u+c2h_k) u_k/msfuy
    cv_k=(c1h_k M_v+c2h_k) v_k/msfvx
    divv_k=msftx dnw_k [rdx(cu_e−cu_w)+rdy(cv_n−cv_s)]
    dmdt_over_map_y=sum_k divv_k;  mudot=msfty*dmdt_over_map_y.

The signed dnw here is negative. Face derivatives include both L_face δvelocity and c1 velocity δM_face; dropping the latter is a stated frozen-mass approximation. Omega has zero boundary *flux* at its endpoints and an interior recurrence from `dnw*c1*dmdt+divv`. This does not impose rigid W/φ at the top. Mode-3 Fast theta has no raw advective theta term, but its product conversion is `tdot_I=−t′ c1h mudot/(H+eps)` (`tile_unified_impl.cpp:26120–26155`). Thus its frozen horizontal-velocity action includes `C_theta,v=−t′ c1h C_mu,v/(H+eps)`. PH has further horizontal advection and diagnosed-Omega/vertical-Phi-gradient dependence (`:195xx–201xx`). At general backgrounds W conversion and other terms may also depend on horizontal velocities. These couplings and all map/stagger weights must use the actual frozen stage state.

Let v=(δu,δv), z=(δw,δφ,δt,δmu), and δp=Ez using the EOS above. Let Hh be the *chosen signed* frozen partial pressure-to-horizontal-acceleration map, C the selected horizontal-velocity-to-z tendency maps, and Pz the existing raw vertical W/φ block plus identity theta/mu. Define the approximate physical packed model

    δv − a Hh δp = b_v
    Pz δz − a C δv = b_z,       δp=E δz.

Its algebraic pressure elimination, when Pz and the resulting Schur are invertible, is

    [I−a² E Pz⁻¹ C Hh] δp = E Pz⁻¹(b_z+a C b_v)
    δz=Pz⁻¹(b_z+a C b_v+a² C Hh δp);   δv=b_v+a Hh δp.

This is exact **for the explicitly defined approximate block**, not the full WRF Jacobian. An abstract exact augmentation `A=A0−aHE` gives `Sp=I−a E A0⁻¹H` only after proving all pressure dependence is isolated into H E and A0 is invertible. Native Hh is not automatically `−G/rho`: terrain/hybrid PGF, `native_nonhydrostatic_pressure_core` (`tile_unified_impl.cpp:20445–20478`), maps, mass conversion and alpha perturbations introduce extra state/geometric paths. Check its independent-pressure action against FWAD and enumerate omissions. Keeping C_mu but omitting nonzero-t′ C_theta changes the EOS feedback. Re-inserting the vertical K/B pressure action already inside Pz double-counts it; explicitly retain or report the omitted Emu-to-W term.

No SPD, symmetry or simple spectrum is guaranteed for this nonuniform terrain-following operator. Preserve actual projection, periodic seam, symmetric wall, bottom and dynamic-top rows. The result may be nonsymmetric/non-normal, and a flexible outer solver may be needed.

## Scaling and reuse

The Newton unknown is a tendency correction dK with physical blocks U/V/W m/s², φ m²/s³, t K/s and mu Pa/s. The true native operator is `A dK=dK−J_I(U_eval)*(a dK)` (`newton_solver.cpp:7021`); E maps the state-rate blocks to pressure rate Pa/s. For `Ahat=S⁻¹AS`, a physical P inverse must be applied as `S⁻¹P⁻¹S` (`:7880–7910`). The solver’s optional D-weighted norm is separate from both this coordinate transform and the independently recomputed original physical `b−A dK`; report them separately.

Public `UnifiedPreconditioner` APIs (`.h:135–162,249–253`) include `raw_principal_enabled()`, `bind_raw_principal_state_or_throw(full_state,stage,label)`, coefficient update and packed `apply`. The K/B/Thomas blocks and `apply_raw_principal` are private: there is no public pressure-column factorization interface. A bounded prototype can pack zero horizontal components with z RHS, call existing `apply`, and extract z, first verifying identity components and the top row. It performs FP32 Thomas elimination on each call, so this is reuse of the owner, not an existing cached factorization or performance claim. A future owner-local interface may expose a block solve without copying EOS or top-row formulas.

## Native-stage A1 hook and checks

At `newton_solver.cpp:5794`, `U_eval=U_stage+dt*gamma*K`; iteration-zero K may be a nonzero predictor. `:7021` owns the actual FWAD `apply_jacobian`, `:7880–7910` the coordinate/preconditioner wrappers, `:8429–8449` the actual b/x0, and `:8754–8835` the existing solve and optional PreparedJvp scope. These are the bounded locations for a single `(host step, stage=2, Newton iteration=0)` hook/driver: freeze U_eval, F, K, a, stage/reference/grid context, S, D/mask, b and x0, and compare independent pristine baseline and candidate P on the identical A. Preserve production K and mutable preconditioner/fallback state; measure setup, inner/outer applies, JVP, solve, full-stage cost and memory.

The apparent `FROZEN_MI_AB` around `:8710–8745` is stale commentary and an unused target-iteration declaration; `frozen_ab_fired_this_solve_` is only declared/reset in this source. It is not an existing executable A/B driver. A new bounded observation or exact stage serialization is needed; do not substitute the t0 history file for a stage Jacobian.

Gate A1 should check (1) Eφ/Eθ/Eμ against FWAD and central differences on the actual positive state; (2) raw K/B/G directions including bottom and dynamic top, with omitted terms recorded; (3) C_mu/C_theta/C_phi/Omega and Hh against native faces, maps and boundaries; (4) small coupled block versus pressure elimination with native coefficients/top; (5) original-A true residual and per-block norms for equal-budget FGMRES A/B, plus all setup, JVP and solve work. Reject or revise on mismatched source, stage, units, scaling, residual, fallback or boundary behavior; iteration counts alone cannot establish an improvement. Gate B/C require the separate same-setup RK3 forecast/runtime comparisons. No native model run or RK3 comparison was performed for this audit.
