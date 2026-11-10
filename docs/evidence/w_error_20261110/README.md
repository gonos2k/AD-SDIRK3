# Complete selected W residual and stored-input norm-gap evidence

Native source d05130c0; baseline main3d4f7857 (PR347). The new default-OFF capture
and independent interval reference compare the **W residual block** at the same
two stored K values. This does not certify the whole RHS, exact merit decrease,
Stage3 iterative convergence or forecast accuracy/cost.

## Portable replay

From this directory, with Python3/NumPy and C++17/MPFR/GMP development libraries:

```sh
c++ -std=c++17 -O2 w_full_mpfr.cpp -I/opt/homebrew/include -L/opt/homebrew/lib -lmpfr -lgmp -o w_full_mpfr
python3 test_w_full_mpfr.py
python3 test_prepare_w_full_manifest.py
python3 test_norm_exact.py
python3 test_w_block_decrease.py
mkdir -p replay/w_reference
tar -xzf w_full_inputs.tar.gz -C replay/w_reference
python3 run_w_full_mpfr.py normalized_manifest.json replay/w_reference replay_full_w.json
python3 norm_exact.py run/accepted_step_probe.json run/result.json replay > replay_norm_gap.json
python3 w_block_decrease.py run/accepted_step_probe.json normalized_manifest.json replay/w_reference replay_full_w.json > replay_w_decrease.json
```

Adjust the two include/library paths for another MPFR/GMP installation. The W
wrapper refuses an existing output file; use fresh replay names and preserve the
archived results. Norm-only replay needs NumPy and the captured arrays, not MPFR.
The archive contains all16 normalized inputs (~46MB uncompressed, ~31MB gzip).
Its B/K/R arrays are full packed vectors, intentionally retained for complete W
and full stored-residual norm evaluation and later component work. No private
computer path is required by these replay commands.

Numeric `analysis` subobjects and exact norm fractions/hex can be compared with
the saved results. Binary hashes, paths and compilation metadata can differ when
building a new reference executable. Binary64 upper bounds require parsing the
JSON numbers as binary64 and matching their exact hex strings; do not reinterpret
the decimal digits as exact-rational upper endpoints.

## Target definition

B, a, actual fixed C and stored K are exact binary input values. The target forms
**exact-real Q(B+aK)**, recomputes ideal upstream alb/EOS, current continuity from
projected U/V, hybrid masses, pressure/buoyancy and W product-rule conversion,
then compares K_W−F*_W against stored native R_W at all215865 W entries, aliases
included. It does not freeze native Mdot, native alpha_w or rounded native Y.

Formation **before** the defined B/a/K/C inputs (stage assembly or physical data
initialization) is outside this pointwise target. Within the evaluation, B+aK,
EOS, mass-flux reciprocal/products/sum, W conversions and final residual subtraction
are included by the direct native-vs-real residual comparison. Correct directed
MPFR arithmetic, complete source mapping and faithful capture remain assumptions;
a native backend operation-count or gamma error model is not assumed.

The profile is mode3 ImplicitOnly, one tile, FP64, unit maps, dry, MU parity true,
no HEVI/implicit transport/W damping or sanitizer replacement, same fixed context.
Actual qv/cqw were present and zero. Both mass/EOS RDNW inputs were captured and
checked bit-identical; RDN/top/g are actual W consumer values. Actual theta_base
was newly captured, so the new reference does not use the prior reconstructed
FNV-only temperature as a substitute.

Q copies horizontal aliases and sets the V normal walls; it does not set vertical
W bottom to zero. The reference retains the bottom product-rule term. Explicit
advection/Coriolis/diffusion/Rayleigh are excluded by actual mode/flags; active
post-pack divergence damping repacks W unchanged according to source. Source
pre-PGF W starts zero and the capture checks that value.

## Captured/native association and normalization

Nine exclusive raw capture files B,K0,K1,R0,R1,theta_base,c1f,c2f,mass_rdnw supplement
the earlier fixed EOS data. Metadata retains source dtype/shape, FNV and block
layout. All old pair/context hashes and16 EOS file SHA values matched. The
normalizer also validates actual raw bytes, source hashes, supported flags and
input identities before emitting normalized_manifest.json.

The normalized pressure_rdn and top_rdnw files are exact promoted Float32 values
derived from the actual consumer JSON; top_rdnw has **one** value, not fabricated
unconsumed entries. The wrapper creates a temporary uniform vector from the
observed W inverse-weight maximum as an **upper bound**, not a new native capture.
Current W min=max1 makes that vector exact on W. The wrapper records this scope.

Native NumPy B+aK (separate FP64 multiply and add) and signed-zero-aware packed Q
are validation only: producer U/V/mu/MUB/W digests and EOS ph/t/mu match. MPFR
still computes the exact-real target independently. Optional re-normalization
requires the original source tree, run receipt and old EOS bundle; it is not a
prerequisite to the portable normalized-input replay above.

## Norm difference nu

norm_exact.py exactly sums stored binary64 R_i squared using integer ratios and
shifts. Positive six-block S_inv extrema bound weighted squared norms; all current
blocks have equal min/max. Integer isqrt gives 256-bit dyadic square-root bounds.
The distance from the exact reported native RMS to this interval bounds nu for
the stored R/S inputs. Array SHA, actual FNV, inventory, contiguous full block
cover, pair, metric and H identity are checked. This includes the norm/scaling/
float-return gap without assuming a backend reduction bound, but is **not** the
physical residual evaluation error epsilon_G. Combine epsilon_G and nu only once.

## W-only mathematical decrease

Exact native W-block norm enclosures combined with the W evaluation bounds give
separated target W-merit intervals for this pair: before lower about6.0547284e-5
exceeds accepted upper about1.0551043e-9. The exact positive separation is in
w_block_decrease.json. This conditional corollary does not use the global native
RMS or add nu again. It checks packet origin/reference association and certifies
only the W block, with full-merit and Stage3 flags explicitly false.

The PR348 follow-up additionally compares the reference's complete input-SHA
map against the normalized manifest, pins the reference source SHA, and
cross-checks packed size, W count, layout and inverse-weight extrema against
the packet. A rebuilt reference binary may have a different hash. Fourteen
contradictory-metadata cases are rejected; the original real-data decrease
output is byte-identical. This is a consistency check, not authentication of
an arbitrarily forged reference result. The old reference source and numerical
results are unchanged; the original evidence inventory is retained with the
follow-up validation record.

## Execution, provenance and limits

Source/header ABI change rebuilt all five recorded consumers with matching flags;
26 archive members were byte-identical, Fortran/toolchain provenance matched,
helper compile/run passed, and the single archive/full link map proves current
Tile/Newton method origins. Current default-OFF CI and active macro/native evidence
are separate. New native h600 run1 exits86 after the same first Stage3 accepted
update: Stage2 is logged converged, Stage3 incomplete, completed timesteps0.

Reference registration pins the source, binary and normalized case before one
full-grid evaluation. It completed in63.21s with unchanged pins. Wrapper rusage
alone is not the C++ child's peak-memory bound and is not forecast performance.
The initial private Python launcher had a parse typo before registration/evaluation;
its text and correction are retained. No native/MPFR result or threshold was replaced.

Build, launcher and packaging scripts contain original local paths as archival
provenance, not portable replay prerequisites. External native binaries/objects,
Fortran stack and raw NetCDF are not supplied. Full WRF reexecution needs that stack.
New RK3/endpoint forecast/speed comparisons were not performed; initial21-field
A9/RK3 evidence is reused at its original scope, with the new initial comparison.

Complete W and nu bounds are for this defined target and two observations only.
Other residual blocks' epsilon_G remain unbounded here, so full e0/e1, strict merit
decrease and Stage3/time/forecast convergence remain uncertified. Earlier NO-GO
and all other incomplete E/T/A/F statuses are preserved.
