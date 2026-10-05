# PR276 CI resolution checklist

Local timestamp: 2026-10-05T09:43:53.975883+09:00.
Target: PR276, initial HEAD34ac3bade555265e038e3a66bf3bb299c69bfec4.
Failed exact-head GitHub run:37222200925.

- [x] Inspect all conversation/review threads: none open at this timestamp.
- [x] Identify sole failure: Wave_Physical_Energy_Budget N4 snapshot frequency,
  generated NumPy omega .00210996032649729 versus native archived .0021099603222840182;
  relative1.99685e-9 exceeds unchanged1e-9 gate.124 passed,1failed,1MPSskip of126.
- [x] Reproduce the failing matrix via scalar-pow float32 construction. Native
  C++ Linux eigenfrequency matches the archived input; NumPy reconstructed PHB
  differs. Native private TileCase basePhi probe independently supplies original
  bits[00000000,46910be4,471f4e8a,4786d572,47d64fde].
- [x] Bind the strict N4 snapshot to those actual imported FP32 faces. Keep
  generic N8/refinement construction, production equations, eigensolver and
  original frequency/energy tolerances unchanged.
- [x] Verify alternate-pow fixed-input A/H equality and one-ULP-PHB mutation
  detection; keep generated geometry as a separately labeled sensitivity.
- [x] Give only the new mixed-mode test a finite900s runtime limit: previous
  Linux pass took579.19s against600s (20.81s margin).No numerical budget change.
- [ ] Run affected local contracts, refresh exact-source graph/receipts, obtain
  independent final Green/Red review, and push one fix to existing PR276.
- [ ] Verify the corrected exact-head remote CI including CTest, ABI/install and
  all four required jobs. Record pending status until complete; ready PR only
  after green checks. Do not merge.

No new WRF/RK3 run is required for this test-input fix: production C++/Fortran
source is unchanged; PR275 same-setup WRF/RK3 receipts remain explicitly reused.
The earlier source-discrete energy/spatial/native mixed-mode scientific scope
remains unchanged. A platform-reconstructed FP32 input is not the same physical
fixture as the native archived input even when algebraic formulas are the same.
