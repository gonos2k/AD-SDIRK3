# Forcing source conditions and actual accepted Stage3 update

Baseline main3a82d601; diagnostic sourcebc0f6646. A compile-time default-off terminal
receipt reads the existing accepted Stage3/Newton0 K/R pair, checks its pairing, and
exits without proceeding. It adds zero RHS/JVP/preconditioner calls. The actual native
metric routine provides the before/after RMS; analysis L2 reductions are separately labelled.
`outer_update_alpha` is not the internal trust scale. The recorded outcome is trust-accepted,
Stage2 convergence is logged, Stage3 is incomplete, and no full timestep is completed.

Source audits close the specific cross-call frozen-omega suspicion: the memo is local
and rebuilt each RHS. The configuration authority audit resolves early NH-off output
as pre-WRF-override, before tile creation. Saved-point domain margins reuse existing
arrays/NetCDF data and verify recorded fingerprints; no smaller-epsilon sweep was run.
Those files are not a complete graph, tangent or floating-point error proof.

The exact-rational mu-tube analysis is conditional on fixed B/MUB/a, the source copy map
Q_mu (infinity norm one), finite FP64 inputs and a one-ULP binary64 basic-operation
rounding contract. Its predefined 1 Pa allowance follows from the magnitude envelope
and 5/4096 Pa bound; it is not a solver tolerance. Positivity covers only the mu_full<=0
branch. Replacement value1000 is not its trigger. It does not cover other clamps, libm,
residual evaluation error rho, FD error tau, exact forcing, inverse/Lipschitz bounds or
forecast stability. Numerical merit decrease is a native computed-value observation.

The run keeps dt600s, 100km,40x80x64,MPI1,Torch1/1 and original convergence controls.
Twenty-one initial fields match archived A9/RK3, but no new RK3, endpoint or cost comparison
was performed. The scope is one accepted update, not Stage3 root convergence or a forecast.

Source/objects/archive/actual linker/map/method/executable/attempt hashes are recorded.
The complete link map is deterministic gzip; its uncompressed SHA is in link_receipt.json.
Original inputs, NetCDF/NPY, binaries, parent archives and toolchain fixtures are external
at paths in receipts. Restore exact filenames, source revision and original W/E paths
before using the scripts; this public folder alone is not a portable native replay bundle.
The registration-draft review-hash refresh occurred before registration/model launch;
original draft and final registration remain distinct. No experimental result was edited.
