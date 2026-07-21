# `ph_tend` dimensional contract and corrected dt=600 interpretation

Date: 2026-07-21

## Correction

The raw `ph_tend` values emitted by the split-explicit diagnostic are **not**
physical geopotential tendencies `d(phi)/dt`. WRF's `rhs_ph` computes the
mass/map-factor-coupled quantity

```text
ph_tend = (mu / msfty) * d(phi)/dt
```

and `advance_w` decouples it while constructing the small-step geopotential RHS:

```text
rhs_acc          = dts * (ph_tend + old-w arm - vertical-advection arm)
rhs_physical     = ph_previous + msfty * rhs_acc / (c1f*mut + c2f)
ph_new           = rhs_physical
                 + msfty * 0.5*dts*g*(1+epssm)*w_new/(c1f*muts+c2f)
```

The authoritative WRF locations are:

- `dyn_em/module_big_step_utilities_em.F`, `rhs_ph`: comments immediately above
  the geopotential tendency state that it is `(mu/my) d(phi)/dt` and is
  decoupled in `advance_w`.
- `dyn_em/module_small_step_em.F`, `advance_w`: the first RHS pass multiplies
  `ph_tend` by `dts`; the second pass divides by `c1f*mut+c2f` and multiplies by
  `msfty`; the final update adds the new-`w` arm divided by the updated mass.
- `wrf_sdirk3_acoustic_substep.cpp`, `advance_w`: mirrors those same two passes.

For the idealized unit-map-factor path and column mass near `9e4`, a raw
`ph_tend=1732` corresponds to

```text
physical d(phi)/dt ~= 1732 / 90000 ~= 1.92e-2
```

not `1732`. With `dts=200`, the isolated direct increment is about `3.85`, not
`346400`. Therefore the earlier diagnostic product `ph_tend*dts*N` was
 dimensionally invalid because it omitted the mass decoupling.

## Standing executable contract

`test_acoustic_substep.cpp` now isolates the frozen-`ph_tend` arm of the real
`advance_w` implementation (`g=0`, `ww=0`, `w=0`, identity Thomas factors) and
checks:

1. `ph_tend = mass*q` produces `delta_phi = dts*q` on active full levels;
2. doubling both mass and coupled `ph_tend` leaves `delta_phi` unchanged;
3. doubling physical `q` doubles `delta_phi`;
4. the isolated arm does not leak into `w`.

The case deliberately uses `q=0.02`, `mass=90000`, hence a raw coupled
`ph_tend=1800`. This pins the exact class of unit error that a blind
"divide/multiply `ph_tend`" patch would introduce.

## Revised next measurement

No numerical `ph_tend` scaling edit is justified by the raw magnitude alone.
The next live dt=600 diagnostic must report quantities in the physical metric:

```text
ph_tend_coupled
ph_tend_physical = msfty * ph_tend_coupled / (c1f*mut+c2f)
dphi_frozen       = dts * ph_tend_physical
```

and decompose the actual `advance_w` geopotential update into:

1. previous `ph`;
2. decoupled frozen `ph_tend` contribution;
3. `ww*d(phi)/deta` vertical-advection contribution;
4. old-`w` off-centering contribution;
5. final new-`w` contribution.

Each term should be reported with max and per-active-DOF RMS, then compared with
WRF dyn_em parity output at the same stage/substep. Only that decomposition can
distinguish a genuine `rhs_ph`/coupling defect from a large but correctly
mass-coupled raw tendency.
