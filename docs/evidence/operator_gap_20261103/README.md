# Terminal directional operator-gap evidence

Base b98a0e81 (PR341), first diagnostic source85df4680, corrected source acc39391.
This is a default-off compile-time diagnostic, not a production solver option.
The corrected run stops after the first stage2/Newton0 linear solve, before trust/recovery
and any accepted Newton update. It completes zero implicit stages and zero timesteps.
`used_p` is the postprocessed search candidate, not an accepted/damped update.

The first attempt remains INCONCLUSIVE: legacy A9's fifth-solve report invalidated ordinal1
before gap measurement. The correction bypasses that unrelated report only in this diagnostic,
and adds its own ordinal/reference checks. A9's validator and numerical solve are unchanged.
The corrected source/build/link/executable and separately registered one-attempt run are pinned.
The complete actual link map is in wrf_link.map.gz; its uncompressed SHA is in link_receipt.json.

The corrected run used mode3, single tile/rank, FP64, selected forward AD, reference-update off,
reuse off, H=I and raw principal preconditioning. Pressure preflight/observers are bypassed;
this is not a P0/P1 cost comparison or new normal-forecast regression. Three selected AD calls
(no routed FD/fallback), 18 perturbed complete-RHS evaluations and four exact center repeats
make 25 implicit RHS calls. Twenty-one initialized fields match archived A9/RK3 references.
No new RK3, endpoint comparison or runtime-performance test was done.

The recorded three epsilons are dimensionless directional perturbations, not timesteps:
integration dt remains600s. Analysis reports measured FD gaps only. Unknown finite-difference
error prevents a rigorous A_star gap or exact forcing bound. Q samples and stable observed
input hashes are not proofs of full constraint-space or complete cache/context invariance.
`gap_scaled_relative` uses the FD norm; `gap_over_base_G` uses the recorded base-G norm.
Analysis norms divide by recorded S; these are not a bitwise reproduction of Krylov's
reciprocal-multiplication reductions. The requested tolerance on the plot is a reference,
not a post-hoc acceptance gate for any of the three directions.

The helper's cubic/alias/error tests were run at85df. Their three source/header hashes are
unchanged atacc; helper_evidence_reuse.json records reuse, not another test execution.
The failed read-only preflight schema check is retained; it did not launch WRF.
Original first-attempt results and experimental criteria were not rewritten.

Scripts retain exact names and original W/E paths. Restore each attempt at its recorded
source revision and directory before using source-hash assertions. Raw NetCDF/NPY files,
executables, baseline archives and complete compiler/MPI/SDK/LibTorch/Fortran fixtures remain
external at paths in receipts. This public folder reproduces analysis of recorded scalars,
but is not a portable standalone native WRF replay bundle. First-attempt observer hashes
include metadata; terminal hashes are data bytes only. point_reuse_audit.json recomputes
old NPY data hashes to compare U_eval/reference, and does not compare all hidden context.
