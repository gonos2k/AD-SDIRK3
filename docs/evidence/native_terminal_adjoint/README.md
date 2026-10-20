# Native terminal-adjoint recomputation packet

This packet records the registered dry `em_b_wave`, 240 s, `h=0.9375 s`,
Newton `1e-9` native terminal-objective check. Thirteen actual WRF processes
completed: one baseline, seven native prefix reexecutions for eight chained
32-step Fortran FP32 pullbacks, one state-only forward replay, and four fixed
finite-difference runs. The control is the solver-entry state after WRF
initialization. All eight local pre-cast FP64 outputs narrow bytewise to the
returned FP32 cotangents; these local captures form a precision audit.

The actual chained initial-gradient product is `-11052.647841641348`. The
central FD comparisons at nominal widths `.01` and `.03` have relative errors
`4.11e-9` and `1.44e-8`, respectively, against the frozen 1% criterion.
The two central estimates differ relatively by `1.85e-8`; both signal-floor
and finite-width one-sided criteria pass. These results cover one fixed
terminal W objective and one mixed six-field direction. Hard upwind signs
change under perturbation, so this is numerical consistency evidence within
the stated tolerance; it does not prove classical differentiability in all
directions. Multi-time observations and the initialization-map derivative
remain separate work.

`recomputed.json` contains the numerical decisions. Exit code zero means the
packet was read and recomputed successfully; acceptance requires the reported
FD agreement, signal, width-agreement and one-sided fields to satisfy the
frozen criteria. `state_replay_proof.json` records the independently checked
257-state replay and 903 prefix-endpoint comparisons. The full raw ledgers
are preserved in the manifest's external evidence root. `build_provenance.json`
and its twelve copied build receipts bind both native producers and the
compiler/MPI/netCDF/libtorch stack. `ci_sourcea9.zip` preserves the five-test
standalone CI artifact; its 8,480-component replay check is separate from this
native 24,531-component test.

Run the archived packet with:

```sh
python3 recompute.py --npz native_terminal_adjoint.npz --manifest manifest.json --output recomputed.json
```

Only Python's standard library and NumPy are required. The executable is pinned
as producer provenance and is never loaded. The packet includes unchanged
receipt JSON copies under `receipts/`; it does not include large raw checkpoint
streams. Therefore replay-ledger equality is checked from the baseline/replay
checkpoint SHA-256 and byte counts recorded in those pinned receipts. This is
not a fresh bytewise comparison of checkpoint arrays by this portable script.

The NPZ schema is fixed at `N=24531` and uses these arrays:

| Array | Shape | Meaning |
|---|---:|---|
| `case_order` | `[6]` | `baseline`, `replay`, `plus_0p01`, `minus_0p01`, `plus_0p03`, `minus_0p03` |
| `direction_f32` | `[N]` | Frozen mixed-block direction |
| `terminal_cotangent_f32` | `[N]` | Objective cotangent rounded once to FP32 |
| `control_base_f64` | `[N]` | Original solver-entry control, promoted to FP64 |
| `seeds_f64` | `[6,N]` | Actual six FP64 solver-entry seeds |
| `terminal_states_f64` | `[6,N]` | Six actual final internal FP64 states |
| `eta_actual_f32` | `[6]` | Eta values as transmitted through the float32 ABI |
| `chain_inputs_f32` | `[8,N]` | Cotangent entering the baseline final-32 window, then seven prefix windows |
| `chain_outputs_f32` | `[8,N]` | Actual chained Fortran FP32 pullback from each of those eight windows |
| `chain_local_precast_f64` | `[8,N]` | Captured local window VJP before the Fortran FP32 return cast |

The baseline chain starts with the full-run final-32 window and continues through
prefixes ending at steps 224, 192, 160, 128, 96, 64, and 32. Replay is
state-only; its ledger hash is checked, but this packet does not claim gradient
replay. The reported full-map directional derivative is
`dot(direction_f32 promoted to FP64, chain_outputs_f32[7] promoted to FP64)`.
The local pre-cast FP64 vectors are per-window diagnostics, not an independent
global FP64 chain. The recomputation reports each block's cast difference by
maximum absolute value, RMS, and relative L2 norm; it does not form
directional-dot comparisons for intermediate blocks.

The manifest schema is `native_terminal_adjoint_chain_manifest_v1`. It records:

- `npz`: packet filename, SHA-256, and byte count;
- `source_sets`: producer-specific repository-relative source paths and SHA-256
  pins (the reused V5 baseline and P6 cases may have distinct test-driver files);
- `executables`: producer-specific path, SHA-256, and byte count; each receipt
  names its producer ID, so the preserved V5 baseline executable is not confused
  with the P6 executable;
- `receipts`: all six primary receipts and seven baseline-prefix receipts, each
  with relative packet path, SHA-256, source-set and executable IDs, horizon,
  observed accepted-step count, observed implicit-stage count, and checkpoint
ledger SHA-256. Baseline-prefix receipts must identify the baseline prefix role,
zero eta, all expected/matched reference endpoints, equal actual/reference
prefix hashes, the full baseline ledger as their reference input, and the
immediately preceding baseline-chain FP32 cotangent as their second input;
- `cases`: each primary case's actual eta, seed SHA-256, terminal-state SHA-256,
  and receipt label;
- `replay_ledger`: baseline/replay checkpoint SHA-256 values, equality flag, and
  the expected 257 checkpoint count;
- `case_order` and `N`, which must match the arrays above.

Source and executable entries are provenance pins. The script checks each
receipt against its producer-specific pins but does not need the source checkout
or either executable to recompute the NPZ. It also binds each chain array row to
the corresponding receipt output hashes for `lambda_in_f32`, `lambda_out_f32`,
and the pre-cast wrapper capture.

Producer roles are fixed: only `baseline` may use source/executable ID
`baseline_v5` and protocol SHA-256
`b96cbe382b1e992c64e21b2ef3f413cf3aa7ba6b7bfaa31e0a43b91390c9ce45`; replay,
FD cases, and all seven prefixes must use `fortran_chain_v6` and protocol
SHA-256 `9971cb56d28022951dfe00c1b7707532cf53c9cc71ca88983c738812751e2650`.
All receipts must also pin the registered WRF input SHA-256
`e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`; the six
primary cases must share the preserved baseline namelist SHA-256. Prefix
namelists are expected to differ only by the frozen run horizon and are checked
through their per-receipt horizon/count fields.

The FD calculation follows the frozen v3 `full_fd` formula: use actual float32
eta promoted to float64, compare each central difference to dAD with the shared
`max(abs(dFD), abs(dAD))` denominator and no denominator floor, and require
`abs(Jplus-Jminus) > 100*eps64*max(abs(Jplus), abs(Jminus),
norm(lambda)*norm(ZN_baseline))` to clear the signal floor. It reports both
one-sided slopes and their relative gap. If both widths retain a one-sided gap
above 1%, the classical directional derivative remains unresolved even when the
central estimates agree.
