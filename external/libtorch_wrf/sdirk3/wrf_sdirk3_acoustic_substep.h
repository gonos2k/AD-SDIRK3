// wrf_sdirk3_acoustic_substep.h — differentiable WRF split-explicit acoustic sub-step (Inc 5)
//
// Clean, one-to-one port of module_small_step_em.F's acoustic operators, kept OUT of the God
// file for clarity (skill: mirror the reference, keep it simple). Operates on the small-step
// ACOUSTIC PERTURBATION state (reset ~0 each RK stage by small_step_prep, accumulated across N
// substeps), with the full-stage-state coefficient c2a and the calc_coef_w factorization
// (a/alpha/gamma) supplied fixed. Reuses the Inc 2/2b-validated calc_coef_w + Thomas.
//
// All tensor ops are out-of-place / index_put_ (no .item() on the compute path) so the whole
// sub-step is autograd-safe for the eventual JVP/VJP (Inc 7). Layout is the project's {nj,nk,ni}.
//
// STATUS: Inc 5 build in progress. Validation = the assembled sub-step vs a dyn_em
// [PARITY substep] dump (retires the buoyancy + horizontal / full-scheme risk).
#pragma once
#include <torch/torch.h>

namespace wrf { namespace sdirk3 { namespace acoustic {

// Small-step acoustic PERTURBATION prognostics + diagnostics. Perturbations are the coupled
// deviations produced by small_step_prep (module_small_step_em.F:238-279); they start ~0 at
// rk_step 1 (u_1=u_2) and accumulate across the substep loop.
struct State {
    torch::Tensor u;    // u' coupled  {ny, nz,   nx_u}
    torch::Tensor v;    // v' coupled  {ny, nz,   nx_v?}   (nx for v; ny stagger handled by caller)
    torch::Tensor w;    // w' coupled  {ny, nz_w, nx}
    torch::Tensor ph;   // ph' geopotential perturbation {ny, nz_w, nx}
    torch::Tensor t;    // theta'' coupled {ny, nz, nx}
    torch::Tensor mu;   // mu' column-mass perturbation {ny, nx}
    torch::Tensor p;    // p'  acoustic pressure perturbation (calc_p_rho) {ny, nz, nx}
    torch::Tensor al;   // al' inverse-density perturbation (calc_p_rho)   {ny, nz, nx}
    torch::Tensor ww;   // omega (vertical mass flux) {ny, nz_w, nx}
    torch::Tensor pm1;  // previous-substep p' for divergence damping {ny, nz, nx}
    // time-averaged intermediates produced by advance_mu_t, consumed by advance_w's buoyancy/RHS
    torch::Tensor t_2ave;  // time-avg coupled theta {ny, nz, nx}  (advance_mu_t / advance_w :1314-1317)
    torch::Tensor muave;   // time-avg mass perturbation {ny, nx}   (advance_mu_t :1107)
    torch::Tensor muts;    // full column mass mut+mu' {ny, nx}     (advance_mu_t :1106; ph update denom)
    torch::Tensor mudf;    // mass divergence-damping field {ny, nx} (advance_mu_t :1105; advance_uv div damp)
};

// Fixed-per-RK-stage coefficients + metrics + FROZEN slow tendencies. c2a and a/alpha/gamma come
// from small_step_prep + calc_coef_w (once per stage; recomputed only when dts changes). The
// *_tend fields are the slow (large-timestep) tendencies, held constant through the substeps.
struct Const {
    // thermodynamic / factorization
    torch::Tensor c2a;                 // sound-speed stiffness {ny, nz, nx}  (full stage state)
    torch::Tensor alt;                 // full inverse density  {ny, nz, nx}
    torch::Tensor pb;                  // base-state pressure   {ny, nz, nx}  (advance_uv al'*d(pb) term)
    torch::Tensor a, alpha, gamma;     // calc_coef_w Thomas factors {ny, nz_w, nx}
    // masses
    torch::Tensor mut, muts;           // {ny, nx}  full column mass (base, base+mu')
    torch::Tensor muu, muv;            // {ny, nx}  u/v-point masses
    // metrics (per-level 1-D)
    torch::Tensor rdn, rdnw, dnw;      // {nz}  (dnw = full-level thickness for column integrals)
    torch::Tensor c1h, c2h, c1f, c2f;  // {nz} / {nz_w}
    torch::Tensor fnm, fnp;            // {nz} vertical interpolation weights
    // reference (large-timestep) fields for the mass/theta fluxes (advance_mu_t)
    torch::Tensor u_1, v_1;            // uncoupled reference velocities {ny,nz,nx_u/v}
    torch::Tensor t_1;                 // reference theta {ny,nz,nx}
    torch::Tensor ww_1;               // large-timestep omega {ny,nz_w,nx}
    float rdx = 0, rdy = 0, dts = 0, g = 9.81f, epssm = 0.1f, cf1 = 0, cf2 = 0, cf3 = 0;
    float emdiv = 0, smdiv = 0, t0 = 300.0f;   // t0 = base reference potential temperature (calc_p_rho)
    // frozen slow tendencies
    torch::Tensor ru_tend, rv_tend, rw_tend, ft, mu_tend;
    torch::Tensor ph_tend;             // {ny,nz_w,nx} frozen geopotential tendency (advance_w rhs :1318)
};

// --- wiring helpers (Inc 6) ---
// RK3 acoustic schedule (solve_em.F:841-853): per stage returns (dts, number_of_small_timesteps).
// Invariant dts*n_sub == the stage fraction of dt (stage1=dt/3, stage2=dt/2, stage3=dt). Stage 1 is a
// single acoustic step of dt/3; stages 2/3 sub-cycle at dts=dt/num_sound_steps. num_sound_steps even, >=4.
struct AcousticSchedule { float dts; int n_sub; };
AcousticSchedule acoustic_schedule(int rk_step, float dt, int num_sound_steps);

// staggered column-mass averages (module_small_step_em.F:200-207) ---
// u-point mass = 0.5*(mu[i]+mu[i-1]) over x; v-point mass = 0.5*(mu[j]+mu[j-1]) over y. Column mass
// mu is {ny,nx}; returns {ny,nx+1} / {ny+1,nx}. Edges use PERIODIC wrap (em_b_wave x-channel); a
// non-periodic y-boundary must overwrite the edge rows via the halo at the call site.
torch::Tensor mass_to_upoint(const torch::Tensor& mu_col);   // {ny,nx} -> {ny,nx+1}
torch::Tensor mass_to_vpoint(const torch::Tensor& mu_col);   // {ny,nx} -> {ny+1,nx}

// --- small_step_prep entry coupling (module_small_step_em.F:238-279) ---
// The two RK-stage states (uncoupled full fields) + masses + metrics. u_2/v_2/w_2/t_2/ph_2 are the
// CURRENT stage estimate; u_1/... are the step-start (time n). msf* are map-scale factors (=1 for the
// idealized em_b_wave). Produces the coupled acoustic PERTURBATION State + the slow-ref Saves.
struct PrepInput {
    torch::Tensor u_1, u_2, v_1, v_2, w_1, w_2, t_1, t_2, ph_1, ph_2, ww, mu_1, mu_2;
    torch::Tensor muus, muu, muvs, muv, muts, mut;   // updated/base masses {ny,nx*}
    torch::Tensor c1h, c2h, c1f, c2f;                // {nz}/{nz_w}
    torch::Tensor msfuy, msfvx_inv, msfty;           // map factors {ny,nx*} (=1 idealized)
};
struct Saves { torch::Tensor u, v, w, t, ph, ww, mu; };  // slow references, for small_step_finish
struct FinishOutput { torch::Tensor u, v, w, ph, t, mu, ww; };  // full UNCOUPLED fields (new time)

// Couples the RK-stage state into acoustic perturbations and saves the slow refs. Perturbations start
// ~0 at rk_step 1 (u_1==u_2, muus==muu). Returned State has u/v/w/ph/t/mu/ww set + muts + diagnostics
// (p/al/pm1/t_2ave/muave/mudf) zero-init'd; the caller runs calc_p_rho(step=0) to fill p/al/pm1 before
// the substep loop (solve_em.F:1352).
State small_step_prep(const PrepInput& in, Saves& saves);

// Reconstructs the full UNCOUPLED prognostic fields from the final perturbation State
// (module_small_step_em.F:379-432) — the inverse of small_step_prep. On the last RK stage
// (rk_step==rk_order) theta gets the h_diabatic term; otherwise the simple decouple. Invertibility:
// finish(prep(x), no substeps) recovers the reference (u_1/v_1/w_1/ph_1). Reuses `in`'s masses/metrics.
FinishOutput small_step_finish(const State& s, const Saves& saves, const PrepInput& in,
                               int rk_step, int rk_order, int n_small, float dts,
                               const torch::Tensor& h_diabatic);

// One acoustic sub-step (LOOP BODY): advance_uv -> advance_mu_t -> advance_w -> calc_p_rho.
// Mirrors solve_em.F:1525-1869. `small_step` is 1-BASED (1..N) — every loop substep applies
// divergence damping. The pressure INIT (calc_p_rho step=0: sets pm1, no damping) is a SEPARATE
// pre-loop call the caller runs once before the first substep (solve_em.F:1352), NOT this function.
// Functional style (returns a new State) so it stays autograd-safe (no in-place grad-tensor mutation).
State advance_substep(const State& s, const Const& c, int small_step);  // 1-based loop body

// Individual operators (exposed for term-by-term validation vs dyn_em).
State advance_uv   (const State& s, const Const& c);                 // module_small_step_em.F:654
State advance_mu_t (const State& s, const Const& c);                 // :969
State advance_w    (const State& s, const Const& c);                 // :1178  (uses Thomas, Inc 2b)
State calc_p_rho   (const State& s, const Const& c, int step);       // :438  (step=0 => pre-loop init; >0 => damp)

}}} // namespace wrf::sdirk3::acoustic
