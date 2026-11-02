# Mode 3 implicit-stage residual and operator contract

Source baseline: `eca8f6fc`, with the bounded pre-change Graphify source pins
verified against `c7` for the affected 39-file corpus. The current source is
`fcd58bc9`, whose intervening Newton edit changes zero-update logging/comments only. Line numbers below identify
source locations in this worktree; they are navigation aids, not numerical evidence.
This document describes the actual stage construction and the conditions under which
its two residual coordinates are algebraically equivalent. It is not an O2/O3, adjoint,
spatial-equivalence, stability, or convergence proof.

## Stage equation and forcing

Let `i` be a zero-based ARK stage, `h = dt_ark`, `a = h * effective_aii`,
`K = k_fast[i]`, and `B` the **actual** `U_stage` passed to
`solveImplicitStage`. Before optional halo/retry processing, the source constructs

\[
 B_i=U_n+h\sum_{j<i}(a^E_{ij}k^E_j+a^I_{ij}k^I_j).
\]

The production composition is ordered by `j`; the equation is mathematical notation,
not a promise of associative floating-point evaluation
(`wrf_sdirk3_ark324_composition.h:13`, `wrf_sdirk3_tile_unified_impl.cpp:10129`,
`:10169`, `:10363`). The stage loop has an explicit first stage when `aii` is zero;
only an implicit stage has the invertible change of coordinates below. It passes
`effective_aii` and the possibly processed base to the solver (`:10580`, `:10734`).
A retry may sanitize the base; take `B` after that processing, not the pristine history.

For mode 3 the actual Newton callback is `compute_rhs_inner(Y, ImplicitOnly)`;
it adds neither `F_exp` nor `F_phys` (`:14123`, `:14140`). The slow stage
path evaluates `ExplicitOnly` and may add `F_phys` according to the tangent and
physics gates (`:6983`, `:7007`); previously completed slow values enter `B` via
ARK coefficients. `F_phys` is packed and boundary-projected (`:6296`, `:6420`).
It is not an extra term to subtract from the mode 3 fast residual. The thresholded
forward-only slow-physics branch and tangent-enabled branch must be distinguished;
no unconditional claim that all nonzero physics enters every configuration follows.

Define `F_C` as that actual callback with all its stage context `C`, and `Y=B+aK`.
Newton evaluates exactly the packed algebraic residual

\[
 G_K(K;B,C)=K-F_C(B+aK).
\]

The first Newton argument already is `B`, not the original timestep state
(`wrf_sdirk3_newton_solver.cpp:5518`, `:6030`, `:6404`). For `a != 0`, define

\[
 G_Y(Y;B,C)=Y-B-aF_C(Y)=a\,G_K((Y-B)/a;B,C).
\]

This identity is algebraic over the same `B`, `a`, callback and context. For the
implemented finite-precision expressions it does not imply bitwise equality. It
also does not apply to the explicit stage (`a=0`), or across retry bases or changed
callback contexts. Only on the corresponding affine space may one infer equal roots.

## Boundaries, context and admissible vectors

On the ordinary packed RHS path `computeUnifiedRHS` first applies
`projectStateBoundaries` to its input (`wrf_sdirk3_tile_unified_impl.cpp:15688`)
and projects its output (`:26724`). Write this as `F_C(Y)=Q f_C(QY)` only for
that path and a fixed boundary policy. `Q` projects symmetric normal velocities;
for the packed periodic-domain case it also copies periodic-X aliases and symmetric-Y
reflection ghosts (`:15346`). These copies/aliases are constraints, not independent
physical degrees of freedom. An admissible perturbation of a constrained state must
preserve the same aliases and boundary constraints; the corresponding tangent is
`Q v` where the projection is linear and fixed. This statement does not establish
that the implementation's entire GMRES space is restricted to that subspace.

The full-halo route instead combines exchanged ghost state with the current interior
and calls `computeUnifiedRHSFullHalo` (`:14072`); it must be audited as its own map,
not silently identified with the packed `Q f Q` path. `U_ref_stage_` is cloned from
`B` on implicit-stage entry (`:13959`) and used by RHS reference-dependent terms
(`:15700`). With `omega_update_ref_per_newton` enabled the callback replaces that
reference with each `U` (`:14128`); a frozen-reference derivative cannot be assumed
in that configuration. Halo exchange mode, ghost/interior combination, geometry,
base fields, options, reference and mutable work/cache state are part of `C`. Cache
or buffer reuse must preserve the same mathematical map and derivative; source
inspection alone does not prove this for all call sequences.

`H` below denotes the Newton halo-zeroing action, distinct from `Q`. For 1-D packed
vectors it multiplies by a constructed halo mask only when initialized and matching;
otherwise it is a no-op (`wrf_sdirk3_newton_solver.cpp:4960`). Each iteration applies
that mask to `K` before evaluating the residual (`:5980`), and zeroes post-GMRES
`dK` (`:10110`). The base may contain stale inter-stage halos unless the applicable
exchange path refreshes them (`:5988`). `H` is not proof of `Q`-admissibility, and
no inference that either operator is the identity on all packed entries is warranted.

## Norm and implemented linear solve

Let `S` be the stage's diagonal scaling and `N` the total packed element count.
With initialized scaling, Newton forms `R_inner=H G_K`, and its convergence scalar is

\[
 \|S^{-1}H G_K\|_2/\sqrt N.
\]

The source freezes the state-derived scale at iteration zero when external physics
scaling is not supplied, with block floors and a shared momentum scale
(`wrf_sdirk3_newton_solver.cpp:6459`, `:6475`, `:6537`); the configured external
scale and fallback paths have their own conditions. `N` includes packed entries,
not merely independent or unmasked degrees of freedom. The source also contains a
last-resort unscaled RMS fallback (`:6593`), although the earlier scale/layout check
requires initialized matching scales (`:5575`). The optional block-max gate adds
a separate convergence condition (`:7130`); the RMS alone is not the whole gate.
This scalar is distinct from unscaled and block-relative diagnostics. For the equivalent state residual the same
metric gives `||S^-1 H G_Y||/sqrt(N)=|a| ||S^-1 H G_K||/sqrt(N)` in exact arithmetic,
provided the same fixed `S` and `H` are used.

For a fixed differentiable `F_C`, the intended physical tendency-coordinate Jacobian
is `A_K v = v - D F_C(Y)[a v]`; the state-coordinate derivative is
`A_Y v = v - a D F_C(Y)[v]`. They are the same linear map only under those fixed-context
and chain-rule assumptions. The source `apply_jacobian` feeds `a*dK` to the selected
JVP and returns `dK-jvp_result` (`:7292`, `:7311`, `:7781`). AD, FD, fallback,
and experimental block-epsilon paths are implementation choices, not a proof that
they all compute the same derivative.

The implemented scaled GMRES map is `S^-1 A_K S`, and its optional scaled
preconditioner is `S^-1 M^-1 S` (`:8370`). The GMRES right-hand side is the **unmasked**
`-S^-1 G_K` (`:8940`); the scaled operator and preconditioner are likewise not wrapped
in `H` there. The Newton convergence norm masks the residual, and the later direction
is halo-zeroed. Therefore this document does not replace the implementation by
`H S^-1 A_K S H` or claim an identical constrained operator. `M` denotes the selected
physical preconditioner, not an exact inverse and not the halo mask.

The production caller routes a non-null right preconditioner to FGMRES, which stores
its actual per-vector preconditioned basis; otherwise it calls GMRES (`:9541`,
`:9649`). Write the already S-scaled system as `A_t x=b_t`, with true residual
`r_t=b_t-A_t x`. With block scaling disabled, the internal relative objective is
`||r_t||/||b_t||`. When `gmres_block_scale`, autograd and a matching valid layout
activate the block path, it instead uses `||D^-1 r_t||/||D^-1 b_t||`, with
`D^-1` built from initial block residual norms (`:1374`, `:2892`, `:3005`).
The FGMRES stage-WRMS objective branch is hard-disabled in this source (`:2912`);
its diagnostic weights do not establish a different active objective. Normalization
has special zero-RHS handling. Internal convergence is not the only exit: budgets
and stagnation can stop the iteration. The final returned success/relative error is
judged with the D-unweighted `||r_t||/||b_t||`, so internal and returned judgments
can differ when D is active (`:4360`, `:4430`). These are relative L2 metrics in
Krylov S-coordinates, not Newton's masked absolute RMS.

Krylov's `zero_halo_regions` returns immediately for tensors with fewer than three
dimensions (`:323`); for the packed 1-D solve its internal and final residual
reductions therefore do not apply Newton's packed halo mask. Passing a halo mask to
the solver must not be read as proof that those reductions use `H`. The exact
interaction of any other helper-level uses of that mask remains a separate audit.

## Remaining proof obligations

- O2/O3: establish, for the selected actual configuration and call sequence, the
  reference/omega, halo, cache and projection context; compare production AD/JVP and
  fixed-context directional differences on valid directions. Identify branches and
  nonsmooth points before asserting the chain rule or operator identity.
- Verify whether the unmasked GMRES system followed by masked updates solves the
  intended constrained equation, including alias, stagger and multi-tile behavior.
- E and T remain open: weighted adjoint/inverse bounds, split/reference consistency,
  stability and forecast evidence are not implied by the identities here.
