# A4 instrumented first-point decision: NO-GO

PR330 brought PR329 into main (`ed188588`). Frozen numerical source is `b0be4a94`.
One predeclared A0–B0–B1–A1 block completed: four independent h600 single steps, no retries.
All numerical, initial-state, snapshot, clock and resource gates passed. Setup-inclusive paired
ratios were1.2825 and1.2213; both failed the fixed2x acceptance threshold. Prior failed blocks
are unchanged. This is a completed negative cost decision, not a forecast speedup or P0 adoption.

`costs.csv` keeps full scaled A separate from wrapped scaled M and linear remainder. Physical M
is a nested subset of wrapped M; existing first-JVP time is nested within A. Neither is additive.
Snapshots are one original first Arnoldi direction, no extra RHS/JVP; clone time stays included.
Reductions/output occur after the measured interval. Whole-step RSL timing is not an operational
forecast benchmark because of the observer work after the first point.

The physical v and A(P^-1 v) share the same v bytes. `defect_blocks.csv`, the signed figure and vertical
profiles use correct owned crops, physical rate-coordinate units and arbitrary normalization.
Total W defect improves, but the non-top63 W faces worsen2.57x. V shape error remains0.745.
The source V-row audit identifies omitted terms but does not attribute this defect or later noise.
`forecast_context.png` separately compares archived native RK3 and A0/B0 at10min.

Large native states and canonical pair binaries remain at the pinned local paths. The identical
A1/B1 snapshots were deduplicated without altering registered paths or bytes. CSV public copies
are LF-normalized and retain the original hashes. Producer scripts preserve local input paths;
they are one-off evidence tools, not a new portable framework.

The recorded `wrapped_M` is the applied inverse preconditioner P_s^-1. The scaled defect is
e_s=v_s−A_s(P_s^-1 v_s); the saved physical vectors give e_phys=S e_s.
