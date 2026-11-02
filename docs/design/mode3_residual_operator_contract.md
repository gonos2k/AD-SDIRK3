# Mode 3 implicit-stage residual and operator contract

Source revision: `b98a0e81df0e3ca7fc59499d9246aa42f03038ab`. This contract
primarily covers the non-split-explicit ARK324 mode 3 implicit-stage path. Line
numbers are navigation aids, not numerical evidence. It defines the residual and
conditional algebra; it does not prove O2/O3, adjoint, spatial equivalence, stability,
or convergence.

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

Let `mathcal F` denote the actual callback on a specified call sequence, including
its reference and other context, and `Y=B+aK`. Newton forms the packed algebraic
residual

\[
 G_K(K;B)=K-\mathcal F(B+aK).
\]

The first Newton argument already is `B`, not the original timestep state
(`wrf_sdirk3_newton_solver.cpp:5518`, `:6030`, `:6404`). For `a != 0`, define

\[
 G_Y(Y;B)=Y-B-a\mathcal F(Y)=a\,G_K((Y-B)/a;B).
\]

This identity is algebraic over the same `B`, `a`, callback and consistent context rules. For the
implemented finite-precision expressions it does not imply bitwise equality. It
also does not apply to the explicit stage (`a=0`), or across retry bases or changed
callback contexts. Only on the corresponding affine space may one infer equal roots.

## Boundaries, context and admissible vectors

On the ordinary packed RHS path `computeUnifiedRHS` first applies
`projectStateBoundaries` to its input (`wrf_sdirk3_tile_unified_impl.cpp:15688`)
and projects its output (`:26724`). With a fixed boundary policy, write its
interior computation as `f(X; C)` and distinguish:

- Fixed, Y-independent context: `F_C(Y)=Q f(QY; C)`.
- Updated context: `mathcal F(Y)=Q f(QY; C(Y))`, only if the other stateful
  inputs behave as a well-defined function under the considered call sequence.

The second map is not generally `Q f_C(QY)` for one fixed C. `Q` projects symmetric normal velocities;
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
reference with each unprojected `U` (`:14128`) before `computeUnifiedRHS` projects
its input; the reference extraction reads `U_ref_stage_` separately (`:15688`,
`:15705`). Thus its reference dependence is not necessarily a function of `QY`
alone. A frozen-reference derivative cannot be assumed in that configuration. Halo exchange mode, ghost/interior combination, geometry,
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

Where the maps are differentiable and `Q` is fixed and linear, a fixed context
has `D F_C(Y)[v]=Q D_1 f(QY;C)[Qv]`. For the updated-context map the chain rule is

\[
D\mathcal F(Y)[v]=Q\{D_1 f(QY;C(Y))[Qv]
 +D_2 f(QY;C(Y))[DC(Y)[v]]\}.
\]

The second term cannot be dropped without evidence. This formula is conditional on
`C(Y)` being a differentiable function rather than history-dependent mutable state,
and on the RHS actually admitting that decomposition; neither is established here.
For whichever differentiable actual `mathcal F` is justified, the **exact** residual
Jacobians are `A_K^* v = v - D mathcal F(Y)[a v]` and
`A_Y^* v = v - a D mathcal F(Y)[v]`. Their equality is conditional on a consistent
context and the linearity of the derivative, not a statement about the selected JVP.

The implementation feeds `a*v` to its selected JVP and returns
`Ahat_K(v) = v - Jhat(Y, a*v)` (`:7292`, `:7310`, `:7783`). The scaled map passed
to GMRES is `Ahat_t(z)=S^-1 Ahat_K(S z)` (`:8382`), with optional scaled
preconditioner `S^-1 M^-1 S`. It need not equal `S^-1 A_K^* S`: finite-difference
steps, fallback, mutable callback context, branch changes, and experimental
block-epsilon paths can create a gap. At finite epsilon, a directional difference
need not be additive or homogeneous, so neither `Jhat` nor `Ahat_K` is assumed
linear. Calling a value `b-Ahat_t(z)` a sampled residual does not by itself make it
the residual of one fixed linear operator.

For a nonzero `b` and a **single common norm**, a candidate `z` obeys the pointwise
triangle bound

\[
\frac{\|b-A_t^*z\|}{\|b\|}\le
\frac{\|b-\widehat A_t(z)\|}{\|b\|}+
\frac{\|\widehat A_t(z)-A_t^*z\|}{\|b\|},
\qquad A_t^*=S^{-1}A_K^*S.
\]

This bound does not require `Ahat_t` to be linear; it does require both terms to
be evaluated for the same `z`, base, context and norm. An observed small Krylov
residual therefore bounds the exact forcing residual only when the directional
operator gap is also controlled. D-weighted and D-unweighted ratios, or masked
Newton RMS and unmasked Krylov norms, cannot be substituted without a justified
norm conversion.

The GMRES right-hand side is the **unmasked**
`-S^-1 G_K` (`:8940`); the scaled operator and preconditioner are likewise not wrapped
in `H` there. The Newton convergence norm masks the residual, and the later direction
is halo-zeroed. Therefore this document does not replace the implementation by
`H S^-1 Ahat_K S H` or claim an identical constrained operator. `M` denotes the selected
physical preconditioner, not an exact inverse and not the halo mask.

The production caller routes a non-null right preconditioner to FGMRES, which stores
its actual per-vector preconditioned basis; otherwise it calls GMRES (`:9541`,
`:9649`). Let `b_t=-S^-1 G_K` and let the explicit Krylov residual evaluation be
`r_t=b_t-Ahat_t(x)`. With block scaling disabled, the internal relative objective is
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
