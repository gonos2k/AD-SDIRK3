# SDIRK3 Krylov early-stop paired ablation

This harness distinguishes a real Stage-2/Stage-3 linear-operator plateau from a
failure caused or amplified by the current Krylov early-stop policy. It does not
change production defaults. Every run executes in an isolated workspace and uses
existing environment variables only.

## Why this experiment is required

With `WRF_SDIRK3_BLOCKMAX_GATE=1`, the dt=600 Stage-2 global Newton metric no
longer hides the `mu` block. The next FGMRES solve was observed to stop after
about seven Krylov iterations with `rel_error=1`. Seven iterations coincide with
the forced midpoint probe for a restart length near 15, so that observation alone
does not prove that a full Krylov cycle is incapable of reducing the residual.

The matrix keeps the restart length and Stage-2 Krylov tolerance explicit while
changing the early-stop route:

| Case | Stage-2 restart override | Restarts | Arnoldi stagnation | Purpose |
|---|---:|---:|---|---|
| `A_legacy` | `restart` | 1 | current policy | reproduce the aggressive midpoint route |
| `B_normal` | 0 (inherit same base restart) | 1 | normal window | remove the stage2-restart trigger but retain measured stagnation |
| `C_full1` | 0 | 1 | effectively disabled | force one complete restart cycle |
| `D_full3` | 0 | 3 | effectively disabled | test whether additional Krylov dimensions/restarts help |

`stage2_krylov_tol` is explicitly set in all four cases. This prevents the
Eisenstat-Walker budget coupling from changing the effective restart count and
keeps the comparison paired. GMRES/INN warm starts and JVP auto-benchmarking are
also disabled so each process starts from the same controlled linear-solve path.
The harness fails closed if the emitted effective restart, restart count, or
tolerance differs from the requested case.

The current configuration exposes no environment override for the separate
restart-to-restart stagnation threshold. Therefore `D_full3` can still terminate
with `restart_stagnation_threshold`; the summary flags that result as an invalid
full-budget control instead of treating it as proof of an operator plateau.

## Run

From any directory:

```bash
python3 external/libtorch_wrf/sdirk3/tests/krylov_early_stop_ablation.py
```

The default run directory is `<repo>/test/em_b_wave`, with expected
`time_step = 600`. Important overrides:

```bash
python3 external/libtorch_wrf/sdirk3/tests/krylov_early_stop_ablation.py \
  --run-dir /path/to/test/em_b_wave \
  --output-dir /path/to/results \
  --restart 15 \
  --krylov-tol 0.30 \
  --long-restarts 3
```

The output contains:

- one combined WRF/RSL log per case;
- the exact controlled environment per case;
- `summary.tsv` and `summary.json`;
- `verdict.md`, containing a conservative first-pass interpretation;
- `manifest.json`, recording the experiment parameters.

The parser can be tested without WRF or LibTorch:

```bash
python3 external/libtorch_wrf/sdirk3/tests/krylov_early_stop_ablation.py --self-test
```

## Reading the result

1. If `A_legacy` exits at the midpoint but `C_full1` materially lowers
   `rel_err`, the early-stop policy is causally limiting the solve.
2. If `C_full1` remains poor but `D_full3` improves, the one-restart budget is
   the limiting factor.
3. If `D_full3` still has `rel_err >= 0.99`, prioritize the paired
   operator/JVP/preconditioner/IMEX split diagnosis.
4. If `D_full3` still reports an early-stop reason, it is not a clean
   full-budget control; inspect the log before assigning root cause.
5. A NaN retry exhaustion or happy breakdown is a distinct failure class and
   must not be classified as stagnation.

The selected record is the first `SDIRK3_FGMRES_DIAG` with `stage=2 iter=0`.
All matching record counts are retained so a retry or repeated solve is visible.
