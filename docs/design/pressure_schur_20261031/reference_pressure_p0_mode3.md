# A1b mode-3 live-EOS reference pressure block

Status update: the reference scalar/mixed/packed components are implemented and their focused tests passed; see the timestamped A1 progress report. Native P0/WRF equivalence and Gate A2 cost are not established. The derivation-stage wording below is retained with that scope.


This selects an approximate, frozen preconditioner for the existing mode-3 linear problem. It does not change the ARK time map, nonlinear RHS, physical unknowns, solver tolerances, or any global PDE; mode 4 is outside this contract. The original mode-3 operator and independently recomputed physical true residual remain authoritative. No implementation, build, model run, or new field comparison was performed for this refinement.

## Chosen reference and coefficient schema

At one named actual stage/Newton evaluation, use the *live captured RHS EOS context after boundary projection*: phi, t′, mu′, base mass, base pressure, inverse-density ALB used by the RHS, c1h/c2h and rdnw. Diagnose positive alpha, p and `Theta=T0+t′` with the same `calc_p_rho_wrf`; T0=300 K and `κ=cp/cv` use the actual RHS constants. The completed A1 observation confirms that the captured primal matched its reconstruction for that point; the compact A1 receipt contains digests, not the full stage arrays needed to instantiate this reference. A future bounded stage driver must obtain the actual arrays in the same call, pin source/executable/stage/context and verify projection, geometry and constants before constructing P0. Do not infer the stage from wrfout.

For the initial flat, unit-map, periodic-X/symmetric-Y reference, select the independent physical mass cells Ωh, excluding packed aliases and ghost rows. Define `mean_h(f)_k = sum_(j,i in Ωh) f_(j,k,i)/|Ωh|`, in FP64 with a fixed reduction order. The unit-map assumption is part of this first reference model, not a claim about native maps. For every vertical k set

    p_ref,k     = mean_h(p)_k
    alpha_ref,k = mean_h(alpha)_k
    Theta_ref,k = mean_h(Theta)_k
    Q_ref,k     = mean_h[κ p |rdnw|/(alpha H)]_k
    T_ref,k     = mean_h[κ p/Theta]_k
    M_ref,k     = mean_h[κ p c1h/H]_k,
    H=c1h*(mub+mu′)+c2h.

Each average uses the live EOS coefficients, not the raw preconditioner’s imported ALB. Averaging the pointwise derivatives is intentional; in general `Q_ref`, `T_ref` or `M_ref` is not the derivative obtained by first averaging primitive fields. `alpha_ref` is the mean specific volume, not the reciprocal of a mean density. Use actual independent counts, grid metric values and periodic/wall conventions from the same stage; an empty/ambiguous physical core, nonfinite inputs or nonpositive p, alpha, Theta, H, Q must fail closed. Do not silently reinterpret nonunit maps as unit maps. A later variable-map/area-weighted model is a separate derivation and must not be described as this FFT/DCT reference.

Set `Ephi_ref=−diag(Q_ref) T`, `Etheta_ref=diag(T_ref)`, and `(Emu_ref mu)_k=M_ref,k*mu`, where T differences active phi against fixed bottom phi0. Define the pressure-rate coordinate by these **same three live-derived reference maps**:

    p_rate = Ephi_ref phi_rate + Etheta_ref t_rate + Emu_ref mu_rate.

This horizontally homogenized E is the average of live state-dependent maps, not pointwise exact E at each native cell. Its precise source, physical cells, reduction order, dtype, hashes and stage identity belong in any future A/B receipt.

For the vertical pressure-to-W map, on each active W level l form the actual native principal factor `g*rdn_l/(c1f_l*M+c2f_l)` from the same frozen column mass and authoritative W-level coefficients, then average that **factor** over Ωh; at the physical top use `2*g*|rdnw_(nz−1)|/(c1f_nz*M+c2f_nz)` before averaging. Call the resulting positive profile `A_ref`. Its inputs were not all serialized in the compact A1 receipt: a future driver must bind/verify the same stage’s c1f/c2f, rdn, g and M, rather than assume a grid clone equals the live owner. Define `Gz_ref=A_ref Tᵀ`, including the native one-sided dynamic top and fixed bottom treatment.

The sole vertical E owner in this reference is now `E_live_ref`. Rebuild the reference vertical maps in double as `K_ref=Gz_ref Ephi_ref` and `B_ref=Gz_ref Etheta_ref` in real arithmetic; their actual floating-point implementation must be checked against a coupled block. Reuse the existing raw owner’s *algorithm, variable ordering, and top/bottom rules*, not its precomputed FP32 K/B as if they shared this live ALB. The existing raw FP32 column and its separate dt×gamma remain an independent approximation/oracle to compare and time. In particular A1 measured nonzero live-versus-imported ALB, so exact numerical equality to raw K/B must not be assumed. P0 also introduces pressure feedback from Emu to W that raw P omits; never add raw K/B on top of `Gz_ref p_rate` and double count.

## Mode-3 uniform-compression coefficient

Choose the horizontal pressure-to-velocity map and compression coefficient

    Hh_ref = −alpha_ref G_h
    Bh_ref,k = −κ p_ref,k * T0/Theta_ref,k.

`Bh_ref` has pressure units. It is a **derived mode-3 reference coefficient**, not a fitted damping or tuning factor. In the previous ideal flat counterexample, take c1h=1, c2h=0, constant M and t′, zero W/Ω and a zero-mean horizontal divergence mode D that is vertically uniform. For the native **ImplicitOnly** partition,

    mu_dot=−M D,      t_dot=−t′ mu_dot/(M+eps).

Live EOS E gives, exactly including the source denominator epsilon,

    p_dot = −κ p [ T0/Theta + (t′/Theta) eps/(M+eps) ] D.

In the source’s positive-mass limit with eps neglected, this is `−κ p (T0/Theta)D`, exactly the selected reference coefficient when the reference equals that uniform state. Unlike the earlier `−κ p D` choice, it does **not** require a fabricated nonzero Cphi in this particular counterexample. The residual epsilon term is recorded, not hidden by retuning Bh. The Full RHS also contains conservative theta advection and has a different partition-level expression; this derivation is for mode-3 ImplicitOnly.

The result does not establish exact native compression for nonuniform vertical D, hybrid c1/c2 profiles, diagnosed Ω, horizontal gradients, terrain, variable maps, or nonuniform backgrounds. In general the native Phi/Ω contribution need not vanish, and `p_ref*T0/Theta_ref` is not the horizontal mean of the pointwise product `p*T0/Theta`. Both are explicit reference approximations. Compare actual native FWAD responses with P0 and report the discrepancy; do not describe this coefficient as the full native Jacobian or claim it resolves late W noise.

## Block, reconstruction and sign

On physical Newton rate corrections use `v−a Hh_ref p=bv`, `w−a Gz_ref p=bw`, and

    p − a[Bh_ref D_h v + g Ephi_ref w] = Ephi_ref bphi + Etheta_ref bt + Emu_ref bmu.

Retain selected native frozen-mass continuity and perturbation-theta maps in recovery:

    mu=bmu+a Cmu v
    t =bt +a Ct v
    phi=Ephi_ref^(-1)(p−Etheta_ref t−Emu_ref mu)
    Cphi_ref=Ephi_ref^(-1)(Bh_ref D_h−Etheta_ref Ct−Emu_ref Cmu).

`Cmu` and `Ct` must be defined from the actual selected fixed stage coefficients and source boundary/epsilon conventions, with omitted current-background-velocity or coefficient derivatives listed. They may vary horizontally; their effect is compensated algebraically in `Cphi_ref`, so the following reference pressure operator stays horizontally separable. This compensation is part of the approximate Phi row, not proof that native Phi/Omega equals it. It also preserves exactly one Emu contribution in the transform and pressure-to-W recovery.

With `Q=diag(Q_ref)`, `Ephi_ref=−QT`, `Gz_ref=A_ref Tᵀ`, all Q and A entries strictly positive. For compatible unit-map C-grid `D_h=−G_hᵀ`, Fourier/cosine mode λ≥0 gives

    Sp(λ) = I + a² g Q T A_ref Tᵀ
              + a² κ p_ref alpha_ref (T0/Theta_ref) λ
    Q^(-1) Sp(λ) = Q^(-1) + a² g T A_ref Tᵀ
              + a² Q^(-1) diag[κ p_ref alpha_ref (T0/Theta_ref) λ].

Every summand on the second line is symmetric positive semidefinite and the first is positive definite; hence this **weighted reference pressure block** is SPD. The top W level is active with its one-sided dynamic factor, and Phi0 is fixed. Packed Phi0 identity or nonzero boundary residuals must be handled separately and carried into pressure RHS. This result does not extend automatically to native maps, projection, terrain, nonnormal couplings or Full A.

Use double `a=h*γ` from the actual stage consistently throughout this new block; do not silently substitute raw FP32 `a`. In production scaled Krylov coordinates an inverse application must be `S^(-1) P0^(-1) S`; distinguish D weighting from both coordinate scaling and independently recomputed physical `b−Ax`.

## Approval and Gate A2 prerequisites

This is a proposal for Red review, not proof or a committed solver. Before implementation, review the reference mean and boundary semantics, actual-stage Cmu/Ct definitions, Ephi inverse and explicit Phi0 identity, added Emu→W coupling, and the mode-3-only epsilon qualification. A2 still needs a pinned, replayable native-stage linear problem or a genuinely noninterfering same-call driver: A1’s compact hashes and saved residual are insufficient. Validate (1) small full declared P0 versus pressure elimination with nonzero t′/mu and dynamic top; (2) signs, weighted symmetry/positive quadratic form and per-block physical/scaled reconstruction; (3) native FWAD Cmu/Ct/Hh/Cphi and EOS action, reporting A−P0 rather than claiming equality; (4) independently recomputed original-A true residual, FGMRES budget/fallback and all setup, inner/outer/JVP/full-stage time and memory. No acceptance is justified by fewer iterations alone. Forecast claims still require the separate same-setup RK3 Gates B/C.
