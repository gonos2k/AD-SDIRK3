# R9 remainder — the 6 open items, restated as measurements

Context that reframes all of them: stage 3's entry state is **99.98% one EXPLICIT ARK term**
(`h a^E_32 k_slow[2] = 1.251e10` of `||Y_3|| = 1.251e10`), and both implicit terms together are
2.2e-5 of it. So every item below that acts on the IMPLICIT solve has a measured ceiling, and
the honest job is to MEASURE that ceiling rather than assert it.

## F1  nonlinear ledger  (review §11 P0-D)
- [x] F1a  in ONE trial record  rho_D, rho_wrms, ||R + A dK||_E (what the linear model
           PREDICTS) and ||R(K + alpha dK)||_E (what the nonlinear residual DOES)
- [x] F1b  measure: is the stage failing because the linear system is unsolved, or because
           solving it does not reduce the nonlinear residual?

## I1  the discriminating measurement the explicit finding demands  (new)
- [x] I1a  dt ladder for the explicit cascade: ||k_slow[1]|| -> ||k_slow[2]|| growth at
           dt in {600, 300, 120, 60}. Already instrumented (SDIRK3_STAGE_BASE) -- no new code.
- [x] I1b  verdict: does the 1.6e5x growth COLLAPSE with dt (a dt/CFL effect -> sub-cycling is
           the fix, supporting the split-explicit rebuild) or PERSIST (structural)?

## H1  mu-phi asymmetry
- [x] H1a  measure the mu row of M against the operator it inverts, judged by ||A M^-1 v - v||
           and NOT by a gain ratio (that error is on the record)
- [x] H1b  the recorded claim is D_mu has the WRONG SIGN (A_mu_mu = 0.95 => the diagonal
           belongs below 1; code has 1 + 4.7e-3). Re-measure under WRFParity + correct Omega.

## H2  acoustic-gravity block re-derivation
- [x] H2a  decide by measurement whether it is REACHABLE: precond-inc2 (the W-phi 2x2
           refinement) was implemented, measured WORSE, and reverted. Do not re-derive before
           showing the block is load-bearing at the stage that fails.

## H3  stage-2 Arnoldi budget
- [x] H3a  510 is a diagnostic budget, not a design point. Sweep it as a SINGLE-VARIABLE
           experiment (WRF_SDIRK3_STAGE_KNOB_FIRST=1, report Arnoldi USED not requested).
- [x] H3b  report the minimum budget at which stage 2 still converges.

## H4  stage-3 strict convergence
- [x] H4a  measure the ceiling: with stage 2 converged and a large stage-3 budget, what does
           stage 3 reach? Do not assert unreachability -- show it.

## H5  dt=600 full step
- [x] H5a  run it and report exactly where it stops, with the stage and the term named.
