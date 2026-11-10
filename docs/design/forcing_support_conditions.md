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


## Same accepted pair: bounded EOS domain and arithmetic component (2026-11-10)

The follow-up uses main4907880c and native diagnostic source354748d6. The final
Tile difference05aac9ac changes only the old mass-floor comments. A detached
Boolean predicate does not remove the discontinuity at mu_full=0; the positive
branch margin is the applicable differentiability condition.

The chosen next target is exact merit decrease at the same Stage3 accepted pair,
not a new forcing tolerance. If |m_i*−mtilde_i|≤e_i with e0<mtilde0, then
m1*/m0*≤(mtilde1+e1)/(mtilde0−e0). The sufficient strict-decrease budget is
`e0+e1 < 4295394570649/2251799813685248`. Full e_i are still unavailable.

The EOS receives perturbation phi, not total PH. With positive captured rdnw r,
its real expression is D=c1(MUB+mu)+c2 and
alpha=alb−(alb*c1*mu−r*Delta(phi_pert))/D, followed by
p'=p0*(rd*(t+t0)/(p0*alpha))^(cp/cv)−p_base. Positivity of total PH difference
alone does not establish alpha>0 unless the needed hydrostatic identity is proved.
The interval evaluator follows the original perturbation expression instead.

A same-call snapshot of the original accepted trial retains both EOS endpoints
and fixed coefficients. The archived K/R/S_inv/reference hashes and native RMS
match; no extra RHS/JVP/preconditioner is called. This does incur copy/hash/I/O
cost, and the process terminates rather than claiming safe continuation.
For fixed captured B,a,coefficients and the stated source-copy and one-ULP basic
arithmetic/magnitude assumptions, interval bounds establish positive D, alpha,
theta and pow base over the selected segment/coordinate box at all64 levels.
This is a domain condition, not a pow-accuracy or complete smoothness proof.

A separate MPFR256 directed-rounding enclosure compares both native outputs to
the real EOS at **captured rounded inputs**, at all212544 cells per endpoint.
The pressure Linf upper bounds are 5.627079411241238e-11Pa (base) and
4.990436858974591e-11Pa (accepted). Exact binary64 hex encodings accompany
all output bounds. The real cp/cv ratio and power are enclosed; no uniform native
libm accuracy contract is inferred from a symbol name.

The captured alb is a computed inverse-density coefficient, not an assumption
that the original ALB field is exact. Its upstream calculation, state formation,
propagation through the full RHS, and native norm/float conversion errors remain
outside this local component. A pressure-unit error cannot be added directly to
a scaled-RMS budget. Thus this result does not yet certify exact merit decrease,
exact forcing, Stage3 completion, or E/T/A/F. Native exit86 follows the first
accepted Stage3 update; completed stages in logs are [2], completed steps0.

[Evidence and independent replay scope](../evidence/eos_descent_20261110/README.md).


## Base-alb component and widened real domain (2026-11-10 follow-up)

The follow-up to main2c1dcce1 uses no new native execution. For the same354748d6
Stage3 accepted pair, reconstructed PB matches captured p_base bitwise. Base theta
is reconstructed by the source FP32 T_INIT+300 then exact FP64 promotion and
matches the recorded data fingerprint; the original array was not retained, so
this is collision-qualified input evidence. The conditional target treats these
rounded theta/pb and stored float constants as exact, excluding their formation.

For rd=287, cv=717.5, cp=1004.5, the upstream real expression is
alb*=rd/100000*theta_b*(p_b/100000)^(-cv/cp). This explicit100000 must be
kept distinct from the EOS p0. MPFR256 directed intervals bound captured native
alb error at all212544 cells by Linf1.245771593971007e-15 m^3/kg.

For D=c1(MUB+mu)+c2 and beta=c1*MUB+c2, exact input shifts satisfy
Delta(alpha)=(beta/D)Delta(alb) and
Delta(al_pert)=-(c1*mu/D)Delta(alb). Pressure shifts are evaluated with directed
EOS intervals rather than a sampled derivative. Direct composed comparison of
native alpha/p against EOS(alb*) includes upstream alb and native EOS arithmetic;
it is not added again to the previously bounded EOS arithmetic component.
Native al_pert was not retained, so its reported shift is only input sensitivity.

The original64-level state box is widened with per-level Ea and a conservative
|beta/D| bound using the original positive D lower endpoint. Alpha remains
strictly positive at all64 levels; the exact-real power base is recomputed from
the widened interval. Old frozen-alb lower endpoints are not silently reused.
This does not bound a counterfactual floating execution; the previous state-box,
source-copy/basic-rounding and input identity assumptions remain necessary.

Full downstream PGF/buoyancy/mass and other RHS, native al_pert arithmetic,
input formation, scaling/norm/float conversion and e0/e1 remain open. Thus exact
merit decrease and full Stage3/forecast convergence are still uncertified.
The old native source/link/run scope, P1 NO-GO and E/T/A/F status are preserved.
[Input evidence, component bounds and portable replay](../evidence/alb_error_20261110/README.md).


## Recovered native al_pert and first W input component (PR345 follow-up)

For the same FP64 endpoints, source alt=al_pert+alb implies
u_hat−u*=(alpha_hat−alpha*)−(alb_hat−alb*)−eta_add. Reconstructing the original
rounded-operation tube before the final addition bounds both its exact operand
sum and rounded output inside magnitude8. Under the existing source-ordered
one-ULP binary64 conditions, Eadd=2^-50 yields native al_pert Linf upper bounds
3.833649509045476e-15 and3.829031194395618e-15m³/kg. These are conditional
recovery bounds, not direct samples or new MPFR runs. Induced shifts are not added.

The active ImplicitOnly WPGF uses pressure differences; inverse-alt diffusion is
in do_explicit. W interior/top real-algebra input sensitivities are respectively
|g*rdn_k|/m_k*(Ep_k+Ep_{k−1}) and2|g*rdnw_top|/m_top*Ep_top for dry cq1=1.
Using the recorded candidate geometry, positive mass bounds and specified Qw,
source-profile inverse-weight cap and H=I gives a conditional mathematical
weighted component near2.40e-13/2.36e-13. Runtime RDN/g identity and scale-floor
path assumptions are explicit, not measured coefficient/weight claims. This
piece excludes native W arithmetic/coefficient formation and every other RHS
term, as well as native scaling/norm/float-conversion errors.

Native al_pert recovery does not close full residual error. Actual coefficient/
weight evidence, U/V primary and NH4, other RHS and metric remainder still need
bounds before e0+e1 or Stage3 convergence can be certified. No native or RK3 was
rerun; the original accepted-pair scope and historical NO-GO results are preserved.
[Replay and source/data conditions](../evidence/eos_rhs_20261110/README.md).


## Selected W operands observed rather than assumed (PR346 follow-up)

Native source ddc9475 records actual CPU Float32 RDN and gravity at the W consumer,
actual Float32 top RDNW, canonical/dry and positive finite operand ranges, and
min/max/count/dtype of the actual cloned rw inverse-metric block. In both selected
calls, values match the prior candidates; rw inverse weights have min=max1. All
old pair/context and16 EOS SHA identities match. These observations close the
previous RDN/g/top and weight assumptions for this pair only, not future solves.

PressureEosContext layout changed, so all five header consumers were rebuilt with
matching macros. The initial generic-lambda compile failure was preserved; explicit
parameter types fixed it without numerical changes. Helper and single-archive
map validation passed, and the one registered h600 diagnostic exited86 after the
first accepted Stage3 update, with zero completed steps.

The new sensitivity model fixes captured native alpha_w (canonical mass/map, not
EOS inverse density), msfty and velocity_mass operands. It uses the positive
product of alpha_w and map minima, rather than assuming pointwise level_mass_w
identity on aliases. Positive velocity mass cancels only in this real intermediate
model. Native ratio/arithmetic and coefficient formation versus full G* remain
separate remainder terms. Observed inverse-weight max replaces the floor proxy;
mathematical norm bounding does not certify native norm/float conversion.

The weighted pressure-input components are approximately2.4011311208232075e-13
and2.364582296508406e-13, with exact Fractions authoritative. Full native W error,
U/V primary/NH4/other RHS, formation, norm error and e0/e1 are still open. Thus
exact merit decrease, Stage3 convergence, E/T/A/F and forecast claims remain open.
[Replay, source and data contract](../evidence/w_runtime_20261110/README.md).
