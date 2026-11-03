# Bounded support for pointwise forcing and an accepted Stage 3 update

Numerical source baseline: `3a82d601` (PR342). The receipt-only experiment is
`bc0f6646`. This ledger complements the [operator contract](mode3_residual_operator_contract.md);
it does not replace its definitions or certify the full native Jacobian.

## Scope and source conditions

The examined profile is dry mode3, non-split-explicit ARK324, one rank/tile, FP64
state/carry, selected forward AD, no reference update, no prepared-JVP reuse and
identity Newton halo action. Effective `mass_coordinate_mode=1` selects WRFParity;
raw deprecated omega flags are not its authority.

- `WRFMassFlux mass_flux_memo` is local to each `computeUnifiedRHS` call and starts
  empty. `diagnose_wrf_mass_flux` rebuilds it from that call's projected state.
  It is not a cross-call frozen-omega cache. The inspected stencil/continuity/
  theta/phi portions retain tensor dependencies; this is not a complete graph proof.
- EOS carries current ph/t/mu through tensor expressions. Its detached mass-floor
  predicate is piecewise: away from its switch, the selected branch derivative is
  valid; at a switch, differentiability cannot be assumed.
- Current EOS pressure is published to `p_pert_` before the inspected V-gradient
  reads. This establishes the named overwrite order, not all transitive cache purity.
- Authoritative `dyn_em/module_implicit_sdirk3.F` configures then loads environment,
  sets WRF dynamics flags, then creates tiles. The tile constructor snapshots NH.
  The earlier `non_hydrostatic=off` print precedes the WRF override; the later setter
  is true before the constructor. The examined nonhydrostatic branch is therefore on.
- W damping and Rayleigh are off in the recorded configuration. A true
  `implicit_wdamp` flag alone does not activate an otherwise disabled damping term.
  Some unlogged switches remain source defaults rather than measured effective values.

Source-pinned audit receipts provide exact functions/locations and remaining gaps.
Fortran authority and indirect callbacks are manually checked graph gaps.

## What the retained point data establishes

The retained Stage2 evaluation and reference arrays were reused; no epsilon scan
was repeated. Reconstructed base fields/coefficient arrays match recorded fingerprints.
The projected mass plus base mass has exact-rational minimum `89073.03125 Pa` and no
nonpositive cell. Conservative point lower bounds are also positive for theta+300
(`258.83935546874994 K`), total geopotential layer difference
(`2110.515136718749 m^2/s^2`) and hybrid mass.

These are point/domain checks of saved binary inputs. They do not bound a neighboring
segment, inverse density, all clamps, libm error, or the full RHS. In particular,
matching a finite fingerprint is evidence tied to the recorded source/input contract,
not a symbolic proof of all possible private state.

## The still-open forcing budget

Use the same base, direction and norm as the operator contract. The Stage2 candidate
has recorded implemented ratio `1.6527321828e-8`. For an exact residual versus its
stored approximation, the user-reviewed sufficient condition has the form

\[
 \tau_\epsilon+(1+t)\rho+\nu\le t-\widehat\eta-\delta_\epsilon,
 \qquad \rho<1.
\]

Here tau bounds the FD-to-exact derivative error; rho bounds reference residual
evaluation error; nu represents an independently justified additional norm/receipt
rounding budget. It cannot be silently set to zero. For epsilon=0.01 the available
right side is about `8.32748e-8` at t≈1e-7. No bounds on those full-RHS errors were
obtained. AD selection, repeated primal equality and small sampled FD gaps do not
supply them. A theorem with a weaker forcing ceiling below one is a different target;
no solver tolerance or historical criterion has been changed to obtain a pass.

## Source-supported boundary part

For this packed copy/reflection map, output projection gives F(Y) in range Q.
If Q is fixed linear and F differentiable in the relevant neighborhood,
`(I-Q)DF=0`, `(I-Q)A*v=(I-Q)v` and `(I-Q)G=(I-Q)K`.
The sampled `Qp=p` removes only the complement of A*p. The complement of the full
linear residual vanishes only with the additional `QK=K` condition.

Do not set a projection norm to one by orthogonality. With positive diagonal S,
P=S^-1QS is idempotent but generally oblique. An alias group retained at j and
copied/sign-reflected to i has complementary rows
`e_i^T - sigma_i*(S_j/S_i)*e_j^T`. With at least one alias its largest singular value
is `sqrt(1+sum_i(S_j/S_i)^2)`; a singleton retained group has zero complement, while
a fixed-zero slot contributes an identity row. A valid global upper bound takes
the maximum over these groups and one. Under block-constant S and the standard
40×80 packed topology, the maximum multiplicity is four, giving an upper bound two.
This conditional source bound does not prove full state admissibility or energy stability.

## Conditional mu-floor tube bound from the actual accepted pair

The new terminal receipt reads already-computed Stage3 base and accepted K/R pairs;
it performs no RHS/JVP/preconditioner replay. Let K0,K1 be the stored finite FP64
endpoints, B the fixed stage base and a the stored positive coefficient. The mu part
of Q is a copy map with infinity norm one. For K(s)=(1-s)K0+sK1, 0≤s≤1,

\[
 \|K(s)-K0\|_{\infty,\mu}\le k_0+k_1,\qquad
 k_i=\|K_i\|_{\infty,\mu}.
\]

Let m0 be `min(captured_projected_mu)+min(MUB)`, using exact binary64 extrema.
The captured mu already includes rounding in a*K0 then +B. For the stated binary64
basic-operation contract, if

\[
 \|B\|_{\infty,\mu}+a\max(k_0,k_1)+\|MUB\|_\infty+1 < 2^{39},
\]

all relevant intermediate magnitudes remain below 2^40. One full ULP per multiply
or addition is then at most 2^-12 Pa. Two base-construction roundings plus three
later construction/base-mass additions cost at most 5/4096 Pa, including a much
looser allowance than possible underflow. The predefined 1 Pa allowance exceeds this.
An exact-rational lower bound is therefore

\[
 m_{\rm lower}=m0-a(k_0+k_1)-1\ {\rm Pa}.
\]

Positivity proves only that the `mu_full<=0` floor cannot activate on the ideal real
segment and on binary64 inputs in the associated endpoint coordinate box, under
fixed B/MUB/a, the source Q_mu map and the stated rounding/overflow assumptions.
The replacement value 1000 is not the trigger. This is not a libm, full RHS,
Jacobian/FD-error or forcing certificate. It does not cover other nonlinear branches.
`mass_tube_bound.py` uses exact fractions and refuses a failed magnitude/positivity
condition; its allowance was defined before the native result.

The observed native merit decrease is also a computed-value observation. It supplies
neither an inverse bound M nor a Jacobian Lipschitz constant L, and cannot be used
to infer their upper bounds from one step. Full residual evaluation/norm error,
other state/direction/branch conditions, E/T/A/F and forecast quality remain open.
