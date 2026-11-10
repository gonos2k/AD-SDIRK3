# Selected mass and perturbation-temperature residual blocks

This extends the PR348 fixed-input reference to the disjoint packed `rmu` and
`rt` blocks. The production solver, native binary, ABI and tolerances are
unchanged. The two endpoints and fixed inputs come from the existing Stage3
first-accepted-update packet. No new native WRF or RK3 run is performed.

## Target and source contract

As in the complete W reference, captured binary `B`, `a`, `K` and fixed `C` are
exact given real inputs. Form `Y=Q_in(B+aK)` in directed interval arithmetic;
recompute current U/V mass flux and column mass tendency from this state.
Formation before these given inputs is excluded. Unit maps, dry mode3
`ImplicitOnly`, single tile, MU parity on, no HEVI/implicit transport/sanitizer
replacement and identity residual mask are inherited from the existing guarded
packet. Output aliases follow the same packed projection. The final subtraction
uses the original stored `K`, including alias entries.

In the physical core, write `M=mu+MUB`,

```
CU_k = (c1h_k*average_x(M)+c2h_k)*u_k
CV_k = (c1h_k*average_y(M)+c2h_k)*v_k
D_m = sum_k [(-1/mass_rdnw_k)*(rdx*Delta_x(CU_k)+rdy*Delta_y(CV_k))]
F_mu = Q_mu(D_m)
F_t  = Q_t[-t_pert*c1h*D_m/(c1h*M+c2h+epsilon)]
G_b  = K_b-F_b,  b in {mu,t}.
```

The coupled temperature tendency is zero before the product-rule conversion on
this fast path. Explicit theta/omega advection is outside this callback. `t_pert`
is the perturbation packed variable, not absolute theta. Post-pack implicit
horizontal divergence damping changes U/V and repacks mu/T unchanged. These are
source/profile-specific equations, not all-mode formulas.

The Tile-local epsilon helper (not the similarly named metric utility) selects
promoted binary32 `1e-10f` or `1e-4f`. The packet did not record CPU autocast state.
The reference encloses **every fixed epsilon** in the closed interval between
these two exact binary values. This is a uniform family bound; it neither claims
an observed epsilon nor silently inserts the decimal real `1e-10`. The interval
also includes both actual choices. Other effective autocast/profile assumptions
remain those of the captured FP64 path.

## Reuse and scope

Reuse the 16 normalized arrays, normalized manifest and accepted packet from
`../w_error_20261110`. The new reference reads only the nine arrays needed by
mu/T; their hashes are checked against the old manifest. Do not regenerate the
closed W/EOS/norm results for this extension. `mu_theta_mpfr.cpp` adapts interval,
NPY, projection and mass-flux utilities from the pinned W reference, with source
attribution. This shares audited utilities rather than serving as a wholly
independent second implementation of continuity.

The output compares all packed mass and temperature entries at both endpoints
with their native residual blocks. Block weights are the observed positive
inverse-metric extrema, and every weighted RMS uses the **full packed N**.
These evaluation-error bounds exclude native norm conversion; the previously
bounded `nu` is separate and must be included only once in a future full-merit
proof. No whole residual error or whole merit decrease is certified here. U/V
and PH remain open; within-block terms cannot be combined as orthogonal blocks.
Stage3 iterative convergence, exact forcing, inverse/Lipschitz bounds, time
convergence, forecast accuracy and cost remain open.

## Pending blocks

PH includes both horizontal geopotential advection and vertical omega transport;
using only `g*w` would omit active terms. U/V include primary PGF, active NH4,
mass product-rule conversion and post-pack divergence damping. The source audit
records missing exact FNM/FNP/selector provenance instead of substituting
formatted or reconstructed candidates as same-call observations. Existing W and
stored-norm results keep their original accepted scope.

The source-audit receipts are navigation and scope evidence. The initial Red
mu/theta plan inspected the similarly named metric helper for epsilon; its
`1e-3` possibility is superseded by `red_mu_theta_epsilon_correction.json`,
which identifies the Tile-local helper actually called. The numerical reference
uses only the corrected `[1e-10f,1e-4f]` hull. The original audit is retained to
make this correction explicit. Graph edges likewise assist navigation and do
not prove numerical completeness.

## Portable replay

From this directory, using Python3/NumPy and C++17 with MPFR/GMP development
libraries (adjust include/library paths for your platform):

```sh
c++ -std=c++17 -O2 mu_theta_mpfr.cpp -I/opt/homebrew/include -L/opt/homebrew/lib -lmpfr -lgmp -o mu_theta_mpfr
python3 test_mu_theta_mpfr.py
mkdir -p replay
tar -xzf ../w_error_20261110/w_full_inputs.tar.gz -C replay
python3 run_mu_theta.py ../w_error_20261110/normalized_manifest.json replay ../w_error_20261110/run/accepted_step_probe.json ../w_error_20261110/run_w_full_mpfr.py replay_mu_theta.json
```

Use a fresh output name: the wrapper refuses an existing result. It verifies the
pinned prior packet, normalized manifest and validation source, then checks all
16 fixture hashes through the prior validator. Only the nine required arrays
are passed to the new reference. Compare `analysis` including the binary64/hex
upper bounds with `mu_theta_analysis.json`; the rebuilt binary hash may differ.
The archived execution/build paths are provenance, not replay prerequisites.

## Results and validation

The single registered all-cell run completed in 18.26 seconds, return code 0,
with all 24 registered source/data/test pins unchanged. The standalone reference
used Apple clang 21.0.0 and MPFR 4.2.2. This duration is offline analysis time,
not native forecast performance. Both endpoints include all 3,321 mu entries
and all 212,544 temperature entries, aliases included; N=1,080,491 throughout.
Actual inverse weights on these blocks have min=max=1.

| Conditional weighted mathematical RMS upper | Base | Accepted |
|---|---:|---:|
| mu | 3.6580367994881326e-16 | 3.61929848876647e-16 |
| T, entire epsilon family | 9.210741413599433e-15 | 9.656070767402656e-15 |

The authoritative upper endpoints are the upward binary64 numbers and their
exact hex representations in the result. Manufactured cases cover zero flow,
nonuniform mass with analytic product rule, nonzero K/a, epsilon-hull inclusion,
a deliberately wrong residual, and nonpositive-domain rejection. Green and Red
reviewed source equations, aliases, weights and provenance separately. Reused
W/norm/native records were not rerun. The P3 test rejects 14 inconsistent
metadata cases, permits a changed platform binary hash, and the real W-decrease
replay is byte-identical to the archived output.

No new native WRF, RK3, endpoint field, runtime or speed comparison was performed.
The existing captured run remains Stage2 converged, Stage3 first accepted update
followed by diagnostic exit86, zero completed timesteps. The historical P1 NO-GO
and earlier successful one-step regression keep their original scope.
