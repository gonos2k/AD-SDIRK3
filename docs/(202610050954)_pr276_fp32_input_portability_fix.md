# PR276: preserve the archived physical input across FP32 power backends

Local timestamp: 2026-10-05T09:54:01.149592+09:00.
Initial exact HEAD:34ac3bade555265e038e3a66bf3bb299c69bfec4.

The only failing CTest in run37222200925 was the N4 energy snapshot.126 tests
registered:124 passed,1 failed,1 MPS skip. The C++ Linux wave frequency matched
the archive, while the NumPy generated-PHB frequency differed by1.99685e-9
relative and violated the existing1e-9 gate. This was not an energy-closure or
production-equation failure.

Scalar `math.pow` with the same rounded FP32 ratio/exponent reproduces Linux's
frequency .00210996032649729 locally to7.5e-18 absolute. Its first basePHB face
is18565.947265625 versus the archived native18565.9453125: one FP32 spacing
(0.001953125m²/s²). The alpha0 intermediate differs by two FP32 bit steps.
Native TileCase::basePhi() was independently probed from unchanged C++ fixture
and core library; its five bit patterns are00000000,46910be4,471f4e8a,4786d572,
47d64fde. These physical input bits, not fitted eigenvalues, now define the
strict N4 snapshot. General N8 and spatial-reference construction stays active.

Fixed input gives bit-identical A/H for NumPy/scalar power paths. An explicit
one-ULP PHB-face mutation still violates the original frequency gate. The
same-state analytic theta_z comparison reuses these faces, so its2.9162% to
0.4401% sensitivity changes only the diagnostic gradient. Red also reran the
full N4 case with both power selectors mapped to the Linux-equivalent scalar
path; it passed. Backend inequality is reported, not required.

Affected energy/spatial CTests passed2/2 in1.60s. No native production source,
eigensolver, Newton criterion or numerical assertion tolerance changed.
Green/Red reviews passed. The mixed-mode runtime limit alone changes600→900s
because the Linux pass took579.19s; the other wave limits stay600s. The workflow
runs the two source-only entrypoints after dependency installation and before
native builds; the full126-test gate and required jobs remain. Both early JSON
results and the later spatial JSON are retained as CI artifacts.

No new WRF/em_b_wave or RK3 comparison was run. Unchanged-production PR275
same-setup field/runtime receipts are reused. Corrected exact-head remote CI is
pending; readiness must wait for its CTest/ABI/install/required-job completion.
Historical evidence remains unchanged; updated exact-source inputs and checks
are in [fix receipt](evidence/wave_energy_spatial/ci_resolution/fix_receipt.json).
