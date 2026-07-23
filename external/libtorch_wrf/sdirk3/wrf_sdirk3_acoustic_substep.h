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
    torch::Tensor t_ave;   // RAW coupled theta'' saved by advance_mu_t BEFORE its update (:1141).
                           // This is the (1-epssm) old-time arm of advance_w's t_2ave (:1314) — WRF
                           // passes ONE array (t_2save) written here and read there each substep.
    torch::Tensor t_2ave;  // NORMALIZED time-avg theta (advance_w :1314-1317 output; diagnostic only —
                           // WRF overwrites it in place and never reads it back across substeps)
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
    torch::Tensor t_1;                 // stage-entry theta save (WRF t_save; advance_mu_t/advance_w dummy t_1)
    torch::Tensor ww_1;               // large-timestep omega {ny,nz_w,nx}
    torch::Tensor ph_1;                // stage-entry geopotential save (WRF ph_save; advance_w dummy ph_1) {ny,nz_w,nx}
    torch::Tensor phb;                 // base-state geopotential {ny,nz_w,nx} (ww*dphi advection + php 4th PGF term)
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
// mu is {ny,nx}; returns {ny,nx+1} / {ny+1,nx}. em_b_wave BOUNDARY CONDITIONS DIFFER PER AXIS:
//   - x is PERIODIC (channel) -> mass_to_upoint wraps the i-edges: muu[0]=muu[nx]=0.5*(mu[nx-1]+mu[0]).
//   - y is SYMMETRIC (free-slip walls) -> mass_to_vpoint reflects: muv[0]=mu[0], muv[ny]=mu[ny-1].
// A case with different BCs (e.g. y-periodic) must overwrite the edge rows/cols via the halo at the call site.
torch::Tensor mass_to_upoint(const torch::Tensor& mu_col);   // {ny,nx} -> {ny,nx+1}  (x-periodic)
torch::Tensor mass_to_vpoint(const torch::Tensor& mu_col);   // {ny,nx} -> {ny+1,nx}  (y-symmetric wall)

// --- calc_coef_w factorization (module_small_step_em.F:570-652) ---
// Builds the vertical acoustic Thomas bands for one RK stage/substep timestep.  This mirrors
// WRF's active inputs: c3*/c4* are accepted by the Fortran signature but unused in the formula;
// cqw and top_lid are retained because they do alter the bands outside the unit-cqw/lid-on case.
struct CoefW { torch::Tensor a, alpha, gamma; };
CoefW calc_coef_w(const torch::Tensor& c2a, const torch::Tensor& mu_full,
                  const torch::Tensor& cqw,
                  const torch::Tensor& c1h, const torch::Tensor& c2h,
                  const torch::Tensor& c1f, const torch::Tensor& c2f,
                  const torch::Tensor& rdn, const torch::Tensor& rdnw,
                  float dts, float g, float epssm, bool top_lid);

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

// --- Coupled 4-term horizontal PGF at u/v points ---
// ONE formula, TWO WRF call sites: rk_tendency's horizontal_pressure_gradient
// (module_big_step_utilities_em.F:2270-2400, evaluated at the RK STAGE STATE) and the small-step
// advance_uv (module_small_step_em.F:802-946, evaluated on the substep DEVIATIONS). The driver
// needs the stage-state evaluation because computeUnifiedRHS's fast channel is perturbation-form
// and vanishes at the linearization point (measured: fast rv 1.9e-7 at the balanced IC where the
// geostrophic V-PGF must be ~f*u*mu). em_b_wave BCs: periodic x (u-points 0..nx-1 wrapped, seam
// column aliased so dpx is {ny,nz,nx+1}); symmetric y (wall v rows ZERO; dpy is {ny_v,nz,nx}).
// Sign: the caller SUBTRACTS (WRF ru_tend -= cqu*dpx; u -= dts*cqu*dpxy).
// --- phi-consistent pressure / inverse-density diagnostics (WRF calc_p_rho_phi family) ---
// GEOMETRIC (hypso-1) form, kept DELIBERATELY over the Registry-default hypso-2: p' is a small
// difference of large EOS terms, so alpha's float32 rel-eps (~1e-4) lands as +-14 Pa of
// diagnosis NOISE on p' — WRF has its own such noise (measured in-run pg_buoy residual 22.09
// vs 1.6e-4 with wrfinput's P) and it CANNOT be reproduced without bit-identical operation
// order. An in-model hypso-2 attempt injected a DIFFERENT noise field and degraded the
// previously-EXACT ru/rv PGF parity. The smooth geometric alpha keeps horizontal-PGF parity
// EXACT (measured ru 33.3752/33.3729, rv 82.0871 == dyn_em) because any vertical-profile alpha
// error cancels in horizontal differences.
//   al_full = |rdnw(k)| * (d_k(ph)+d_k(phb)) / (c1h(k)*muts+c2h(k))
//   p'      = p0 * ( rd*(t0+t') / (p0*al_full) )^(cp/cv) - pb
// Returns {p_pert, al_pert, al_full}, all {ny,nz,nx}. TRUE dims.
std::tuple<torch::Tensor, torch::Tensor, torch::Tensor> diag_p_al(
    const torch::Tensor& ph,    // perturbation geopotential {ny,nz_w,nx}
    const torch::Tensor& phb,   // base geopotential {ny,nz_w,nx}
    const torch::Tensor& t,     // theta perturbation from t0 {ny,nz,nx}
    const torch::Tensor& pb,    // base pressure {ny,nz,nx}
    const torch::Tensor& muts,  // full column mass {ny,nx}
    const torch::Tensor& mub,   // base column mass {ny,nx}
    const torch::Tensor& c1h, const torch::Tensor& c2h, const torch::Tensor& rdnw,
    float p0, float rd, float cpovcv, float t0);

// --- WRF hypsometric_opt=2 p diagnosis (module_big_step_utilities_em.F:1033-1052, WRF default) ---
// al = dphi/(phm*LOG(pfd/pfu)) with pf = znw*MUTS+ptop (hybrid off), then the same EOS as
// diag_p_al. ONLY consumer: pg_buoy_w's p (matched p-diagnosis/operator pair for the discrete
// vertical balance). Returns p' {ny,nz,nx}. znw {nz_w}, znu {nz}, ptop {ny,nx}.
torch::Tensor diag_p_hypso2(
    const torch::Tensor& ph, const torch::Tensor& phb, const torch::Tensor& t,
    const torch::Tensor& pb, const torch::Tensor& muts,
    const torch::Tensor& znw, const torch::Tensor& znu, const torch::Tensor& ptop,
    float p0, float rd, float cpovcv, float t0);

// --- WRF pg_buoy_w (module_big_step_utilities_em.F:2423-2503, dry cqw=0 -> cq1=1, cq2=0) ---
// NOTE: NOT wired into the split-explicit rw channel. The dyn_em stage-1 decomposition measured
// its in-run contribution as 22.09 coupled — but that value is WRF's OWN float32 p-diagnosis
// noise (offline with wrfinput's P: 1.6e-4). The RHS fast channel already supplies the
// internally-consistent equivalent (measured 1.59e-4); reproducing WRF's 22.09 would require
// bit-identical diagnosis noise, and chasing it with an in-model hypso-2 p made w'' WORSE
// (3321 vs dyn_em 541). Kept for future hypso-2-exact work.
torch::Tensor pg_buoy_w_stage(
    const torch::Tensor& p,      // pressure perturbation {ny,nz,nx}
    const torch::Tensor& mu,     // column-mass perturbation {ny,nx}
    const torch::Tensor& msfty,  // {ny,nx}
    const torch::Tensor& c1f, const torch::Tensor& rdn, const torch::Tensor& rdnw, float g);

// --- WRF curvature, w row (module_big_step_utilities_em.F:4452-4471) ---
// COUPLED earth-curvature rw tendency at the RK stage state (ADT eqn 46 RHS term 4:
// [mu/(a my)]*(u*u+v*v)). Applied UNCONDITIONALLY by rk_tendency; MEASURED to be the dominant
// content of dyn_em's stage-1 rw_tend (~63 of 70.74 coupled at the balanced IC jet). The
// buoyancy/vertical-PGF residual (pg_buoy_w) is NOT built here: the RHS fast channel already
// computes it with its internally-consistent pressure (measured 1.59e-4 == offline pg_buoy with
// WRF's P), so the w channel keeps its Full-RHS sourcing and ADDS this term.
//   rw(kf=1..nz-1) += reradius*( ruw*uw + (msftx/msfty)*rvw*vw ),
//   Xw(kf) = 0.5*( fnm(kf)*(X(kf,i)+X(kf,i+1)) + fnp(kf)*(X(kf-1,i)+X(kf-1,i+1)) )  [u-pair; v in j]
// with ru=(c1h*muu+c2h)*u/msfuy, rv=(c1h*muv+c2h)*v*msfvx_inv. Levels 0 and nz zero. TRUE dims.
torch::Tensor curvature_w_stage(
    const torch::Tensor& u,                                 // {ny,nz,nx_u}
    const torch::Tensor& v,                                 // {ny_v,nz,nx}
    const torch::Tensor& muu, const torch::Tensor& muv,     // {ny,nx_u}, {ny_v,nx}
    const torch::Tensor& msfuy, const torch::Tensor& msfvx_inv,
    const torch::Tensor& msftx, const torch::Tensor& msfty, // {ny,nx}
    const torch::Tensor& c1h, const torch::Tensor& c2h,
    const torch::Tensor& fnm, const torch::Tensor& fnp, float reradius);

// WRF w-momentum CORIOLIS term (module_big_step_utilities_em.F:3843):
//   rw_tend += e*(cosa*avg_w(ru) - (msftx/msfty)*sina*avg_w(rv))
// The companion to curvature_w_stage; both are frozen slow w-forcings in WRF's rk_tendency.
torch::Tensor coriolis_w_stage(
    const torch::Tensor& u, const torch::Tensor& v,
    const torch::Tensor& muu, const torch::Tensor& muv,
    const torch::Tensor& msfuy, const torch::Tensor& msfvx_inv,
    const torch::Tensor& msftx, const torch::Tensor& msfty,
    const torch::Tensor& c1h, const torch::Tensor& c2h,
    const torch::Tensor& fnm, const torch::Tensor& fnp,
    const torch::Tensor& e, const torch::Tensor& cosa, const torch::Tensor& sina);

// --- WRF rhs_ph (module_big_step_utilities_em.F:1369-2182; dry, phi_adv_z default,
// h_sca_adv_order=5 -> the 6th-order centered advection branch :1774+) ---
// The COMPLETE COUPLED large-step geopotential tendency at the RK stage state:
//   vertical:  wdwn(kf)= .5*(ww(kf)+ww(kf-1))*rdnw(kf-1)*d(phf); ph_t(1..nz-2) -= fnm*wdwn(+1)+fnp*wdwn
//   mu g w:    ph_t(1..nz-1) += (c1f*mut+c2f)*g*w/msfty
//   x/y adv:   ph_t -= (0.25*rd/msfty)*[m_stag(+)*velsum(+) + m_stag*velsum]
//                      *(1/60)*(45*d1 - 9*d2 + d3)(ph+phb)     [top full level nz-1 uses cfn/cfn1]
// Level 0 untouched (0); level nz (kde) zeroed (:1505). x periodic (wrap), y symmetric (even mass
// ghosts about the walls, module_bc.F). TRUE dims. This IS Const.ph_tend for the small step.
torch::Tensor rhs_ph_stage(
    const torch::Tensor& ph, const torch::Tensor& phb,     // {ny,nz_w,nx}
    const torch::Tensor& u,                                // {ny,nz,nx_u}
    const torch::Tensor& v,                                // {ny_v,nz,nx}
    const torch::Tensor& ww, const torch::Tensor& w,       // {ny,nz_w,nx}
    const torch::Tensor& mut, const torch::Tensor& muu, const torch::Tensor& muv,
    const torch::Tensor& msfty,
    const torch::Tensor& c1f, const torch::Tensor& c2f,
    const torch::Tensor& fnm, const torch::Tensor& fnp, const torch::Tensor& rdnw,
    float cfn, float cfn1, float rdx, float rdy, float g);

std::pair<torch::Tensor, torch::Tensor> horizontal_pgf(
    const torch::Tensor& ph,    // geopotential perturbation {ny,nz_w,nx}
    const torch::Tensor& p,     // pressure perturbation {ny,nz,nx}
    const torch::Tensor& al,    // inverse-density perturbation {ny,nz,nx}
    const torch::Tensor& alt,   // full inverse density {ny,nz,nx}
    const torch::Tensor& pb,    // base pressure {ny,nz,nx}
    const torch::Tensor& phf,   // FULL geopotential (pert+base) {ny,nz_w,nx} -> php mass-level avg
    const torch::Tensor& mu,    // column-mass perturbation {ny,nx}
    const torch::Tensor& muu, const torch::Tensor& muv,
    const torch::Tensor& c1h, const torch::Tensor& c2h,
    const torch::Tensor& fnm, const torch::Tensor& fnp, const torch::Tensor& rdnw,
    float rdx, float rdy, float cf1, float cf2, float cf3);

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
