# S1b factored endpoint product budget

Local timestamp: 2026-09-27 06:22 JST (+0900)

Implemented in isolated branch `agent/s1b_factored_budget`, based on validated head `c6eb9b5040d1ec837f3a5d00280d6422e44ba3e7`. The test-source change is limited to `tests/test_full_tile_step.cpp`; the existing nonuniform hybrid/map fixture and its timestep/state remain unchanged. Existing stage face/source and boundary receipts remain in the targeted test.

## Identity and predeclared bounds

For each mass point and layer, let `C0=c1h*(MUB+MU0)+c2h`, `T0=300+theta0`, and `W=dx*dy*eta_width/(g*mapfac_m^2)`. The implementation factors the endpoint product difference as `W*(C0*delta_theta + c1h*T0*delta_mu + c1h*delta_mu*delta_theta)` and subtracts the accepted-stage chain rule. It independently assembles the linear endpoint-rounding correction `L` from observed sequential FP32 update residuals and the bilinear correction `B=c1h*W*(delta_mu*delta_theta - dt*sum_s b_s*(delta_mu_s*theta_dot_s + delta_theta_s*mu_dot_s))`, then checks direct `D` against `L+B`.

The FP32 update replay follows the production scalar-multiply/add order. With unit roundoff `u=epsilon_float/2`, each observed update residual is checked against `gamma2*|alpha*f| + gamma1*(|x_before|+|increment32|) + 2*denorm_min`, where `gamma1=u/(1-u)` and `gamma2=2u/(1-2u)`. The FP64 budget is `(gamma64(32)+gamma64(Ark::stages)+gamma64(ny*nz*nx))*factored_scale`; `factored_scale` sums the absolute factored endpoint, stage-chain, linear-rounding, and cross terms. It excludes the large baseline `Q0=M0*T0` products. The mutant holds captured data fixed and drops `B`; the test unconditionally requires its residual to exceed 100 times that budget and the factored identity error to fit within the same budget.

## Evidence

On the original closed fixture, the emitted values were `D=-9712.04`, expanded `D=-9712.04`, identity error `1.21872e-10`, and factored gamma budget `9.55995e-05`. The observed linear FP32 correction was `-9712.09`; `B=0.0420836`. Omitting `B` gave residual `0.0420836`, a separation of `440.207×`, so the closure assertion ran. Every packed-field update residual satisfied its per-element FP32 bound; their reported maxima were `8.38139e-07` and `1.90736e-06` respectively.

Validation: standalone CMake target `test_full_tile_step` built, and after making the 100x mutant gate unconditional, `ctest --test-dir build/sdirk3 -R '^Full_Tile_Step_Adjoint$' --output-on-failure` passed 1/1 in 17.23 s. A direct run before the final gate-only adjustment printed `Full tile step contracts passed` with the same signal values. Graphify was run before and after the edit; the extracted graph path remains `unifiedStep() -> computeUnifiedRHS()`. Graphify is navigation evidence only; the ARK update order was checked in source.

A read-only Red review confirmed the algebraic decomposition and the observed FP32 update bound. It flagged the original conditional mutant gate; this was changed to an unconditional assertion, followed by the passing targeted CTest above.

No production solver file changed. No WRF `em_b_wave` run, RK3 comparison, full WRF rebuild, or MPI run was performed.

SHA-256 at this revision:

- `external/libtorch_wrf/sdirk3/tests/test_full_tile_step.cpp`: `88762e231137de95325cf18c4d5f1789c8f3ad9bfa4e7e3ce356dc2e7d9d3db5`
- `external/libtorch_wrf/sdirk3/wrf_sdirk3_tile_unified_impl.cpp`: `6eb2aa365453c9673c27b3fa8c34091d97c3613027d81c5c0cf138dae37caba1`
- `external/libtorch_wrf/sdirk3/wrf_sdirk3_ark324_composition.h`: `8ebf4d680c79a0103522b68c278f5145e2c2dd1c5007fd17e684014411a2a907`
- Graphify `graph.json`: `1b5547d68884cd2797df10fc03c2850166e80fd52ec41cbd8395dca9128b254c`
