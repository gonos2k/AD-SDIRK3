# Two-time adjoint portable recomputation

`recompute.py` is a NumPy-only audit of the frozen two-time objective and the
four new checked Fortran FP32 pullbacks. It imports no historical analyzer and
does not launch or build a model. Run `python3 recompute.py --self-test` before
use; this reads no run data.

After all four prefixes complete, collection is:

```sh
python3 recompute.py \
  --evidence-root /Users/yhlee/AD-SDIRK3-review-evidence/native-two-time-adjoint-20261021 \
  --old-root /Users/yhlee/AD-SDIRK3-review-evidence/native-terminal-adjoint-20261020
```

This writes `native_two_time_adjoint.npz`, `manifest.json`, and
`recomputed.json` here. The portable calculation can then be rerun without the
raw run trees:

```sh
python3 recompute.py --npz native_two_time_adjoint.npz --manifest manifest.json
```

The NPZ schema is frozen before reading any new prefix result:

| Array | Shape | Dtype | Meaning |
| --- | --- | --- | --- |
| `case_labels` | `(6,)` | Unicode | baseline, replay, and four old FD cases |
| `old_eta_f32` | `(6,)` | little-endian float32 | Actual transmitted old perturbation sizes |
| `old_mid_states_f64` | `(6, 24531)` | little-endian float64 | Original accepted state at step 128 |
| `old_terminal_states_f64` | `(6, 24531)` | little-endian float64 | Original accepted state at step 256 |
| `q_mid_f32` | `(24531,)` | little-endian float32 | Frozen midpoint theta cotangent `q128` |
| `q_terminal_f32` | `(24531,)` | little-endian float32 | Original terminal W cotangent |
| `direction_v_f32` | `(24531,)` | little-endian float32 | Original control direction |
| `old_g0_terminal_f32` | `(24531,)` | little-endian float32 | Reused terminal-only initial gradient |
| `old_lambda128_terminal_f32` | `(24531,)` | little-endian float32 | Reused terminal-only cotangent at step 128 |
| `new_prefix_ends_i32` | `(4,)` | little-endian int32 | `[128, 96, 64, 32]` |
| `new_lambda_in_f32` | `(4, 24531)` | little-endian float32 | Actual FP32 chain input per new prefix |
| `new_lambda_out_f32` | `(4, 24531)` | little-endian float32 | Actual Fortran FP32 return per prefix |
| `new_local_precast_f64` | `(4, 24531)` | little-endian float64 | Captured local pre-cast FP64 VJP |

Collection hash-pins the prior portable manifest, six-case retained-state NPZ,
and frozen pre-AD record, carrying forward their six raw receipt/checkpoint
pins without reparsing those completed old runs. It independently verifies the
prefix160 `gradient_control_step=128` cotangent, the prefix032
`gradient_control_step=0` terminal-only gradient, and the one-time injection
`Q32(promote64(lambda128)+promote64(q128))`. It verifies each new receipt,
source/executable/protocol/input binding, `3N` Newton records, exact accepted
prefix ledger bytes, reference counts, lambda edges, and Fortran FP64-to-FP32
casts. Prefix 032 has no warm suffix file by design; its seed audit uses the
recorded checkpoint 0, which must also equal the old baseline checkpoint 0.

The recomputation forms `Jmid = dot(q128, Z128)`, `Jterminal = dot(qN, Z256)`,
and `Jtwo = Jmid + Jterminal` from FP64 states and promoted frozen FP32
cotangents. It calculates full and midpoint-increment FD/adjoint checks
separately with their protocol-defined signal floors and shared-signal relative
denominators. `dADmid` is `dot(v, FP64(new_g0_f32) - FP64(old_g0_f32))`; the
subtraction occurs after promotion. Per-window FP64 captures remain local
pre-cast diagnostics. The actual `lambda128_new - lambda128_old - q128` is
reported as local addition-rounding error and never redefines `q128`.

The synthetic negative check omits the midpoint contribution: its full relative
error is about 0.148% and could appear to pass alone, while the separately
normalized midpoint error is 100% and must fail. A one-sided relative gap above
1% at both widths marks that component's finite-width direction check
unresolved; otherwise the check only lacks that unresolved flag and never
proves classical differentiability. Taylor residuals are diagnostics only. The
output excludes an exact-global FP64 gradient, a second global gradient replay,
and general differentiability claims.

## Actual outcome

All four native lower-prefix runs completed: 320 model steps, 960 implicit
stages, and 324 byte-equal checkpoint comparisons across overlapping prefixes.
Both the total and added-midpoint contributions pass the frozen 1% comparisons
at both widths. Maximum relative errors are `1.335e-8` (total) and `7.455e-7`
(midpoint). See `recomputed.json` for values and individual decisions.

Exit code zero means recomputation succeeded. Acceptance requires both
`finite_difference.two_time_full.frozen_component_checks_pass` and
`finite_difference.midpoint_increment.frozen_component_checks_pass`. The compact
mode verifies pinned arrays and chain invariants, not omitted raw checkpoint
streams; the raw collector and independent closing reviews perform those byte
comparisons. New receipts are copied under `receipts/`; raw runs remain at the
external evidence root in the manifest. Existing build/archived RK3 context is
reused from `../native_terminal_adjoint/` without a new build or RK3 run.

This is a harness-level two-time linear diagnostic using existing native
Fortran pullbacks, not a production observation-cost scheduler. Exact global
FP64, initialization-map differentiation and all-direction certification remain
outside its scope.

The full Taylor diagnostic is near quadratic (orders 1.99986/2.00059). The
midpoint-only residual does not resolve a quadratic remainder at these widths
(orders 1.06/0.90); it is compatible with the scale of the small FD/adjoint
discrepancy. This is recorded without attributing all of it to rounding or
changing the predeclared acceptance conditions.
