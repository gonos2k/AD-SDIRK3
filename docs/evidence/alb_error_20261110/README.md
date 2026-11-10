# Base-alb arithmetic and its EOS propagation

This is an offline follow-up to PR344/main2c1dcce1 using its same Stage3 accepted
pair (native source354748d6). No production formula, solver tolerance, or native
executable was changed or run. Exact full-residual decrease remains uncertified.

## Portable offline replay

The previous EOS bundle is reused, not duplicated. From **this directory**:

```sh
mkdir -p replay
# Extracts only the sixteen already-published EOS arrays.
tar -xzf ../eos_descent_20261110/eos_inputs.tar.gz -C replay
gzip -dc theta_base.npy.gz > replay/theta_base.npy
c++ -std=c++17 -O2 alb_mpfr.cpp -I/opt/homebrew/include -L/opt/homebrew/lib -lmpfr -lgmp -o alb_mpfr
python3 test_alb_mpfr.py
python3 test_widen_alb_tube.py
python3 evaluate_alb.py ../eos_descent_20261110/run/accepted_step_probe.json replay/eos_raw replay/theta_base.npy reconstruction.json replay_alb.json
python3 widen_alb_tube.py ../eos_descent_20261110/run/accepted_step_probe.json ../eos_descent_20261110/eos_domain_analysis.json replay_alb.json > replay_tube.json
```

Use Python3 with NumPy and C++17 with MPFR/GMP development libraries. The recorded
include/library paths are macOS Homebrew paths; adjust them for another install.
The evaluator refuses an existing output file. Do not overwrite the archived
analysis or registration files. Numeric subobjects/hex bounds can be compared;
checker binary hashes naturally differ when compiling on another host.

`widen_alb_tube.py` is deliberately registered to the historical receipt and its
original domain-file SHA256, not a generic proof for arbitrary pairs. It checks
that the new alb analysis belongs to the same receipt. The result is an exact-real
domain for the old state box with widened alb, not a counterfactual FP64 execution.

The previous bundle's corrected script resolves its receipt independently of the
working directory. From this directory:

```sh
python3 ../eos_descent_20261110/descent_budget.py > replay_budget.json
```

Here the relative **script path** is relative to this directory; the script itself
resolves its bundled receipt relative to its own file and only writes stdout.
Its archived descent_budget.json and all native evidence are preserved.

## Inputs and target

The original EOS packet, 16 arrays and constants are reused. The additional base
theta is reconstructed from the retained standard em_b_wave wrfinput: FP32
T_INIT+300, periodic-X/symmetric-Y padding, then exact promotion to FP64. It has
shape [81,64,41], data FNV 4185430885422016772. Pressure reconstructed from PB is
bitwise equal to the captured p_base array, FNV 14087318007462321114. **Original
th_base bytes were not retained**: its match is fingerprint evidence with possible
hash collisions, not a direct byte comparison. The mathematical bounds are
conditional on faithful input reconstruction and capture. Source, dtype, shape
and reconstruction assumptions are recorded in the Green/Red input audits.

The target takes these rounded theta_base/pressure_base values and the stored
float constants as exact real inputs. Upstream T_INIT/PB or state formation error
is excluded. The active constants are rd=287, cv=717.5, cp=1004.5. The base inverse
density call uses p1000mb=100000 explicitly, separate from the captured EOS p0.
`reconstruct_alb_inputs.py` optionally reconstructs this theta from an available
original wrfinput (requires xarray), but the default replay above uses the supplied
compressed theta and needs neither xarray nor native WRF/NetCDF inputs.

MPFR256 directed intervals enclose ideal base alb, induced exact alpha/al_pert/p
changes, and direct native alpha/p errors versus the composed real-alb EOS. Both
endpoints include all212544 EOS cells, including aliases. Bounds use RNDU binary64
and accompanying exact hexadecimal encodings. The new standalone reference adapts
interval/NPY/norm utilities from the pinned previous EOS checker; the production
hydrostatic functions remain the authoritative numerical formulas.

Induced shifts and direct composed errors are different quantities and must not
be added to double count the same error. Native al_pert was not captured: its
reported quantity is **only the exact-real shift caused by alb error**, not the
native al_pert arithmetic error. The widened tube also bounds only this input
sensitivity, under the prior state-box/basic-arithmetic assumptions.

## Registration and limits

alb_registration.json pinned38 code/input/review files before one all-cell offline
analysis. Execution completed with the pins unchanged. Timing is offline-analysis
cost, not forecast performance. The subsequent exact-fraction tube computation
was fixed and reviewed before those MPFR results; it rejects a domain reaching0.
The old arrays, native run and P1 NO-GO were not rerun or reclassified.

`register_and_run_alb.py` and `publish_alb.py` retain experiment paths as archival launchers;
it is **not** one of the portable replay commands. Reproduce the component with
the explicit-argument commands above. The original native executable, archive and
raw NetCDF are not included and are unnecessary for that component replay.

The remaining budget includes input formation, actual downstream PGF/buoyancy/mass
transform and other RHS arithmetic, native al_pert arithmetic, and scaling/norm/
float conversion. No Pa error is added directly to dimensionless residual RMS.
Full endpoint errors e0,e1, exact merit decrease, Stage3 iterative convergence,
E/T/A/F, W noise, forecast cost and five-day validation remain open. No new native,
RK3, endpoint forecast or speed comparison was performed in this follow-up.
