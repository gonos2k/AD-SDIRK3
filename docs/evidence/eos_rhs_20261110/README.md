# Conditional native al_pert and W pressure-input component

Source baseline: main9e7d69d8 (PR345); reused native data source354748d6.
No production source, ABI, tolerance or native executable changed or ran here.
These are two conditional component results, not a complete native RHS bound.

## Replay from this directory

Use Python3 with NumPy. No MPFR headers or WRF run is needed for these replays:

```sh
python3 test_recover_alpert_bound.py ../eos_descent_20261110/eos_domain_bounds.py
python3 recover_alpert_bound.py ../eos_descent_20261110/run/accepted_step_probe.json ../eos_descent_20261110/eos_domain_analysis.json ../alb_error_20261110/alb_analysis.json > replay_alpert.json
mkdir -p replay
tar -xzf w_inputs.tar.gz -C replay
python3 test_w_pressure_sensitivity.py
python3 w_pressure_sensitivity.py w_coefficients.json ../alb_error_20261110/alb_analysis.json ../eos_descent_20261110/run/accepted_step_probe.json replay/eos_raw replay_w.json
```

The al_pert command locates the original interval implementation relative to the
explicitly supplied historical receipt bundle. It verifies that module and the
receipt/domain/ALB-result SHA256s; it does not rely on an author's absolute path.
The W evaluator takes explicit paths and refuses an existing output file.
Redirect to new replay files; preserve the archived JSONs. Fractions are the
exact authorities; binary64 al_pert upper bounds also carry exact hex encoding.

The small `w_inputs.tar.gz` is a byte-identical four-file fragment of the previous
16-file EOS archive: base_mu, accepted_mu, mu_base and rdnw. It is **not a new
capture**. Their SHA/FNV and dtype/shape checks connect them to the original
packet and ALB analysis. The fragment avoids requiring the full old archive for
this W-only calculation. The prior 425088-point MPFR calculation is not repeated.

## Native al_pert result and assumptions

The identity is u_hat−u*=(alpha_hat−alpha*)−(alb_hat−alb*)−eta_add. Reconstruct the
original *rounded-operation* interval immediately before alt=al_pert+alb, not the
later widened exact-real interval. Exact pre-add and rounded-alt envelopes stay
inside magnitude8; one-ULP finite binary64 addition then permits Eadd=2^-50.
The composed-alpha and upstream-alb bounds are summed as exact Fractions and
converted upward. No induced al_pert term is added again.

This assumes source-ordered separate binary64 operations, the FP64 output/cast,
original state box and fixed coefficients, faithful snapshots and the existing
MPFR/input provenance. Theta remains reconstruction/FNV-qualified. There are no
new direct native al_pert samples and no new backend-rounding certification.

## W result is conditional input sensitivity, not native error

The active ImplicitOnly WPGF/buoyancy term depends on EOS pressure. The inspected
inverse-alt diffusion consumers belong to do_explicit and are outside this callback;
diffusion is not declared globally OFF. U/V primary and NH4 are separate paths.

Exact-real fixed-state pressure changes are bounded with the source interior and
dynamic-top stencils and positive hybrid masses. Coupled/mass/map cancellation
is real algebra only. All W arithmetic and coefficient/state-formation error
remains in the unbounded remainder, together with other RHS contributions.

The coefficient candidates carry c1f/c2f/map/dry fingerprints matching the Stage3
packet. RDN also matches the same-run initial output, but **the selected Stage3
RDN was not retained**. Gravity is the source9.81f candidate, not a runtime sample.
Both runtime verified flags remain false. Top RDNW uses the source Float32 request
on the captured coefficient and assumes consistent getter semantics.

Sinv values were not retained. The weight cap1+2^-23 is conditional on the
inspected scale_u>=1 source/profile path, no physics-scale replacement, FP32/64
cast and reciprocal semantics. It is not a measured weight. The mathematical
RMS factor uses 4*Nw<N and H=I; it does not bound the implementation's norm or
float conversions. Result status stays `conditional_source_profile_bound` with
`full_rhs_or_native_error_certified=false`.

## Provenance and remaining work

Registrations precede the exact-fraction evaluations; original pins remain
unchanged. Green/Red source and math/data reviews are included. The optional
prepare_w_coefficients.py needs the original matching wrfinput and xarray; it is
not needed by the portable replay above, which uses the supplied candidates.
The private paths in archival registration/execution receipts describe the
original local experiment, not prerequisites for those replay commands.

No new WRF/RK3, endpoint forecast or speed comparison occurred. Earlier A9/RK3
initial-field evidence and Stage2-converged/Stage3-incomplete/zero-step scope are
reused only as originally recorded. Native W arithmetic, actual coefficient and
weight closure, U/V primary/NH4, other RHS, input formation and metric conversion
still prevent full e0/e1, exact merit decrease or Stage3 convergence certification.
