# Selected-call W coefficients and actual inverse-metric maximum

Source baseline main e5567710; new native source ddc9475f. This default-OFF
accepted-step diagnostic observes the actual W-PGF consumer values at the base
and accepted trial. It is not a forecast completion or full RHS error certificate.

## Portable replay (pure Python standard library)

From this directory:

```sh
python3 test_analyze_w_runtime.py
python3 analyze_w_runtime.py run/accepted_step_probe.json ../eos_descent_20261110/run/accepted_step_probe.json ../alb_error_20261110/alb_analysis.json run/result.json > replay_w_component.json
```

The script takes explicit paths and only writes JSON to stdout. Use a new replay
file, preserving the original artifacts. The prior accepted-pair and MPFR analysis
files are supplied by the same repository. The code checks exactly16 EOS file
identities, two endpoints, constants, context/pair/metric hashes, and actual
consumer metadata before calculating exact Fraction bounds. These commands need
neither Torch/MPFR headers nor a native WRF/NetCDF installation.

All16 new EOS arrays matched the prior analysis SHA256s, so that data bundle is
reused rather than duplicated. run/result.json contains the new files' hashes,
and green_w_runtime_postreview.json records their actual dtype/shape/FNV checks.
The original raw arrays can also be recovered from the earlier eos_inputs.tar.gz
for an independent byte check; the coefficient calculation above only needs the
published scalar/metadata receipts and prior pressure-error bounds. It does not
rerun the old full MPFR analysis.

## Observed values, not source-default guesses

The W consumer captures its actual CPU Float32 RDN tensor, used index range,
gravity scalar and separately selected Float32 top RDNW. It also records the
canonical/dry branch and positive finite map, mass and alpha_w operand extrema.
Existing EOS capture scope ties them to the same base/trial as the accepted K/R.
The endpoint fixed RDN/g/top/map summaries agree. This is not all-private-state
or all-Newton-step identity evidence.

The cloned metric slice is the cached-layout rw block [432896,648761), count215865,
Float64, with observed min=max1 and the full inverse-metric hash matching the
original receipt. No full weight array is newly stored, and the previous floor
proxy is not used for the new calculation. Observing a max does not certify the
native norm, sqrt or float conversion errors.

Candidate RDN/g/top values from PR346 match these new observations for the selected
pair. candidate_observation_comparison.json records this without changing the old
artifact's historical verified=false fields. Neither the old conditional experiment
nor its criteria are retroactively changed.

## What the new mathematical coefficient means

alpha_w here is the native canonical mass/map operand (Pa), **not EOS alt**
(m³/kg). The separately extended level_mass_w is not assumed pointwise identical
on packed aliases. The calculation bounds the pressure-input sensitivity of a
real intermediate model holding the captured alpha_w, msfty and velocity_mass
operands fixed. Its coefficient uses g*RDN/(min(msfty)*min(alpha_w)), with factor2
at the dynamic top. Positive velocity mass cancels only in this real model.

Native ratio formation and every finite-precision multiply/divide remain a separate
remainder. So do coefficient/state formation relative to the full target G*, U/V
primary and NH4, other RHS, input formation and metric arithmetic. Result status
`selected_pair_observed_operand_bound` and the observed=True fields mean the
selected operands were recorded; `full_rhs_or_native_error_certified=false`
remains authoritative. The bounds are exact Fraction strings; decimal displays
are approximations.

## Build and execution provenance

PressureEosContext layout changed, so all five recorded header consumers were
rebuilt with matching headers/macros. The other26 archive members were checked
byte-identical. Helper assertions were active and compile/run returned0. The
single SDIRK archive link and full map (wrf_link.map.gz) identify direct Tile and
Newton method bodies at IDs465/466. Existing Fortran1270 artifacts,18 dylib entries,
SDK/compiler/linker/OS were verified; no Fortran clean rebuild is claimed.

The initial8706 build failed at dependent-template lambda syntax, before helper,
link or model execution. build_attempt_8706 preserves that failure. ddc9475 changes
only the lambda parameter types, then rebuilds all five consumers cleanly. The
runner's earlier wrong-prior-packet SHA preflight was also corrected before any
native run; its original preflight record is retained. No failed numeric experiment
or time gate was overwritten.

native_registration.json pins382 files before one h600 run, with no retry. The
result exits86 intentionally, matches the original pair and16 EOS files, logs only
Stage2 convergence, and completes zero timesteps. The initial21 fields/coefficient
comparison reuses archived A9/RK3. This is not a new one-step forecast regression,
RK3/speed/endpoint comparison, or evidence of Stage3 completion.

Build/runner/packaging scripts retain original absolute paths as archival tooling;
they are not the portable replay commands above. External executables, objects,
raw NetCDF and toolchain are not included, so full WRF reexecution requires that
original stack. Their hashes/argv and full link map provide the provenance record.
The same source/capture, prior theta-fingerprint and MPFR assumptions still apply.
No claim about full e0/e1, exact merit decrease, whole Newton convergence or E/T/A/F
follows from this component or this observer run.
