# Exact Newton-local JVP reuse candidate

Local timestamp: 2026-10-31T17:14:04.188321+09:00.
Base: bceffd2f190d5d038dcca4a7d615fe581d8a88c0 (merged PR327).
All Green/Red agents use GPT-6.1-Terra medium.

Mode 3 remains default; mode 4 remains a non-adopted transport-consistency
candidate. No mode 5, damping, spatial discretization, ARK coefficient or final
Newton/Krylov tolerance change is proposed. Late W noise remains unresolved.

At one Newton point x, prepare y=F(x) and g(w)=J(x)^T w once with reverse AD.
Then grad_w(g(w) dot v)=J(x)v gives a true JVP, not the incorrect single VJP
J(x)^T v. This captures state-dependent mass, Omega, pressure and flux
contributions; coefficients are fixed only because the Newton point is fixed.
A new point prepares a new object. The object is never shared across steps.

The production candidate is default OFF via reuse_linearization. It requires
the strict forward-only carried FP64, dry, whole-tile single-rank WRFParity
mode-3/4 profile. Solver-local options control selection, with explicit
configuration wiring and unsupported-context rejection. Before enabling a
prepared solve, its primal must match the base RHS bit-for-bit and its tangent
must agree with a direct dual-number sample (rtol1e-10, atol1e-12, no FD).
Unsupported AD paths fall back to the existing JVP; allocation failures use the
existing failed-stage policy. Eligible performance evidence requires no fallback.

The tape exists only during one GMRES/FGMRES call. Any intervening RHS call
through the solver destroys it before physical work buffers are touched. Tile
output graph owners are detached by reassignment, preserving values including
pressure writeback. Ordinary and injected callback exceptions are covered;
cleanup under allocator exhaustion is not guaranteed and must fail closed.
Retained adjoint, saved trajectories, changed references and AD halo are excluded.

Preliminary disposable C++ probes (not native WRF) matched fwAD below 6e-17 at
synthetic 40x80x64 shape. Six directions including preparation took 1.094 vs
1.224 s for mode3 and 2.045 vs2.348 s for mode4. Process peak RSS was ~2.0GB,
not an incremental graph-memory measurement. Small-tile reachable-node checks
expired all observed nodes while Tile remained alive; immutable output values
and normal/injected-exception cleanup were checked. These are feasibility
results, not a native speedup, new physical accuracy or late-noise repair.

Two focused tests cover JVP orientation, mass-flux derivatives, repeated
directions/point refresh, actual Tile owner lifetime and the actual Newton
solver versus a dense nonsymmetric exact root with preparation-failure fallback.
Affected default-off, config, FP64, adjoint and diagnostic checks are also selected.
The integrated candidate has not yet passed those fresh-core checks.

The Registry field changes Fortran configuration layout: a full clean native
Fortran+C++ rebuild is required. Archived native objects will not be mixed with
new headers/configuration. This rebuild and native comparison are still pending.
The first screen is a pinned OFF/ON one-step pair at600s with common canonical
input, existing8eps output screen, strict residuals,4GiB peak-RSS acceptance
bound, pre/post space checks only, and no retries. Only if this passes and
shows full-step benefit will a separately pinned six-step A-B-B-A block begin.
Full costs include preparation and reference validation. No long run starts
from these component tests. The immutable preregistration is stored locally at
`/Users/yhlee/AD-SDIRK3-review-evidence/newton-local-linearization-20261031/native_screen_preregistration.json`.

Same-setup native RK3 fields and runtime are preserved from PR327; no new model
run or RK3 comparison has yet been performed for this reuse candidate. The
600s canonical case and existing references will be reused, not a smaller dt.
