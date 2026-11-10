# Same-pair Stage3 EOS evidence

Native source: `354748d60248d2b407ef18b646b618e28cbf445a`, main baseline4907880c.
The later Tile patch05aac9ac changes comments only. This bundle supports the
**rounded-input EOS component and conditional EOS-domain result**, not a full RHS,
exact residual-decrease or Newton/forecast convergence certificate.

## Offline replay

Unlike a full WRF replay, this EOS analysis includes its16 captured input/output
arrays in `eos_inputs.tar.gz`. Extract from this directory with
`tar -xzf eos_inputs.tar.gz -C run`. The run/accepted_step_probe.json supplies
shape, original dtype, constants and byte FNV. SHA256/size is in manifest.json;
run/result.json also preserves original uncompressed array hashes. The NPY files
are flat little-endian FP64 with original shape/dtype metadata. FP32 sources must
promote exactly, including signed zero. Both endpoints include212544 EOS cells.

Use Python3 with NumPy and a C++17 compiler with MPFR/GMP development libraries:

```sh
c++ -std=c++17 -O2 eos_mpfr.cpp -I/opt/homebrew/include -L/opt/homebrew/lib -lmpfr -lgmp -o eos_mpfr
python3 test_eos_mpfr.py
python3 run_eos_mpfr.py run/accepted_step_probe.json replay_mpfr.json
python3 evaluate_eos_domain.py run/accepted_step_probe.json > replay_domain.json
python3 test_eos_domain_bounds.py
python3 test_evaluate_eos_domain.py
python3 descent_budget.py > replay_budget.json
```

`descent_budget.py` reads the bundled `run/accepted_step_probe.json` relative to
its own file and writes JSON only to stdout. It does not overwrite the archived
`descent_budget.json`; the displayed command selects a separate output.

The shown include/library paths are the recorded macOS install; adjust those two
paths for another MPFR/GMP install. run_eos_mpfr.py refuses an existing output
file. MPFR bounds are upward-rounded binary64 and have exact companion hex strings;
do not treat the decimal digits as exact-rational upper bounds. Offline rerunning
of this same checker is reproducibility, not a second independent implementation.

Domain replay uses the pinned scalar extrema, source operation order and explicit
basic-rounding/no-overflow assumptions. It rejects unsupported/nonpositive rdnw;
it does not silently substitute abs. It does not bound pow arithmetic accuracy.
The MPFR replay supplies an EOS arithmetic bound at the rounded inputs; it does
not include upstream alb/state formation or propagation through other RHS terms.

## Native provenance and exclusions

native_registration.json fixes one attempt and the old accepted-pair matching
condition before execution. The initial draft had one stale review hash; the root
preflight refused registration before any run. Its final_pin_refresh records the
review3547 hash used by the actual registration. No numerical threshold changed.
run/result.json retains original completed diagnostic status and pending offline
analysis text; separate eos_domain_analysis/eos_mpfr_analysis add postvalidation
without rewriting it. Every16-array byte hash was checked. Stage2 is logged as
converged; Stage3 and the timestep were not completed. Terminal exit86 is intended.

raw_capture/build_receipt.json and link_receipt.json pin the new Newton object,
single archive, executable and full link map (`wrf_link.map.gz`). Direct Tile and
Newton method bodies map to member IDs465/466. Existing Fortran artifacts and30
unchanged archive members were checked, not cleanly rebuilt. The helper receipt
records the enabled standalone assertion test; generic CI covers the default-OFF
configuration. No new user option or ABI change is introduced.

Native scripts and original receipts retain absolute experiment paths. They are
archival, not a portable WRF launcher. External native executable/archive, original
Fortran stack and raw NetCDF are not included. They are unnecessary for the above
EOS-only replay but required for full WRF reproduction. The old packet/input/run
references remain at their original immutable paths and prior evidence bundles.

Initial21 fields/coefficient checks use archived A9 and RK3 on the same standard
setup. No new RK3 run, endpoint forecast comparison, speed comparison, P1 cost
experiment or long integration is claimed. Diagnostic wall/CPU/RSS include real
capture costs. Additional solver calls0 is not zero overhead or continuation parity.

Green/Red receipts are scoped reviews, not complete Jacobian proofs. The rejected
native-norm shortcut is retained as a limitation (`red_norm_component_math.json`):
backend operation counts and libm dispatch were not certified. Full e0,e1 and the
strict descent inequality remain open despite the small local EOS errors.

The replay-budget path/stdout correction is a follow-up to PR344. The native
registration, captured arrays, `descent_budget.json` and numerical results remain
unchanged; the original script and manifest remain available at main2c1dcce1.
