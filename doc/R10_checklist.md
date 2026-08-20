# R10 — stage-2 slow-RHS causal isolation and dt/split contracts

## 0. RETRACTION (accepted before anything else)

PUBLISHED, AND NOT ESTABLISHED:
  "No preconditioner, Krylov budget or objective alignment can fix stage 3."

Y_2 is the OUTPUT of the stage-2 implicit solve, so a solve error dY_2 propagates
  dF_E(Y_2) = J_E(Y_2) dY_2 + O(||dY_2||^2)
and F_E(Y_2) is the term that dominates Y_3. The DIRECT implicit contribution to Y_3 is
measured small; the INDIRECT path through Y_2 was never tested.

WHAT IS ACTUALLY MEASURED:
  the direct blow-up term in Y_3 is h a^E_32 F_E(Y_2); whether the stage-2 solver/acceptance
  contributed to F_E(Y_2) being that large is UNTESTED.

- [x] R0  retract the overreach in the PR text, doc/R9_findings.md and memory

## P0 — causal isolation (A and B first, per the review)

- [x] A1  stage-2 solve accuracy intervention: same stage-2 input, tighten the TRUE residual
          target (eta, 0.1eta, 0.01eta, + a reference), re-evaluate F_E on each Y_2 with the
          SAME function; report ||F_E(Y_2^m) - F_E(ref)||_W / ||F_E(ref)||_W against
          ||Y_2^m - Y_2^ref||_W / ||Y_2^ref||_W
- [x] A2  verdict: solver in the causal path, or excluded

- [x] B1  fixed-state dt-invariance: freeze state/metric/halo/boundary, vary ONLY the dt handed
          to the RHS, compare F_E(Y; h1) vs F_E(Y; h2) vs F_E(Y; h3)
- [x] B2  verdict: hidden dt / tendency-vs-increment mixing, or clean

- [x] C1  fixed RHS, vary ONLY the outer ARK multiplier h -- assembly linearity
- [x] D1  state continuation Y(lambda) = Y_1 + lambda (Y_2 - Y_1): find the lambda where
          F_E jumps (branch switch / positivity violation / local singularity)

- [x] P0-3a  primal split contract    F_full = F_E + F_I
- [ ] P0-3b  JVP split contract       J_full v = J_E v + J_I v
- [ ] P0-3c  VJP split contract       J_full^T w = J_E^T w + J_I^T w
- [ ] P0-3d  transpose consistency    <J v, w> == <v, J^T w>

- [x] P0-4a  operator-level ledger of k_slow[2] (adv / pressure-metric / buoyancy / coriolis /
             mix-diff / source / boundary), observed through the PRODUCTION assembly, never a
             second implementation
- [x] P0-4b  per-term: fp64 raw, block-WRMS, max_abs + location, cosine with the total,
             interior vs halo, nonfinite count
- [x] P0-4c  state admissibility at Y_1, Y_2, Y_3^base (min density/pressure/theta/thickness,
             negative-mass count, nonfinite, halo mismatch, hydrostatic residual)
- [x] P0-4d  restate "99.98% dominance" as RAW PACKED COORDINATE dominance, not physical

- [x] P0-5a  CFL_{x,y,z} and rho(h J_E(Y_2)) via JVP Arnoldi / power iteration
- [~] P0-5b  UNANSWERED, and the reason is recorded: the FD power-iteration estimate is
             eps-DEPENDENT (rho ~ eps^+1), so it is not a Jacobian spectral radius and no
             stability-region verdict follows from it. Needs a true JVP on the explicit RHS.

## P1 — contracts and reporting

- [x] P1-1  StageKnobFirst does NOT guarantee a single-variable experiment: budget_active is an
            OR, so the first stage-3 knob also flips it. Soften the claim AND add a validator
            that resolves both arms and fail-closes unless diff == expected_changes.
- [x] P1-2  block partition validation: no overlap, no gap, exact cover, positive finite
            weights, matching device/dtype; negative tests for each
- [x] P1-3  cast to FP64 BEFORE the reduction (8 sites do norm() in FP32 then cast the scalar)
- [x] P1-4  the nonlinear model claim: report the VECTOR defect
            ||r_actual - r_pred|| / ||r_pred||, not just the norm ratio; correct
            "linearization faithful to 0.5%"
- [x] P1-5  two dt points are an interpolation, not a scaling law -- separate the four dt
            dependencies (A/B/C/D above) before any exponent or crossover claim
