# R9 checklist — coordinate-system defects in PR #161 measurements

Three coordinate systems coexist and were conflated:
  outer physical   R
  Krylov (S)       r~ = S^-1 R        (gmres_rhs = -(S_inv_diag_ * R), :7397)
  inner objective  D^-1 r~  or  E^-1 r~

S is NOT identity: block-constant  S_q = max(||R_0,q||/sqrt(n_q), floor_q)   (:5443)

## Verified against source (all three P0 claims CONFIRMED)
- [x] V1  bnorm_safe = ||D^-1 b~||   (:2465 scales b_inner, :2481 takes its norm)
- [x] V2  rho_unscaled = ||r~|| / ||D^-1 b~||  -> MIXED denominator (:3644)
- [x] V3  E-objective sets D_inv = E^-1, missing the S factor (:2388)
- [x] V4  "physical share" uses r_true_inner = S^-1 R, not R (:2447)
- [x] V5  S is block-constant and != I, so V3/V4 have real numeric consequence

## P0-A  metric correction
- [x] A1  single relative_residual(r, b, L) helper; no site assembles num/den separately
- [x] A2  preserve unweighted b_krylov before D/E scaling
- [x] A3  emit rho_D, rho_S, rho_phys, rho_E from the SAME (r,b) at every restart
- [x] A4  E-objective left weight  E^-1 S  (not E^-1)
- [x] A5  objective shares emitted per coordinate, each named
- [x] A6  WRMS_METRIC requested but prerequisites missing -> fail-close, not silent baseline

## P0-B  contracts
- [x] B1  Krylov_Metric_Coordinate_Contract   (S=diag(100,0.01), E=diag(10,0.1): E^-1 vs E^-1 S flip block dominance)
- [x] B2  Relative_Residual_Denominator_Contract (b != r fixture; mixed-weight mutant must fail)
- [x] B3  Objective_Share_Coordinate_Contract  (four shares deliberately differ)

## P0-C  re-measure + retract
- [x] C1  re-run paired trajectory at dt=600, publish rho_D/rho_S/rho_phys/rho_E
- [ ] C2  measure kappa(E^-1 S)
- [x] C3  retract or update: "70-291x", "Spearman 0.04 => uncorrelated with stage gate", "kappa(E)=3.98e9"
- [x] C4  restate the block-share table in named coordinates

## P0-D  stage-3 entry (user's request: Y3 진입 residual 분해)
- [x] D1  entry ledger: raw L2 + stage-WRMS + block-max + per-block, at stage 2 and stage 3
- [x] D2  predictor decomposition at FIXED Y3_base: K0 = 0 / Picard / K2 / production
- [x] D3  attribute the 27x to Y3_base vs predictor; retract "inherited from Y3" if unsupported

## P0-B'  fixed-(A,b) objective comparison  (review §11 P0-B)
- [ ] E1  freeze ONE stage-3 linear system (A, b, x0, M^-1) and compare
          D-objective / S-coord L2 / physical raw / stage-WRMS on the SAME system
          (run-level A/B is confounded -- see sdirk3-scalar-coefficients-non-separable D1)

## P0-D'  stage-3 nonlinear ledger  (review §11 P0-D)
- [ ] F1  in the same trial record  rho_D, rho_E, ||R + A dK||_E, ||R(K + alpha dK)||_E
          (separates "the linear model was solved" from "the nonlinear residual fell")

## Carried P0 from earlier reviews, still open  (review §10)
- [x] G1  stage-3 policy inherits stage-2 settings: budget resolver applies stage-2 at
          stage>=2 and only overrides if the stage-3 knob is set; aggressive early-stop
          also keys on stage_id>=2 with the stage-2 knob.
          -> extract a PURE StageKrylovPolicy resolver so a stage-3 experiment varies ONE thing
- [x] G2  NOT REPRODUCIBLE on current head: `last_mu_schur_reduction_used_ = !hevi_mu_identity`
          and solve_path=2 already record the identity bypass (closed by 7192666/f38abb4).
          `reduction_applied` no longer exists as a field; the two comments still naming it
          have been corrected.

## Production numerics, after the metrics are trustworthy  (review §11 P0-E)
- [ ] H1  mu-phi asymmetry
- [ ] H2  acoustic-gravity block re-derivation
- [ ] H3  stage-2 Arnoldi budget reduction (510 is a diagnostic budget, not a design point)
- [ ] H4  stage-3 strict convergence
- [ ] H5  dt=600 full step
