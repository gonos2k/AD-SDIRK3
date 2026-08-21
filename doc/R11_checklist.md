# R11 — differentiability and full-step stability closure

## 0. CLAIM CORRECTIONS (before any code)

| published | what the evidence supports |
|---|---|
| "explicit response is non-differentiable" | strong nonlinear / precision-sensitive growth along Y_1 -> Y_2 |
| "no positive stable timestep exists" | candidate RHP Ritz modes in an UNCONVERGED projected explicit Jacobian |
| "sub-cycling cannot fix it" | not yet proven either way |
| "R10 22/22 closed" | causal path closed; differentiability, full-step stability, admissibility OPEN |

- [x] C0  correct these four in doc/R9_findings.md, the R10 checklist, and the PR text

## P0-1  directional differentiability, measured properly
- [x] D1  Taylor remainder with the TRUE JVP, over eps = 2^-2 .. 2^-16, FP64 accumulation:
          r1(eps) = ||F(U+eps d) - F(U) - eps J d|| / max(||dF||, eps||J d||)
          r2(eps) = ||F(U+eps d) - F(U-eps d) - 2 eps J d|| / (2 eps ||J d||)
- [x] D2  VERDICT: INCONCLUSIVE at FP32 state precision. r1 -> 1 at small eps is an ARTEFACT
          (realized_frac -> 0 forces r1 -> 1 identically). Clean window is only eps in
          [0.0156, 0.25]: stage-1/random falls 3.0x (consistent with O(eps)), stage-2/implicit
          RISES 3.5x (not O(eps)). Settling it needs an FP64 STATE path, not FP64 accumulation.
- [ ] D3  separate directions: the Y_2-Y_1 direction, random, interior-only, boundary-only

## P0-2  the RHP spectrum needs convergence evidence
- [x] S1  Ritz residual |h_{m+1,m} e_m^T y| per pair
- [x] S2  m = 24, 48, 96 convergence; >=3 deterministic seeds
- [x] S3  reorthogonalization (double MGS/DGKS) + ||V^T V - I||
- [x] S4  active-domain projection P J_E P, not raw J_E (halo / boundary / staggered endpoints)
- [~] S5  PARTIAL: J_full measured with the same Arnoldi and has LARGER RHP eigenvalues than
          J_E (470.8 vs 169.8 at stage 2, converged) -- so the SPLIT does not create them.
          D(Phi_h), the one-step tangent through the implicit solves, remains unmeasured.
- [ ] S6  np = 1,2,4 comparison

## P0-3  hidden dt authorities beyond dt_stage_
- [ ] T1  enumerate: grid_info_->dt, local dt args, acoustic substep, cached coefficients,
          stage coefficient builder, diffusion/damping timestep, physics tendency dt
- [ ] T2  mutation-test each one; any that changes the RHS becomes an explicit dependency
- [ ] T3  RAII restore for the probe (dt, refs, caches, counters) incl. on exception

## P0-4  physical-state admissibility via the PRODUCTION reconstruction
- [ ] A1  mu_full = mub + mu, theta_full = t0 + t, phi_full = PHB + PH, dz from phi_full
- [ ] A2  p, rho through the production EOS -- not a separate approximation
- [ ] A3  layer thickness, metric Jacobian, hydrostatic residual, BC residual
- [ ] A4  per-extremum (i,j,k,var,rank); reopen the R10 items marked closed

## P0-5  full slow-RHS decomposition
- [ ] F1  every term x every variable, through a production-side term observer
- [ ] F2  cosine with the total, so two large cancelling terms are visible

## P1  fail-closed in the PRODUCTION path
- [ ] V1  block_energy_shares() must itself reject overlap/gap/tail/zero/negative/NaN weights
          and device/dtype mismatch -- the validators exist but no caller invokes them
- [ ] V2  experiment pair validator must compare RUNTIME behaviour (hopeless cap, early-stop
          streak, refresh period, precond fallback latch, warm start, trust state), not just
          the pure policy fields
