# Frozen residual objective and portable packet checker

`recompute.py` exposes the single runner/FD objective primitive
`objective_pair(z_mid_f64,z_terminal_f64,q_mid_f32,qN_f32,y,sigma)`. It returns
`h`, `residual_f64`, `cost_components`, `cost_total_f64`, `alpha`, and separate
FP64 `cotangent_mid_f64` / `cotangent_terminal_f64`; it never replaces or
combines the frozen q vectors. The caller casts the terminal cotangent only at
the native Fortran boundary. `inject_midpoint_f32` performs the one accepted
128-step sum as `Q32(FP64(lambda128)+FP64(alpha_mid*qmid))`.

Protocol b65b2b01 freezes `y=[0,0]`, `sigma=[1,1]`, and
`J=0.5*sum(((h-y)/sigma)^2)`, with `h=[qmidᵀZ128,qNᵀZ256]`. The signal bound
for each component is `0.5*((sum(abs(q*z_baseline))+abs(y))/sigma)^2`; the full
cost bound is the sum of the midpoint and terminal bounds.

`recompute_components` calculates the full-cost gradient from the actual new
Fortran FP32 g0, the midpoint increment as
`vᵀ(FP64(gnew0_f32)-alphaN*FP64(gold_terminal0_f32))`, and the terminal-residual
reference `alphaN*vᵀgold_terminal0_f32`. It checks FD/AD error, width plateau,
and the per-width signal floor separately for all three components. New
quadratic-cost one-sided gaps are diagnostics only because curvature separates
finite-width side slopes even when the central derivative is exact. The
existing unresolved one-sided rule remains for the underlying linear h checks.
Taylor diagnostics are not acceptance gates.

`check_packet.py` validates the fixed portable NPZ schema, objective constants,
baseline/replay states, exact terminal seed, the single midpoint injection,
all eight FP32 handoffs, and all eight local FP64-to-FP32 casts. It does not
parse raw receipts or checkpoint ledgers; it reports the manifest's external
receipt/source/executable pins and leaves raw collection verification to the
collector. Run both `python3 recompute.py --self-test` and
`python3 check_packet.py --self-test` before packet use.

The native driver's `local_terminal_c_dot` remains its original unscaled W
linear diagnostic. It is not the squared residual cost. Cost values and
state-dependent cotangents both come from `objective_pair`; the native wrapper
receives the separately bound `lambda_wrapper` input.

The midpoint diagnostic uses the original raw T perturbation. Replacing it by
a total-theta diagnostic while preserving the same residual problem would also
require shifting its target by the corresponding constant. The frozen target
0 here refers to the original dimensionless diagnostic, not a 0 K observation.

Exit code 0 means the packet was read and recomputed successfully, not that the
scientific criteria passed. Acceptance requires `overall_pass: true` and every
entry in `component_checks` to be true in the output JSON.

From this directory, recompute the compact packet with NumPy installed:

```sh
python3 check_packet.py --npz native_residual_objective.npz \
  --manifest manifest.json --output /tmp/native_residual_recomputed.json
```

The fixture keeps the registered 0.9375 s time step, Newton absolute tolerance
1e-9, and the original post-initialization control point. It adds no physical
observation operator, optimizer, new time step, or production tolerance change.
The full gradient is newly computed; the separated midpoint check is a derived
increment against a scaled retained terminal gradient, rather than an
independently computed midpoint-only native gradient.

`collect_packet.py` is the unchanged local raw-evidence collection recipe. Its
fixed worktree/evidence paths and required checkpoint ledgers refer to the
preserved validation environment; it is not a second portable packet checker.
The archived pre-native FD file contains retained-gradient predictions only.
Use the final `recomputed.json` for the new native chain's acceptance result.
`root_packet_audit.py` is also an archived local recipe and expects the original
`E/packet/` layout; use `check_packet.py` for this flat portable directory.

The final new native chain passes all frozen component checks. See
`directional_checks.csv` for the six FD/AD comparisons and
`objective_values.csv` for the six retained primal cases. The maximum full
FD/AD relative discrepancy is 0.0021588192; the maximum derived-midpoint
relative discrepancy is 1.9768339e-7. `run_summary.json` records 1152 accepted
model steps, 3456 implicit-stage records and 1160 byte-equal overlapping
checkpoint comparisons. Green independently checked the scalar math; Red
independently checked the raw-to-packet provenance. This is one mixed direction
on the registered dry map, not a physical observation or assimilation result.
