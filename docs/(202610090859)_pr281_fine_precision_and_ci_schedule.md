# PR281 fine inverse precision and CI schedule

Local timestamp: 2026-10-09T08:59:51+09:00.

All fine 16x12x8 physical evaluations use Newton 1e-13/Krylov 1e-10, including accepted/rejected BFGS trials, finite differences and the fixed-control h10 comparison. Effective Float32 metadata is checked on each call. Coarse defaults remain N12/K8. The raw gradient 1e-5 gate, 12-iteration budget, Armijo and separately recorded stationary trial are unchanged. No old N12 fine payloads or history are reused.

CI retains the exact 129-name inventory, 2700-second inverse timeout and 180-minute core job limit. CTest uses two workers for light tests; fixed-data refinement, quadratic forcing and physical inverse run serially without overlap. This is scheduling only, not a missing gate or claimed speedup. Full exact-head CI is pending.

Production source is unchanged from be4077b and its recorded 117-case/direct/full-tile/WRF validation. The isolated WRF run used matching cached Fortran objects and produced three byte-identical PR279 outputs. No extra WRF run is necessary for this runner/scheduling-only change. No merge.
