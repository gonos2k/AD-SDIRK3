#!/usr/bin/env python3
"""Run a bounded two-control inverse for one seeded full-Y inertia-gravity mode."""
from __future__ import annotations

import argparse
import platform
import subprocess
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import probe_small_step_inverse as driver  # noqa: E402
import test_fine_wave_inverse as inverse  # noqa: E402
import test_y_wave_inverse as yi  # noqa: E402
import wave_rotating_reference as rotating  # noqa: E402

GRID, NY, STEPS, DT, SIGMA = (16, 12, 8), 12, 960, 0.3125, 3.0e-4
TIMES, W_AMPLITUDE = (150.0, 300.0), 0.01
TARGET_F = float(np.float32(1.0e-4))
TRUTH_CONTROLS = np.array([0.78, -0.36], dtype=np.float64)
START_CONTROLS = np.array([0.25, 0.15], dtype=np.float64)
ROTATING_ARGS = ("--rotating-wave",)


def source_receipt(exe: Path) -> dict:
    names = (
        "tools/test_rotating_wave_inverse.py", "tools/test_y_wave_inverse.py",
        "tools/probe_small_step_inverse.py", "tools/test_fine_wave_inverse.py",
        "tools/wave_quadratic_reference.py", "tools/wave_y_reference.py",
        "tools/wave_rotating_reference.py", "tools/wave_energy_spatial_reference.py",
        "tools/wave_quadratic_transport.py",
        "external/libtorch_wrf/sdirk3/tests/test_native_wave_refinement.cpp",
        "external/libtorch_wrf/sdirk3/wrf_sdirk3_coriolis.h",
        "external/libtorch_wrf/sdirk3/wrf_sdirk3_newton_solver.h",
        "external/libtorch_wrf/sdirk3/wrf_sdirk3_newton_solver.cpp",
        "external/libtorch_wrf/sdirk3/wrf_sdirk3_config.h",
        "external/libtorch_wrf/sdirk3/wrf_sdirk3_tile_unified.h",
        "external/libtorch_wrf/sdirk3/wrf_sdirk3_tile_unified_impl.cpp",
        "dyn_em/module_big_step_utilities_em.F",
        "dyn_em/module_implicit_sdirk3.F",
    )
    return {"revision": subprocess.check_output(
                ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
            "executable_sha256": driver.sha(exe),
            "sha256": {name: driver.sha(ROOT / name) for name in names}}


def require_rotating_profile(meta: dict, text: dict,
                             expected_f: float | None = None) -> dict:
    f, rdy = rotating.fp32_coefficients(meta, text, ny=NY,
                                        ly=30_000.0, nx=GRID[0])
    if f != TARGET_F or (expected_f is not None and f != expected_f):
        raise ValueError(f"rotating f differs from target/descriptor: {f}")
    extra = {"wrf_w_damping_config": 0.0, "wrf_damp_opt_config": 0.0,
             "diffusion_option_config": 2.0, "khdif_config": 0.0,
             "kvdif_config": 0.0}
    bad = {key: (meta.get(key), value) for key, value in extra.items()
           if meta.get(key) != value}
    if bad:
        raise ValueError(f"rotating-wave inactive-physics profile mismatch: {bad}")
    return {"f_fp32": f, "rdy_fp32": rdy,
            "rdy_fp32_bits": text["rdy_fp32_bits"],
            "f_field_min": meta["coriolis_f_effective_field_min"],
            "f_field_max": meta["coriolis_f_effective_field_max"],
            "f_input_max_abs": meta["f_input_max_abs"],
            "rotating_wave_test_enabled": meta["rotating_wave_test_enabled"]}


def descriptor(exe: Path, outdir: Path, report: dict) -> dict:
    original = inverse.native_call

    def timed_descriptor_call(executable, nx, ny, nz, output, native_args):
        command = [str(executable), str(nx), str(ny), str(nz), str(output),
                   *native_args, *ROTATING_ARGS]
        return yi.run_timed(label="rotating_descriptor", command=command,
                            output=output, outdir=outdir, report=report)

    inverse.native_call = timed_descriptor_call
    try:
        data = inverse.descriptor(exe, outdir, GRID)
    finally:
        inverse.native_call = original
    profile = require_rotating_profile(data["meta"], data["text_meta"])
    data["rotating_profile"] = profile
    symbols = rotating.validate_descriptor_symbols(data["column"], data["text_meta"], ny=NY)
    data["source_symbols"] = symbols
    return data


def make_case(descriptor_data: dict, outdir: Path) -> dict:
    column = descriptor_data["column"]
    f = descriptor_data["rotating_profile"]["f_fp32"]
    rdy = descriptor_data["rotating_profile"]["rdy_fp32"]
    seed = rotating.continuation_seed(column, rdy_fp32=rdy, ny=NY)
    if not seed["f0_lift"]["passed"]:
        raise ValueError("full-Y source does not reduce to the existing f=0 operator")
    energy = rotating.verify_uniform_f_energy_identity(
        column, f_fp32=f, rdy_fp32=rdy, ny=NY)
    if not energy["passed"]:
        raise ValueError("uniform-background Coriolis energy identity failed")
    matrix = rotating.full_matrix(column, f_fp32=f, rdy_fp32=rdy, ny=NY)
    mode = rotating.select_seeded_inertia_gravity_mode(
        matrix, column, seed["state"], f_fp32=f, ny=NY)
    quadratures, quad_info = rotating.time_quadrature_basis(mode, w_amplitude=W_AMPLITUDE)
    xyz = rotating.source_observation_xyz(column)
    truth_q = TRUTH_CONTROLS[0]*quadratures[0] + TRUTH_CONTROLS[1]*quadratures[1]
    observations = rotating.source_observations(
        column, truth_q, matrix, xyz, times=TIMES, linear_geometry=False, ny=NY)
    H = rotating.source_observation_matrix(
        column, quadratures, matrix, xyz, times=TIMES, ny=NY)
    B = np.column_stack([rotating.pack_native_state(column, q, ny=NY)
                         for q in quadratures])
    base = descriptor_data["base"]
    packed_start = base + B @ START_CONTROLS
    scaled_H = H / SIGMA
    singular_values = np.linalg.svd(scaled_H, compute_uv=False)
    rank = int(np.linalg.matrix_rank(scaled_H))
    gram = scaled_H.T @ scaled_H
    if rank != 2 or singular_values[-1] <= 0.0:
        raise ValueError(f"rotating source H_BG is not rank two: {singular_values.tolist()}")
    np.linalg.cholesky(gram)

    for q in quadratures:
        packed = rotating.pack_native_state(column, q, ny=NY)
        v = packed[yi.block_slices()["v"]].reshape(NY+1, 8, GRID[0])
        if np.count_nonzero(v[[0, -1]]):
            raise ValueError("rotating control has a nonzero Y-wall V component")
    source_rhs_vectors, source_signals = {}, {}
    rhs_floors = {}
    slices = yi.block_slices()
    for name, block in slices.items():
        rhs_floors[name] = float(
            64.0*np.finfo(np.float64).eps*yi.RHS_BLOCK_SCALES[name]*
            np.sqrt(block.stop-block.start))
    for j, q in enumerate(quadratures):
        scaled_q = yi.RHS_DIRECTION_SCALE*q
        rhs = rotating.pack_native_state(
            column, rotating.full_rhs(column, scaled_q, f_fp32=f,
                                      rdy_fp32=rdy, ny=NY), ny=NY)
        source_rhs_vectors[str(j)] = rhs
        source_signals[str(j)] = {name: float(np.linalg.norm(rhs[slc]))
                                  for name, slc in slices.items()}
    source_gradient = H.T @ (H @ START_CONTROLS - observations.reshape(-1)) / SIGMA**2
    metric = np.linalg.solve(gram, np.eye(2, dtype=np.float64))

    inputs = outdir / "inputs"
    inputs.mkdir(parents=True, exist_ok=True)
    obs_path = inputs / "observations.txt"
    yi.write_observations(obs_path, xyz, observations)
    np.save(inputs / "canonical_basis.npy", B, allow_pickle=False)
    np.save(inputs / "source_H_BG.npy", H, allow_pickle=False)
    driver.write_vector(inputs / "initial_state.txt", packed_start)
    return {"column": column, "f": f, "rdy": rdy, "matrix": matrix,
            "seed": seed, "energy_identity": energy, "mode": mode,
            "quadratures": quadratures, "quadrature_metadata": quad_info,
            "xyz": xyz, "observations": observations, "truth_state": truth_q,
            "basis": B, "base_state": base, "initial_state": base,
            "packed_start_state": packed_start, "source_H_BG": H,
            "source_gradient": source_gradient, "initial_metric": metric,
            "singular_values": singular_values, "rank": rank,
            "rhs_source_vectors": source_rhs_vectors,
            "rhs_source_signals": source_signals, "rhs_abs_floor": rhs_floors,
            "observation_path": obs_path, "descriptor_arrays": descriptor_data["arrays"]}


def run_rotating_gate(exe: Path, outdir: Path, report: dict, case: dict) -> dict:
    state_path = outdir / "rotating_replay_gate.initial_state.txt"
    driver.write_vector(state_path, case["packed_start_state"])
    output = outdir / "rotating_replay_gate.csv"
    command = [str(exe), *map(str, GRID), str(output), "--physical-wave-inverse",
               str(state_path), str(case["observation_path"]), "16", str(DT),
               "--newton-tol", "1e-14", "--krylov-tol", "1e-12", "--replay-smoke",
               *ROTATING_ARGS]
    yi.run_timed(label="rotating_replay_gate", command=command, output=output,
                 outdir=outdir, report=report, initial_state=state_path)
    meta, arrays, _, text = inverse.read_payload(output)
    profile = require_rotating_profile(meta, text, case["f"])
    gate = driver.validate_replay_gate(output, driver.sha(exe), exe,
                                       case["packed_start_state"], case["descriptor_arrays"])
    return {**gate, "rotating_profile": profile}


def evaluate_rotating(exe: Path, outdir: Path, inputs: Path, case: dict,
                      center_arrays: dict | None, report: dict,
                      delta: np.ndarray, label: str, gradient: bool) -> dict:
    meta, arrays, scalars, text = driver.invoke_native(
        exe=exe, outdir=outdir, inputs=inputs, state=case["base_state"],
        basis=case["basis"], delta=delta, label=label, gradient=gradient,
        center_arrays=center_arrays, report=report, native_args=ROTATING_ARGS)
    require_rotating_profile(meta, text, case["f"])
    report["calls"][-1]["rotating_profile"] = case["f"]
    report["calls"][-1].update(stdout_log=f"{label}.stdout.log",
                               stderr_log=f"{label}.stderr.log")
    driver.write_json(outdir / "report.json", report)
    projected = case["basis"].T @ arrays["initial_pullback"] if gradient else None
    return {"label": label, "delta": np.asarray(delta, dtype=np.float64),
            "arrays": arrays, "scalars": scalars, "text": text,
            "objective": float(scalars["objective_physical_w"]),
            "gradient": projected,
            "gradient_l2": float(np.linalg.norm(projected)) if gradient else None}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--outdir", type=Path, required=True)
    parser.add_argument("--preflight-only", action="store_true",
                        help="run descriptor/source checks, replay gate, and RHS probes only")
    args = parser.parse_args()
    exe, outdir = args.exe.resolve(), args.outdir.resolve()
    if not exe.is_file() or (outdir.exists() and any(outdir.iterdir())):
        parser.error("--exe must exist and --outdir must be new or empty")
    outdir.mkdir(parents=True, exist_ok=True)
    (outdir / "inputs").mkdir(parents=True, exist_ok=True)
    report_path = outdir / "report.json"
    report = {"schema": "source-generated-rotating-wave-inverse-v1",
              "status": "preflight", "grid": list(GRID), "ny": NY,
              "steps": STEPS, "dt": DT, "sigma": SIGMA,
              "times": list(TIMES), "target_f": TARGET_F,
              "truth_controls": TRUTH_CONTROLS.tolist(),
              "starting_controls": START_CONTROLS.tolist(),
              "w_amplitude": W_AMPLITUDE,
              "rhs_direction_scale": yi.RHS_DIRECTION_SCALE,
              "rhs_centered_difference_widths": list(yi.RHS_WIDTHS),
              "rhs_budget": {
                  "relative_l2_tolerance": yi.RHS_RELATIVE_L2_TOLERANCE,
                  "combination_rule": "max(relative_l2_tolerance*source_signal_l2, fixed_FP64_floor_l2)",
                  "floor_formula": "64*eps64*predeclared_block_scale*sqrt(packed_block_length)",
                  "block_scales": yi.RHS_BLOCK_SCALES,
                  "floor_type": "engineering floor, not a formal bound"},
              "max_updates": yi.MAX_UPDATES, "max_backtracks": yi.MAX_BACKTRACKS,
              "armijo": yi.ARMIJO, "gradient_threshold": yi.GRADIENT_THRESHOLD,
              "calls": [], "optimizer_run": False,
              "source_truth_scope": "linear 484-component full-Y f-plane source dynamics with PH-aware W sampling; not a nonlinear native trajectory",
              "formal_Eg_bound": False, "full_state_stationarity_checked": False,
              "convergence_claim": "none", "platform": platform.platform(),
              "python": platform.python_version(), "numpy": np.__version__,
              "source": source_receipt(exe)}
    driver.write_json(report_path, report)
    try:
        descriptor_data = descriptor(exe, outdir, report)
        case = make_case(descriptor_data, outdir)
        report.update(status="rotating_source_case_ready",
            descriptor_csv_sha256=driver.sha(descriptor_data["path"]),
            rotating_profile=descriptor_data["rotating_profile"],
            source_symbol_validation=descriptor_data["source_symbols"],
            input_sha256={name: driver.sha(outdir / "inputs" / name) for name in
                          ("initial_state.txt", "observations.txt", "canonical_basis.npy",
                           "source_H_BG.npy")},
            source_observation_xyz=case["xyz"].tolist(),
            source_observations=case["observations"].tolist(),
            f0_lift=case["seed"]["f0_lift"],
            uniform_energy_identity=case["energy_identity"],
            rotating_mode={"eigenvalue_real": float(case["mode"]["eigenvalue"].real),
                           "eigenvalue_imag": float(case["mode"]["eigenvalue"].imag),
                           "angular_frequency": case["mode"]["angular_frequency"],
                           "period_s": float(2.0*np.pi/case["mode"]["angular_frequency"]),
                           "overlap_with_f0_seed": case["mode"]["overlap_with_f0_seed"],
                           "nmax": case["mode"]["nmax"],
                           "near_zero_eigenvalue_count": case["mode"]["near_zero_eigenvalue_count"],
                           "eigenpair_relative_residual_by_block": case["mode"]["eigenpair_relative_residual_by_block"]},
            quadrature_metadata=case["quadrature_metadata"],
            source_truth_state=[[float(v.real), float(v.imag)] for v in case["truth_state"]],
            B_shape=list(case["basis"].shape), B_sha256=inverse.digest(case["basis"]),
            source_H_BG_shape=list(case["source_H_BG"].shape),
            source_H_BG_rank=case["rank"],
            source_H_BG_singular_values=case["singular_values"].tolist(),
            source_H_BG_sha256=driver.sha(outdir / "inputs" / "source_H_BG.npy"),
            source_initial_metric_definition="inverse[(H_BG/sigma).T@(H_BG/sigma)]",
            source_initial_metric=case["initial_metric"].tolist(),
            source_initial_gradient=case["source_gradient"].tolist(),
            local_chart="native_initial=descriptor_base+B@absolute_control_coefficients",
            literal_base_state_sha256=inverse.digest(case["base_state"]),
            packed_start_state_sha256=inverse.digest(case["packed_start_state"]),
            predeclared_rhs_floor_l2_by_block=case["rhs_abs_floor"],
            predeclared_rhs_signal_l2_by_quadrature=case["rhs_source_signals"])
        driver.write_json(report_path, report)

        # Keep the same-executable short gate and RHS comparisons ahead of any
        # full 960-step inverse call.
        gate = run_rotating_gate(exe, outdir, report, case)
        report["same_executable_replay_gate"] = gate
        rhs = yi.run_quadratic_probe(exe, outdir, report, case,
                                     native_args=ROTATING_ARGS,
                                     pack_fn=lambda column, q, ny:
                                     rotating.pack_native_state(column, q, ny=ny))
        report["native_source_rhs_comparison"] = {
            str(j): {"direction_scale": record["direction_scale"],
                     "direction_w_max_abs": record["direction_w_max_abs"],
                     "adjacent_width_drift_by_block": record["adjacent_width_drift_by_block"],
                     "levels": [{"width_multiplier": level["width_multiplier"],
                                 "direction_scale": level["direction_scale"],
                                 "nominal_QA_W_amplitude": level["nominal_QA_W_amplitude"],
                                 "per_block": level["per_block"],
                                 "v_wall_rhs_max_abs": level["v_wall_rhs_max_abs"],
                                 "v_wall_plus_max_abs": level["v_wall_plus_max_abs"],
                                 "v_wall_minus_max_abs": level["v_wall_minus_max_abs"]}
                                for level in record["levels"]]}
            for j, record in rhs["directions"].items()}
        report["rhs_gate_status"] = ("passed" if rhs["all_blocks_within_declared_budget"]
                                      else "failed_declared_per_block_l2_budget")
        driver.write_json(report_path, report)
        if not rhs["all_blocks_within_declared_budget"]:
            raise ValueError("rotating source/native RHS comparison exceeded a fixed block budget")
        if args.preflight_only:
            report.update(status="completed_preflight", optimizer_run=False,
                          total_native_calls=len(report["calls"]),
                          native_executable_sha256=driver.sha(exe),
                          convergence_claim="preflight only; no 960-step VJP or optimization")
            driver.write_json(report_path, report)
            return 0

        # The rotating evaluator adds only the test-only Coriolis profile flag.
        center = evaluate_rotating(exe, outdir, outdir / "inputs", case,
                                   None, report, START_CONTROLS, "gradient_u0", True)
        report = yi.run_controller(exe, outdir, report, case, center,
                                   evaluate_fn=evaluate_rotating)
        report.update(final_delta=report.get("terminal_gradient", {}).get("delta"),
                      final_objective=report.get("terminal_gradient", {}).get("objective"),
                      total_native_calls=len(report["calls"]),
                      native_executable_sha256=driver.sha(exe))
        if report["status"] == "completed_gradient_threshold":
            report["convergence_claim"] = "two-control projected gradient below 1e-5; no formal Eg bound"
        else:
            report["convergence_claim"] = "incomplete bounded actual-cost Armijo run"
        driver.write_json(report_path, report)
        return 0 if report["status"] == "completed_gradient_threshold" else 2
    except Exception as error:
        report.update(status="preflight_or_native_failure", failure_type=type(error).__name__,
                      failure=str(error), convergence_claim="none")
        driver.write_json(report_path, report)
        raise


if __name__ == "__main__":
    raise SystemExit(main())
