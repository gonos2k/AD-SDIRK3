# Option-2 U physical RHS at fixed mass

Local timestamp: 2026-09-27 05:54:05 JST (+0900)

## Corrected basis

The earlier `(202609270348)_option2_u_full_rhs_normalization_wrf_blocker.md`
compared C++'s physical velocity rate with the intermediate Fortran
`rk_addtend_dry` `ru_tend`, and proposed an incorrect `M/msfuy` conversion. It
is retracted. The intermediate coupled tendency is not the physical U rate after
the WRF small step.

For fixed mass, WRF's source chain is:

```text
u_c,prep = (L_s*u1 - L_t*u2) / msfuy
ru_tend += D / msfuy                 # rk_addtend_dry, for diffusion D=H+V
u_c,advance += dt * ru_tend          # advance_uv
u2,finish = (msfuy*u_c + u_save*L_t) / L_s
```

Subtracting diffusion OFF from ON cancels the saved-state terms. The physical
U rate is therefore `D/L_s`, where `L_s=c1h*muus+c2h`; the map factor cancels.
This fixture holds mass fixed, so `L_s=L_t`. The candidate scales option-2
stress U H and V increments by `velocity_mass_u/L_s` before the coupled
accumulator's final `/velocity_mass_u` conversion. Option-1 and option-2 simple
diffusion retain their previous scale. The implementation builds `L` from the
current RHS mass; equivalence to WRF's `muus` when later small-step states have
different `L_s` and `L_t` remains open.

The Fortran driver compiles the extracted deformation, U horizontal/vertical
diffusion, and `rk_addtend_dry` routines. Its `SMALLSTEP_LU` receipt is manually
evaluated in the driver as `c1rk*mut+c2rk` from the source formula; the compiled
driver does **not** call `small_step_prep`, `advance_uv`, or `small_step_finish`.
Their exact source sections are hashed for provenance. Thus `D/L_s` is a
source-equivalent fixed-mass transform, not compiled full-small-step parity. In
the fixture mass is constant, `muus=muu=mut=80000`, so `L_s=L_t`. This contract
is not complete Fortran small-step or WRF boundary parity.

## Same-state U contract

The C++ side calls the actual private `computeUnifiedRHS` through `ActualRhsTag`
for diffusion ON and OFF. The compiled Fortran operator driver has nonzero U/V/W/T,
terrain in PH, dry `km_opt=1`, complete single tile, `rho=1`, zero
`MU_ONminusOFF`, and fixed distinct K values `khdif=0.6666667`,
`kvdif=1.3333334`, `xkhh=2`, `xkhv=4`. Layer mass is nonuniform through c1/c2;
the maps are constant at 1 or 1.25. All 112 owned U keys `(j=1..4,k=0..3,i=1..7)`
and all four Fortran `SMALLSTEP_LU` levels are required; each C++ U/H/V output
must contain all 216 fixture cells.

| Map | Physical signal `max|D/L_s|` | Corrected C++ max error | Old `H/alpha+V/M` residual | Wrong `D/map` residual | Raw H/V max error |
|---:|---:|---:|---:|---:|---:|
| 1.0 | `1.27703e-6` | `1.30664e-11` | `2.33960e-7` | `0.117486` | `8.27e-9` / `1.49e-8` |
| 1.25 | `1.29949e-6` | `1.19807e-11` | `2.47609e-7` | `0.0956411` | `1.28e-8` / `2.24e-8` |

The absolute FP32 budget is `1e-9`. Both incorrect formulas fail by more than
32 times that budget. The Fortran raw H/V rates match the C++ raw helpers before
normalization. The equation-level residuals of the old C++ formula are the
component-mass and map differences shown above; they are small relative to the
intermediate coupled tendency but material relative to the physical U signal.

For example, at map 1, `(j,k,i)=(4,0,1)`, `M_u=80000`, `L_s=60000`, and the
Fortran raw components are `H=0.00432489998639`, `V=0.0561503879726`. The old
C++ formula gives `H/L + V/M = 7.73961e-7`; Fortran's post-small-step physical
rate is `(H+V)/L_s = 1.00792e-6`. The corrected candidate matches that source
rate within the declared budget. At map 1.25, the old path's maximum physical
residual is `2.47609e-7`, while the prior M/map patch would err by `9.5641e-2`.

The Fortran fixture analytically fills periodic-X/symmetric-Y metric halos; it
does not call `set_physical_bc3d`. D12 edge differences at `(j=2,k=0,i=0)` are
separate halo receipts (`4.40e-5` at map 1, `5.50e-5` at map 1.25), outside the
owned U inventory and not used as parity evidence. The fixture also pins density
to one. Full physical-boundary, live WRF density/halo, later-stage `L_s/L_t`,
V/W, MPI, supplied/spatial K, and complete-step adjoint parity remain open.

## WRF comparison

The candidate Make archive was linked first into a new executable, with both
library lookup sites selecting that archive. The link reused unchanged WRF
Fortran objects and `libwrflib.a` from clean #246 worktree HEAD
`ca051d47c1a1ad379dbae8d3ea9e967647254595`. It is a candidate-first relink, not
a clean full WRF build of this candidate. It uses the preserved one-rank,
60-second `em_b_wave` input, initial state, and diagnostics.

| Case | Result |
|---|---|
| PC2, `khdif=1000`, `kvdif=10`, 60 s | `SUCCESS COMPLETE WRF`; candidate output is byte-identical to clean #246 PC2 |
| RK3, same input and coefficients, 60 s | `SUCCESS COMPLETE WRF`; candidate output is byte-identical to clean #246 RK3 |
| PC2, K=0 control | Candidate output is byte-identical to a clean #246 K=0 control |

Candidate PC2 `Timing for main` is `0.47486 s`; candidate RK3 is `0.08036 s`.
The clean #246 readings are `0.47756 s` and `0.07879 s`. These are one-step
internal timings, not a performance claim. No external wall time was captured.
The byte-identical PC2 output shows regression neutrality on this standard
`em_b_wave` setup; it does not demonstrate material activation of the nonunit
map/layer-mass correction.

Both files contain 174 floating variables and 378,352 floating values, all
finite. The comparison CSV contains 187 numeric time-dependent variables,
including those 174 floating variables and 13 integer variables. It has
per-field finite counts, RMS, and
maximum differences against the matching clean outputs and between PC2/RK3. The
two clean comparisons are exactly zero. Selected endpoint PC2-minus-RK3 values
from the same output files are:

| Field | RMS | Maximum absolute |
|---|---:|---:|
| U (m/s) | `0.00209012` | `0.0106077` |
| V (m/s) | `2.21139e-5` | `9.93933e-5` |
| W (m/s) | `0.000665487` | `0.00167457` |
| PH (m²/s²) | `0.306797` | `0.941406` |
| T (K) | `0.0103998` | `0.0304565` |
| P (Pa) | `0.0398132` | `0.131130` |
| MU (Pa) | `0.0415027` | `0.144721` |

## Validation and provenance

Full CTest passed 106/106 at `-j2`; the exact pinned inventory comparison
passed. Focused tests passed 4/4: `Scalar_Diffusion_Contract`, existing
`Option2_Momentum_Geometry_Source_Parity`, and new map-1/map-1.25 physical-U
contracts. Make manifest and exact 24-member archive checks passed.

| Artifact | SHA-256 |
|---|---|
| Candidate source base | `9147bbdca8e34059847c908d6e633d9053dd13a4` |
| Development HEAD before final M/L correction | `d6a807906f0bceba9c287988f1985daad905454c` |
| Candidate U implementation | `cbf9dcbbbcb4ae7be00b0da5680d96020df141fd56613e3b3b053c3ec75f22f3` |
| Stage-history diagnostic header | `db6e329be69bd6a7ccb662071c0c85ffac31d511dd2ac0946db4fb3ae8c5a079` |
| C++ full-RHS fixture / Python oracle | `a32e10ef25ebaa3dd0ab5d56acc54ed1f8f827b7ed7618bb28e5ae6ac5d20bcd` / `c0034b324f19030fa3dcbe5ccabf4bc804c3dbc2bca89b358fb60c23b2117aac` |
| `module_diffusion_em.F` / `module_em.F` | `c044c533e5e1e9d168418f2b72feba62964bee6a0f55e211c2522ffceaa6b7ea` / `fb424486dbf9c903f77da6a15baf35e6772847e786b18d0297139eb48f0d586e` |
| Source-extracted operator bundle / small-step source sections | `6f365e7dcb5c3d55324476ea8210e8fe70b15236bdff5e4df05ffd273ef49a14` / `e3b87d6bf9bd28cbdf5b863a86fc79b6af6096758377bdfbb95d690723294522` |
| Candidate Make archive / WRF executable | `631dd504f26776e68911c60a67dc3d7721a3f6914ce3ad9fca189f7b63d18bbe` / `40d36798ad130dc352808806f7e3265fb736940edd85b87c843475e9fab99bcd` |
| Candidate link command / argv | `53eaf5e6cf2aae19de93fa20ddaee45835f2e7d93cb03c95a5841ef2f66376de` / `b5bf92ad7a8895ec28a28bc0d8ca06080ee046ba12e1a009deee88e5349259a9` |
| Clean #246 `libwrflib.a` / `module_diffusion_em.o` / `module_em.o` | `23fc8f9fec05eb2dd3d64f0200ebb7fb023df9e345088b824b65ffc8e5c13d6e` / `63bc5663cd981b3fa9a8239186007d6fd343b027e7b81cb5788e114fbcd97e78` / `8f04898cc46056d0aaf1c36664154c189bb41ad999c15a519da26bc6152adf7f` |
| Full CTest log / focused log | `e7d588e8f154faaf89d1643df81b9af4b979dcbb5334cb40db997da6b965e067` / `5bf8620a435f2d86b4c087ce504cadd520704adb705b41ae17b48cc117e53089` |
| Make archive log / field delta CSV | `ed0065a1f5da565cf5769c736f5fe3b9a140baff690f8bfae3cfebab37cde0d6` / `5b9d08db56a8e94710d5618ce9261f8585f04bbf71b0d1ad184aaacb34e94640` |
| Graphify graph | `a53ca97cdc7a643ec8f825ef0854bd3aa825f04ce30dc79895460a1e6be620e6` |
| Map-1 / map-1.25 final source-equation logs | `4b8f3d9b68cc6490d2b7b7b39c270257a24c19de072d086c7b4dfefd2cc54d9e` / `ddc14fbd30617ade3368dbcab38102616ad1c2f896a705bbfb9f9428b1af0bab` |
| Unmodified #246 baseline physical-basis probe logs | `504ab98dbb650ca240b324671976cd92947b6244f7395f6a679b5047787ceb94` / `306c9eda49e94ebbbc699261c7354d6c8783c5ea6c73356f023942aa8da606ab` |
| PC2 output candidate / clean #246 | `1a4d48818597764938777d70290bd9a847a56d8003ece43b1fd07eec5649b144` / same |
| RK3 output candidate / clean #246 | `bcb32fdddb10fa2756e7bc88cac2c9da05664ed34cebac05ae68d55b00f5c336` / same |
| K=0 PC2 output candidate / clean control | `359737a47eac4b0fb7c4031dc216d32e165fc0a16f2438492a7ee064f3c0cd77` / same |

All artifacts are retained locally under `.validation/fullrhs_probe/`. The
small-step test oracle is fixed-mass/stage-1 equivalent only. Later-stage
`L_s/L_t` differences, materially nonunit WRF-map activation, full physical
boundary parity, and V/W/MPI coverage still require separate evidence.
