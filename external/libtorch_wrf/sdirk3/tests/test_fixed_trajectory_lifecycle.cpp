#include "tile_test_fixture.h"
#include "wrf_sdirk3_autograd_utils.h"
#include <iostream>
#include <functional>

using wrf::sdirk3::test::TileCase;
using wrf::sdirk3::test::total;

static void configure(bool top_lid) {
  auto& c = wrf::sdirk3::g_sdirk3_config;
  c = wrf::sdirk3::SDIRK3Config{};
  c.debug_level=0; c.n_threads=1; c.imex_split_mode=3; c.mass_coordinate_mode=1;
  c.buoyancy_use_current_w=true; c.hevi_split=false; c.khdif=0.0f; c.kvdif=0.0f; c.wrf_w_damping=0; c.use_autograd=true; c.imex_slow_in_tangent=true;
  c.non_hydrostatic=true; c.do_curvature=true; c.split_explicit_top_lid=top_lid;
  c.precond_type=0; c.max_newton_iter=40; c.newton_tol=1e-7f; c.krylov_tol=1e-6f;
  c.gmres_restart=30; c.max_krylov_iter=20; c.stage_fail_action=1;
  c.gmres_warmstart=false; c.inn_warmstart_enable=false; c.retain_graph_for_adjoint=true;
}
static torch::Tensor x0() { auto i=torch::arange(total,torch::kFloat32); return 1e-3f*torch::sin(.017f*i); }
static torch::Tensor terminal() { auto i=torch::arange(total,torch::kFloat32); return torch::cos(.023f*i).to(torch::kFloat32); }
static void expect_rejected(const std::function<void()>& call, const char* message);
static void run_n(int n, bool top_lid) {
  TileCase t(100000.0f, 0.0f, true); t.set(x0()); t.solver.beginFixedTrajectory(n);
  for (int k=0;k<n;++k) t.step(.1f);
  auto lambda=t.solver.pullbackFixedTrajectory(terminal());
  TORCH_CHECK(lambda.defined() && torch::isfinite(lambda).all().item<bool>() && lambda.norm().item<double>()>1e-8,
              "nonzero N",n," pullback invalid");
  auto zero=t.solver.pullbackFixedTrajectory(torch::zeros_like(terminal()));
  TORCH_CHECK(zero.norm().item<double>()==0.0,"zero cotangent failed N",n);
  t.solver.closeFixedTrajectory();
  std::cout << "FIXED_TRAJECTORY_LIFECYCLE NH=1 curvature=1 top_lid=" << top_lid
            << " N="<<n<<" initial_norm="<<lambda.norm().item<double>()<<" passed\n";
}
static void deferred_request_lifecycle() {
  TileCase t(100000.0f, 0.0f, true); t.set(x0());
  t.solver.requestFixedTrajectory(2, {.1f, .1f});
  TORCH_CHECK(t.solver.fixedTrajectoryRequested(), "deferred request was not armed");
  t.step(.1f); t.step(.1f);
  const auto lambda=t.solver.pullbackFixedTrajectory(terminal());
  TORCH_CHECK(wrf::sdirk3::guarded_item<bool>(torch::isfinite(lambda).all()) &&
              wrf::sdirk3::guarded_item<double>(lambda.norm())>1e-8,
              "deferred activation pullback invalid");
  t.solver.closeFixedTrajectory();

  TileCase cancelled(100000.0f, 0.0f, true); cancelled.set(x0());
  cancelled.solver.requestFixedTrajectory(1, {});
  cancelled.solver.cancelFixedTrajectoryRequest();
  TORCH_CHECK(!cancelled.solver.fixedTrajectoryRequested(), "pending request was not cancelled");
  cancelled.step(.1f);

  TileCase invalid(100000.0f, 0.0f, true); invalid.set(x0());
  invalid.solver.requestFixedTrajectory(1, {});
  wrf::sdirk3::g_sdirk3_config.imex_split_mode=2;
  expect_rejected([&]{invalid.step(.1f);}, "fixed trajectory canonical NH/curvature profile unsupported");
  wrf::sdirk3::g_sdirk3_config.imex_split_mode=3;
  invalid.solver.requestFixedTrajectory(1, {});
  invalid.solver.cancelFixedTrajectoryRequest();

  TileCase reset(100000.0f, 0.0f, true); reset.set(x0());
  reset.solver.requestFixedTrajectory(1, {});
  reset.solver.invalidateCaches();
  TORCH_CHECK(!reset.solver.fixedTrajectoryRequested(), "reset retained pending request");
  reset.step(.1f);
  reset.solver.requestFixedTrajectory(1, {});
  reset.step(.1f);
  (void)reset.solver.pullbackFixedTrajectory(terminal());
  reset.solver.closeFixedTrajectory();
  std::cout << "FIXED_TRAJECTORY_DEFERRED_LIFECYCLE passed\n";
}
static void reset_invalidates() {
  TileCase t(100000.0f, 0.0f, true); t.set(x0()); t.solver.beginFixedTrajectory(2); t.step(.1f);
  t.solver.invalidateCaches();
  // A reset drops the zero-copy map/coeff caches. Republish one native step
  // before re-arming the tape so the new fingerprint starts from live inputs.
  t.step(.1f);
  t.solver.beginFixedTrajectory(2); t.step(.1f); t.step(.1f);
  (void)t.solver.pullbackFixedTrajectory(terminal()); t.solver.closeFixedTrajectory();
  std::cout << "FIXED_TRAJECTORY_RESET_INVALIDATES passed\n";
}
static void config_mutation_rejects() {
  TileCase t(100000.0f, 0.0f, true); t.set(x0()); t.solver.beginFixedTrajectory(2); t.step(.1f); t.step(.1f);
  wrf::sdirk3::g_sdirk3_config.imex_split_mode=2;
  expect_rejected([&]{(void)t.solver.pullbackFixedTrajectory(terminal());},
                  "fixed trajectory fixed input changed");
  t.solver.closeFixedTrajectory();
  std::cout << "FIXED_TRAJECTORY_MUTATION_REJECT passed\n";
}
static void expect_rejected(const std::function<void()>& call, const char* message) {
  bool rejected=false;
  try { call(); }
  catch (const c10::Error& e) {
    rejected=std::string(e.what()).find(message)!=std::string::npos;
    if (!rejected) throw;
  } catch (const std::exception& e) {
    rejected=std::string(e.what()).find(message)!=std::string::npos;
    if (!rejected) throw;
  }
  TORCH_CHECK(rejected,"missing expected rejection: ",message);
}
static void map_mutation_rejects(bool after_final) {
  TileCase t(100000.0f, 0.0f, true); t.set(x0()); t.solver.beginFixedTrajectory(2); t.step(.1f);
  if (after_final) t.step(.1f);
  // Mutate the caller-owned from_blob source in place, with no setter/reset or
  // epoch update. The retained mathematical input must still reject it.
  t.mass_map[9] += .125f;
  if (after_final)
    expect_rejected([&]{(void)t.solver.pullbackFixedTrajectory(terminal());},
                    "fixed trajectory fixed input changed");
  else
    expect_rejected([&]{t.step(.1f);},"fixed trajectory fixed input changed");
  t.solver.closeFixedTrajectory();
  std::cout << "FIXED_TRAJECTORY_MAP_MUTATION after_final=" << after_final << " passed\n";
}
static void handoff_rejects() {
  TileCase t(100000.0f, 0.0f, true); t.set(x0()); t.solver.beginFixedTrajectory(2); t.step(.1f);
  t.theta[5] += .001f;
  expect_rejected([&]{t.step(.1f);},"packed state handoff changed");
  t.solver.closeFixedTrajectory();
  std::cout << "FIXED_TRAJECTORY_HANDOFF_REJECT passed\n";
}
static void incomplete_rejects() {
  TileCase t(100000.0f, 0.0f, true); t.set(x0()); t.solver.beginFixedTrajectory(2); t.step(.1f);
  expect_rejected([&]{(void)t.solver.pullbackFixedTrajectory(terminal());},"fixed trajectory incomplete");
  t.solver.closeFixedTrajectory();
  std::cout << "FIXED_TRAJECTORY_INCOMPLETE_REJECT passed\n";
}
static void first_step_profile_rejects() {
  TileCase t(100000.0f, 0.0f, true); t.set(x0()); t.solver.beginFixedTrajectory(2);
  const auto before=t.state();
  wrf::sdirk3::g_sdirk3_config.buoyancy_use_current_w=false;
  expect_rejected([&]{t.step(.1f);},"fixed trajectory fixed input changed");
  TORCH_CHECK(torch::equal(t.state(),before),"invalid first-step profile changed caller state");
  t.solver.closeFixedTrajectory();
  std::cout << "FIXED_TRAJECTORY_FIRST_PROFILE_REJECT passed\n";
}
static void gravity_mutation_rejects() {
  TileCase t(100000.0f, 0.0f, true); t.set(x0()); t.solver.beginFixedTrajectory(2); t.step(.1f); t.step(.1f);
  const auto grid=t.solver.getGridInfo();
  TORCH_CHECK(grid,"fixture has no gravity owner");
  grid->g *= 1.125f;
  grid->reradius *= 1.125f;
  expect_rejected([&]{(void)t.solver.pullbackFixedTrajectory(terminal());},
                  "fixed trajectory fixed input changed");
  t.solver.closeFixedTrajectory();
  std::cout << "FIXED_TRAJECTORY_GRAVITY_REJECT passed\n";
}
static void base_mutation_rejects() {
  TileCase t(100000.0f, 0.0f, true); t.set(x0()); t.solver.beginFixedTrajectory(2); t.step(.1f);
  auto base = t.solver.getBaseStatePressure();
  base.flatten()[7] += .25f;
  expect_rejected([&]{t.step(.1f);}, "fixed trajectory fixed input changed");
  t.solver.closeFixedTrajectory();
  std::cout << "FIXED_TRAJECTORY_BASE_MUTATION_REJECT passed\n";
}
static void coefficient_mutation_rejects() {
  TileCase t(100000.0f, 0.0f, true); t.set(x0()); t.solver.beginFixedTrajectory(2); t.step(.1f);
  t.one[2] += .125f;
  expect_rejected([&]{t.step(.1f);}, "fixed trajectory fixed input changed");
  t.solver.closeFixedTrajectory();
  std::cout << "FIXED_TRAJECTORY_COEFF_MUTATION_REJECT passed\n";
}
static void slope_inplace_mutation_rejects() {
  TileCase t(100000.0f, 0.0f, true); t.set(x0()); t.solver.beginFixedTrajectory(2); t.step(.1f);
  {
    torch::NoGradGuard no_grad;
    auto zx = t.terrainSlopeX();
    auto zy = t.terrainSlopeY();
    TORCH_CHECK(zx.defined() && zy.defined() && zx.numel() > 8 && zy.numel() > 8,
                "fixture terrain slopes are not published");
    // These handles share the solver-owned storage; this is an in-place source
    // mutation, not a setter replacement or caller-buffer mutation.
    zx.flatten()[7] += 1.0e-3f;
    zy.flatten()[11] -= 1.0e-3f;
  }
  expect_rejected([&]{t.step(.1f);}, "fixed trajectory fixed input changed");
  t.solver.closeFixedTrajectory();
  std::cout << "FIXED_TRAJECTORY_SLOPE_INPLACE_MUTATION_REJECT passed\n";
}
static void moisture_correction_mutation_rejects() {
  TileCase t(100000.0f, 0.0f, true); t.set(x0()); t.solver.beginFixedTrajectory(2); t.step(.1f);
  {
    torch::NoGradGuard no_grad;
    auto cqu = t.moistureCorrectionU();
    auto cqv = t.moistureCorrectionV();
    auto cqw = t.moistureCorrectionW();
    TORCH_CHECK(cqu.defined() && cqv.defined() && cqw.defined(),
                "fixture moisture corrections are not published");
    cqu.flatten()[3] += 1.0e-3f;
    cqv.flatten()[5] -= 1.0e-3f;
    cqw.flatten()[7] += 1.0e-3f;
  }
  expect_rejected([&]{t.step(.1f);}, "fixed trajectory fixed input changed");
  t.solver.closeFixedTrajectory();
  std::cout << "FIXED_TRAJECTORY_MOISTURE_CORRECTION_MUTATION_REJECT passed\n";
}
static void effective_vertical_metric_mutation_rejects() {
  // Actual native caller: a changed float metric must invalidate the tape.
  {
    TileCase t(100000.0f, 0.0f, true); t.set(x0());
    t.solver.beginFixedTrajectory(2); t.step(.1f);
    t.metric[1] *= 1.01f;
    expect_rejected([&]{t.step(.1f);}, "fixed trajectory fixed input changed");
    t.solver.closeFixedTrajectory();
  }
  // Test the input guard itself for a double grid source. unifiedStep always
  // republishes the float-pointer ABI, so this is a guard precision test,
  // separate from the native caller mutation above.
  for (bool change_rdnw : {false, true}) {
    TileCase t(100000.0f, 0.0f, true); t.useDoubleGridMetrics(); t.set(x0());
    t.solver.beginFixedTrajectory(1);
    t.checkFixedInputs();  // The unchanged source must be accepted first.
    auto grid = t.solver.getGridInfo();
    auto metric = change_rdnw ? grid->rdnw : grid->rdn;
    const auto rounded_before = metric.to(torch::kFloat32).clone();
    {
      torch::NoGradGuard no_grad;
      metric.flatten()[1] += std::ldexp(1.0, -40);
    }
    TORCH_CHECK(torch::equal(rounded_before, metric.to(torch::kFloat32)),
                "metric mutation must be invisible after float32 rounding");
    expect_rejected([&]{t.checkFixedInputs();}, "fixed trajectory fixed input changed");
    t.solver.closeFixedTrajectory();
  }
  std::cout << "FIXED_TRAJECTORY_EFFECTIVE_VERTICAL_METRIC_REJECT passed\n";
}
static void coriolis_metric_mutation_rejects() {
  TileCase t(100000.0f, 0.0f, true); t.set(x0()); t.solver.beginFixedTrajectory(2); t.step(.1f);
  {
    torch::NoGradGuard no_grad;
    auto f = t.coriolisF();
    auto e = t.coriolisE();
    TORCH_CHECK(f.defined() && e.defined() && f.numel() > 2 && e.numel() > 2,
                "fixture Coriolis tensors are not published");
    f.flatten()[1] += 1.0e-4f;
    e.flatten()[2] -= 1.0e-4f;
  }
  expect_rejected([&]{t.step(.1f);}, "fixed trajectory fixed input changed");
  t.solver.closeFixedTrajectory();
  std::cout << "FIXED_TRAJECTORY_CORIOLIS_METRIC_REJECT passed\n";
}
static void timestep_mutation_rejects() {
  TileCase t(100000.0f, 0.0f, true); t.set(x0()); t.solver.beginFixedTrajectory(2); t.step(.1f);
  expect_rejected([&]{t.step(.075f);}, "fixed trajectory timestep changed");
  t.solver.closeFixedTrajectory();
  std::cout << "FIXED_TRAJECTORY_CONSTANT_DT_REJECT passed\n";
}
static void scheduled_timestep_record_accepts() {
  TileCase t(100000.0f, 0.0f, true); t.set(x0());
  const std::vector<float> schedule{.1f, .075f, .05f};
  t.solver.beginFixedTrajectory(static_cast<int>(schedule.size()), schedule);
  for (const float dt : schedule) t.step(dt);
  const auto lambda = t.solver.pullbackFixedTrajectory(terminal());
  TORCH_CHECK(torch::isfinite(lambda).all().item<bool>() && lambda.norm().item<double>() > 1e-8,
              "nonuniform timestep tape pullback invalid");
  t.solver.closeFixedTrajectory();
  std::cout << "FIXED_TRAJECTORY_NONUNIFORM_DT_RECORD passed\n";
}
int main() {
  torch::set_num_threads(1);
  int failures = 0;
  auto run = [&](const char* name, const std::function<void()>& test) {
    try { test(); }
    catch (const std::exception& e) {
      ++failures;
      std::cerr << "FAIL " << name << ": " << e.what() << "\n";
    }
  };
  for (const bool top_lid : {false, true}) {
    configure(top_lid); run("run_n2", [&]{run_n(2, top_lid);});
    configure(top_lid); run("run_n3", [&]{run_n(3, top_lid);});
    configure(top_lid); run("deferred_request", deferred_request_lifecycle);
    configure(top_lid); run("reset_invalidates", reset_invalidates);
    configure(top_lid); run("config_mutation", config_mutation_rejects);
    configure(top_lid); run("map_mutation_before_final", [&]{map_mutation_rejects(false);});
    configure(top_lid); run("map_mutation_after_final", [&]{map_mutation_rejects(true);});
    configure(top_lid); run("handoff", handoff_rejects);
    configure(top_lid); run("incomplete", incomplete_rejects);
    configure(top_lid); run("first_step_profile", first_step_profile_rejects);
    configure(top_lid); run("gravity", gravity_mutation_rejects);
    configure(top_lid); run("base", base_mutation_rejects);
    configure(top_lid); run("coefficients", coefficient_mutation_rejects);
    configure(top_lid); run("slope_inplace", slope_inplace_mutation_rejects);
    configure(top_lid); run("moisture_correction", moisture_correction_mutation_rejects);
    configure(top_lid); run("effective_vertical_metric", effective_vertical_metric_mutation_rejects);
    configure(top_lid); run("coriolis_metric", coriolis_metric_mutation_rejects);
    configure(top_lid); run("constant_dt", timestep_mutation_rejects);
    configure(top_lid); run("nonuniform_dt", scheduled_timestep_record_accepts);
  }
  return failures == 0 ? 0 : 1;
}
