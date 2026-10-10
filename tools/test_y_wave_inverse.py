#!/usr/bin/env python3
"""Run a bounded two-control inverse for one source-selected symmetric-Y mode."""
from __future__ import annotations

import argparse
import json
import platform
import subprocess
import sys
import time
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import probe_small_step_inverse as driver  # noqa: E402
import test_fine_wave_inverse as inverse  # noqa: E402
import wave_y_reference as ywave  # noqa: E402

GRID, STEPS, DT, SIGMA = (16, 12, 8), 960, 0.3125, 3.0e-4
TIMES, W_AMPLITUDE = (150.0, 300.0), 0.01
TRUTH_CONTROLS = np.array([0.78, -0.36], dtype=np.float64)
START_CONTROLS = np.array([0.25, 0.15], dtype=np.float64)
RHS_DIRECTION_SCALE = 1.0e-3
RHS_WIDTHS = (1.0, 0.5, 0.25)
GRADIENT_THRESHOLD = 1.0e-5
MAX_UPDATES, MAX_BACKTRACKS, ARMIJO = 3, 4, 1.0e-4
RHS_RELATIVE_L2_TOLERANCE = 1.0e-5
RHS_BLOCK_UNITS = {"u": "m s-2", "v": "m s-2", "w": "m s-2",
                   "ph": "m2 s-3", "theta": "K s-1", "mu": "Pa s-1"}
RHS_BLOCK_SCALES = {"u": 1.0, "v": 1.0, "w": 1.0,
                    "ph": 100.0, "theta": 1.0, "mu": 100.0}


def source_receipt(exe: Path) -> dict:
    names = (
        "tools/test_y_wave_inverse.py", "tools/probe_small_step_inverse.py",
        "tools/test_fine_wave_inverse.py", "tools/wave_quadratic_reference.py",
        "tools/wave_y_reference.py",
        "tools/wave_energy_spatial_reference.py", "tools/wave_quadratic_transport.py",
        "external/libtorch_wrf/sdirk3/tests/test_native_wave_refinement.cpp",
        "external/libtorch_wrf/sdirk3/wrf_sdirk3_newton_solver.h",
        "external/libtorch_wrf/sdirk3/wrf_sdirk3_newton_solver.cpp",
        "external/libtorch_wrf/sdirk3/wrf_sdirk3_config.h",
        "external/libtorch_wrf/sdirk3/wrf_sdirk3_tile_unified.h",
        "external/libtorch_wrf/sdirk3/wrf_sdirk3_tile_unified_impl.cpp",
    )
    return {"revision": subprocess.check_output(
                ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
            "executable_sha256": driver.sha(exe),
            "sha256": {name: driver.sha(ROOT / name) for name in names}}


def run_timed(*, label: str, command: list[str], output: Path,
              outdir: Path, report: dict, initial_state: Path | None = None,
              input_file: Path | None = None) -> tuple:
    usage = outdir / f"{label}.time.txt"
    timed = ["/usr/bin/time", "-v", "-o", str(usage), *command]
    report["active_call"] = {"label": label, "command": command,
                             "timed_command": timed,
                             "initial_state_sha256": driver.sha(initial_state) if initial_state else None}
    driver.write_json(outdir / "report.json", report)
    started = time.monotonic()
    result = subprocess.run(timed, capture_output=True, text=True)
    elapsed = time.monotonic() - started
    (outdir / f"{label}.stdout.log").write_text(result.stdout)
    (outdir / f"{label}.stderr.log").write_text(result.stderr)
    usage_text = usage.read_text() if usage.is_file() else ""
    row = {"label": label, "command": command, "timed_command": timed,
           "return_code": result.returncode, "elapsed_seconds_monotonic": elapsed,
           "process_elapsed_wall": driver.parse_time_value(
               usage_text, "Elapsed (wall clock) time (h:mm:ss or m:ss)"),
           "peak_rss_kib": driver.parse_time_value(
               usage_text, "Maximum resident set size (kbytes)"),
           "time_report": usage.name, "stdout_log": f"{label}.stdout.log",
           "stderr_log": f"{label}.stderr.log",
           "output_file": output.name,
           "output_sha256": driver.sha(output) if output.is_file() else None}
    if initial_state:
        row.update(initial_state_file=initial_state.name,
                   initial_state_sha256=driver.sha(initial_state))
    if input_file:
        row.update(input_file=input_file.name, input_sha256=driver.sha(input_file))
    report["calls"].append(row)
    report["active_call"] = None
    driver.write_json(outdir / "report.json", report)
    if result.returncode:
        raise RuntimeError(f"native call {label} failed with rc={result.returncode}")
    if not output.is_file():
        raise RuntimeError(f"native call {label} produced no output CSV")
    return inverse.read_payload(output)


def write_observations(path: Path, xyz: np.ndarray, targets: np.ndarray) -> None:
    if xyz.shape != (105, 3) or targets.shape != (2, 105):
        raise ValueError("source observation arrays must be XYZ=(105,3), targets=(2,105)")
    with path.open("w") as stream:
        stream.write("PHYSICAL_WAVE_OBSERVATIONS_V1\n105\n")
        for point, w150, w300 in zip(xyz, targets[0], targets[1]):
            stream.write(" ".join(f"{value:.17g}" for value in (*point, w150, w300)) + "\n")


def require_inactive_extra_physics(meta: dict) -> None:
    # The Rayleigh coefficient/depth retain defaults; damp_opt=0 disables
    # their actual RHS path. Diffusion option 2 has zero active coefficients.
    expected = {"f_input_max_abs": 0.0, "e_input_max_abs": 0.0,
                "wrf_w_damping_config": 0.0, "wrf_damp_opt_config": 0.0,
                "diffusion_option_config": 2.0,
                "khdif_config": 0.0, "kvdif_config": 0.0}
    bad = {k: (meta.get(k), value) for k, value in expected.items()
           if meta.get(k) != value}
    if bad:
        raise ValueError(f"Y-wave inactive-physics profile mismatch: {bad}")


def descriptor(exe: Path, outdir: Path, report: dict) -> dict:
    original = inverse.native_call

    def timed_descriptor_call(executable, nx, ny, nz, output, native_args):
        command = [str(executable), str(nx), str(ny), str(nz), str(output), *native_args]
        return run_timed(label="descriptor", command=command, output=output,
                         outdir=outdir, report=report)

    inverse.native_call = timed_descriptor_call
    try:
        data = inverse.descriptor(exe, outdir, GRID)
        require_inactive_extra_physics(data["meta"])
        return data
    finally:
        inverse.native_call = original


def make_case(descriptor_data: dict, outdir: Path) -> dict:
    column = descriptor_data["column"]
    symbols = ywave.verify_y_staggered_symbols(12, float(column["ly"]))
    reduction = ywave.verify_ky_zero_reduction(column, 12)
    if not symbols["passed"] or not reduction["exact"]:
        raise ValueError("source Y symbol or exact ky=0 reduction preflight failed")
    matrix = ywave.source_y_matrix(column, ny=12)
    mode = ywave.select_internal_mode(matrix, column)
    quadratures, quadrature_info = ywave.standing_quadratures(mode, w_amplitude=W_AMPLITUDE)
    xyz = ywave.source_observation_xyz(column)
    truth_q = TRUTH_CONTROLS[0] * quadratures[0] + TRUTH_CONTROLS[1] * quadratures[1]
    observations = ywave.source_observations(
        column, truth_q, 12, xyz, times=TIMES, linear_geometry=False)
    B = np.column_stack([ywave.pack_source_mode(column, q, 12) for q in quadratures])
    base_state = descriptor_data["base"]
    packed_start_state = base_state + B @ START_CONTROLS
    H = ywave.quadrature_observation_matrix(column, quadratures, 12, xyz, times=TIMES)
    if (B.shape != (8480, 2) or not np.isfinite(B).all() or
            not np.isfinite(packed_start_state).all() or observations.shape != (2, 105) or
            not np.isfinite(observations).all()):
        raise ValueError("source mode, packed basis, or generated observations are invalid")
    v_slice = block_slices()["v"]
    for control in range(2):
        v = B[v_slice, control].reshape(13, 8, 16)
        if np.count_nonzero(v[0]) or np.count_nonzero(v[-1]):
            raise ValueError("packed V control is nonzero at a symmetric Y wall")
    scaled_H = H / SIGMA
    singular_values = np.linalg.svd(scaled_H, compute_uv=False)
    rank = int(np.linalg.matrix_rank(scaled_H))
    gram = scaled_H.T @ scaled_H
    if rank != 2 or singular_values[-1] <= 0.0:
        raise ValueError(f"source H_BG lacks two-control observability: sv={singular_values.tolist()}")
    np.linalg.cholesky(gram)
    initial_prediction = H @ START_CONTROLS
    fixed_residual_source = initial_prediction - observations.reshape(-1)
    source_gradient = H.T @ fixed_residual_source / SIGMA**2
    metric = np.linalg.solve(gram, np.eye(2, dtype=np.float64))
    slices = block_slices()
    rhs_abs_floor = {name: float(64.0*np.finfo(np.float64).eps*RHS_BLOCK_SCALES[name]*
                                  np.sqrt(slc.stop-slc.start))
                     for name, slc in slices.items()}
    rhs_source_vectors, rhs_source_signals = {}, {}
    for j, q in enumerate(quadratures):
        scaled_q = RHS_DIRECTION_SCALE*q
        rhs_vector = ywave.pack_source_mode(
            column, ywave.source_y_rhs(column, scaled_q, 12), 12)
        rhs_source_vectors[str(j)] = rhs_vector
        rhs_source_signals[str(j)] = {name: float(np.linalg.norm(rhs_vector[slc]))
                                      for name, slc in slices.items()}
    observation_path = outdir / "inputs" / "observations.txt"
    observation_path.parent.mkdir(parents=True, exist_ok=True)
    write_observations(observation_path, xyz, observations)
    np.save(outdir / "inputs" / "canonical_basis.npy", B, allow_pickle=False)
    np.save(outdir / "inputs" / "source_H_BG.npy", H, allow_pickle=False)
    driver.write_vector(outdir / "inputs" / "initial_state.txt", packed_start_state)
    return {"column": column, "matrix": matrix, "mode": mode,
            "quadratures": quadratures, "quadrature_info": quadrature_info,
            "xyz": xyz, "observations": observations, "basis": B,
            "initial_state": base_state, "source_H_BG": H,
            "source_gradient": source_gradient, "initial_metric": metric,
            "singular_values": singular_values, "rank": rank,
            "rhs_abs_floor": rhs_abs_floor,
            "rhs_source_vectors": rhs_source_vectors,
            "rhs_source_signals": rhs_source_signals,
            "symbols": symbols, "ky0_reduction": reduction,
            "observation_path": observation_path,
            "truth_source_state": truth_q,
            "packed_start_state": packed_start_state}


def run_gate(exe: Path, outdir: Path, report: dict, case: dict,
             descriptor_arrays: dict) -> dict:
    state_path = outdir / "replay_gate.initial_state.txt"
    driver.write_vector(state_path, case["initial_state"] + case["basis"] @ START_CONTROLS)
    output = outdir / "replay_gate.csv"
    command = [str(exe), *map(str, GRID), str(output), "--physical-wave-inverse",
               str(state_path), str(case["observation_path"]), "16", str(DT),
               "--newton-tol", "1e-14", "--krylov-tol", "1e-12", "--replay-smoke"]
    run_timed(label="replay_gate", command=command, output=output, outdir=outdir,
              report=report, initial_state=state_path)
    gate = driver.validate_replay_gate(output, driver.sha(exe), exe,
                                       case["packed_start_state"], descriptor_arrays)
    report["same_executable_replay_gate"] = gate
    driver.write_json(outdir / "report.json", report)
    return gate


def block_slices() -> dict:
    ny, nx, nz = GRID[1], GRID[0], GRID[2]
    sizes = {"u": ny*nz*(nx+1), "v": (ny+1)*nz*nx,
             "w": ny*(nz+1)*nx, "ph": ny*(nz+1)*nx,
             "theta": ny*nz*nx, "mu": ny*nx}
    result, offset = {}, 0
    for name, size in sizes.items():
        result[name] = slice(offset, offset+size)
        offset += size
    if offset != 8480:
        raise AssertionError("packed native block sizes do not total 8480")
    return result


def run_quadratic_probe(exe: Path, outdir: Path, report: dict,
                        case: dict) -> dict:
    slices = block_slices()
    rows = {}
    for j, q in enumerate(case["quadratures"]):
        small_q = RHS_DIRECTION_SCALE * q
        direction = ywave.pack_source_mode(case["column"], small_q, 12)
        direction_path = outdir / f"rhs_direction_{j}.txt"
        driver.write_vector(direction_path, direction)
        output = outdir / f"rhs_quadrature_{j}.csv"
        command = [str(exe), *map(str, GRID), str(output),
                   "--quadratic-probe", str(direction_path)]
        _, arrays, _, _ = run_timed(label=f"rhs_quadrature_{j}", command=command,
                                    output=output, outdir=outdir, report=report,
                                    input_file=direction_path)
        expected = case["rhs_source_vectors"][str(j)]
        v_slice = slices["v"]
        v_direction = direction[v_slice].reshape(13, 8, 16)
        if np.count_nonzero(v_direction[0]) or np.count_nonzero(v_direction[-1]):
            raise ValueError("quadratic probe direction has nonzero V walls")
        levels = []
        for level, width in enumerate(RHS_WIDTHS):
            plus, minus = arrays[f"rhs_plus_{level}"], arrays[f"rhs_minus_{level}"]
            if (plus.shape != (8480,) or minus.shape != (8480,) or
                    not np.isfinite(plus).all() or not np.isfinite(minus).all()):
                raise ValueError("quadratic RHS probe returned an unexpected packed size")
            centered = (plus - minus) / (2.0 * width)
            per_block = {}
            for name, block in slices.items():
                error = centered[block] - expected[block]
                signal_l2 = float(np.linalg.norm(expected[block]))
                difference_l2 = float(np.linalg.norm(error))
                # Fixed block scales and FP64 floors are precomputed from the
                # source case, before either native RHS sample is evaluated.
                fp64_floor = case["rhs_abs_floor"][name]
                below_floor = signal_l2 <= fp64_floor
                relative_allowance = (0.0 if below_floor else
                                      RHS_RELATIVE_L2_TOLERANCE*signal_l2)
                budget = max(relative_allowance, fp64_floor)
                engineering_floor_active = fp64_floor >= relative_allowance
                per_block[name] = {
                    "unit": RHS_BLOCK_UNITS[name], "source_signal_l2": signal_l2,
                    "difference_l2": difference_l2,
                    "fp64_engineering_floor_l2": float(fp64_floor),
                    "relative_l2_allowance": relative_allowance,
                    "total_l2_budget": float(budget),
                    "source_signal_below_floor": bool(below_floor),
                    "engineering_floor_active": bool(engineering_floor_active),
                    "relative_budget_active": bool(not engineering_floor_active),
                    "relative_l2_error": (difference_l2/signal_l2
                                           if signal_l2 > fp64_floor else None),
                    "max_abs_error": float(np.max(np.abs(error))),
                    "passed": bool(difference_l2 <= budget),
                    "budget_is_formal_bound": False}
            v_centered = centered[v_slice].reshape(13, 8, 16)
            v_plus = plus[v_slice].reshape(13, 8, 16)
            v_minus = minus[v_slice].reshape(13, 8, 16)
            wall_max = float(max(np.max(np.abs(v_centered[0])),
                                 np.max(np.abs(v_centered[-1]))))
            wall_plus = float(max(np.max(np.abs(v_plus[0])), np.max(np.abs(v_plus[-1]))))
            wall_minus = float(max(np.max(np.abs(v_minus[0])), np.max(np.abs(v_minus[-1]))))
            if wall_max != 0.0 or wall_plus != 0.0 or wall_minus != 0.0:
                raise ValueError(f"native centered V RHS is not zero at Y walls: {wall_max}")
            levels.append({"width_multiplier": width,
                           "direction_scale": RHS_DIRECTION_SCALE*width,
                           "nominal_QA_W_amplitude": W_AMPLITUDE*RHS_DIRECTION_SCALE*width,
                           "per_block": per_block,
                           "v_wall_rhs_max_abs": wall_max,
                           "v_wall_plus_max_abs": wall_plus,
                           "v_wall_minus_max_abs": wall_minus,
                           "centered_rhs": centered})
        drift = {}
        for level in range(2):
            per_block = {}
            change = levels[level]["centered_rhs"] - levels[level+1]["centered_rhs"]
            for name, block in slices.items():
                signal = float(np.linalg.norm(expected[block]))
                floor = case["rhs_abs_floor"][name]
                change_l2 = float(np.linalg.norm(change[block]))
                per_block[name] = {
                    "l2_change": change_l2,
                    "relative_change": change_l2/signal if signal > floor else None,
                    "source_signal_below_floor": bool(signal <= floor),
                    "engineering_floor_l2": floor,
                }
            drift[f"level_{level}_to_{level+1}"] = per_block
        rows[str(j)] = {
            "direction_scale": RHS_DIRECTION_SCALE,
            "direction_w_max_abs": float(np.max(np.abs(direction[slices["w"]]))),
            "source_rhs": expected, "levels": levels,
            "adjacent_width_drift_by_block": drift,
            "all_blocks_within_declared_budget": all(
                level["per_block"][name]["passed"]
                for level in levels for name in slices)}
    return {"directions": rows, "block_slices": slices,
            "all_blocks_within_declared_budget": all(
                record["all_blocks_within_declared_budget"] for record in rows.values())}


def same_trial_and_gradient(trial: dict, gradient: dict) -> None:
    if not np.array_equal(np.asarray(trial["delta"], dtype=np.float64),
                          np.asarray(gradient["delta"], dtype=np.float64)):
        raise ValueError("accepted forward and gradient chart points differ")
    for t in TIMES:
        for key in (f"predicted_{int(t)}", f"checkpoint_{int(t)}", f"bracket_index_{int(t)}"):
            if not np.array_equal(trial["arrays"][key], gradient["arrays"][key]):
                raise ValueError(f"accepted forward/gradient output mismatch: {key}")
    if not np.isclose(trial["objective"], gradient["objective"], rtol=0.0, atol=1e-10):
        raise ValueError("accepted forward and gradient objectives differ")


def evaluate(exe: Path, outdir: Path, inputs: Path, case: dict,
             center_arrays: dict | None, report: dict, delta: np.ndarray,
             label: str, gradient: bool) -> dict:
    meta, arrays, scalars, text = driver.invoke_native(
        exe=exe, outdir=outdir, inputs=inputs, state=case["initial_state"],
        basis=case["basis"], delta=delta, label=label, gradient=gradient,
        center_arrays=center_arrays, report=report)
    require_inactive_extra_physics(meta)
    report["calls"][-1].update(stdout_log=f"{label}.stdout.log",
                               stderr_log=f"{label}.stderr.log")
    driver.write_json(outdir / "report.json", report)
    projected = case["basis"].T @ arrays["initial_pullback"] if gradient else None
    return {"label": label, "delta": np.asarray(delta, dtype=np.float64),
            "arrays": arrays, "scalars": scalars, "text": text,
            "objective": float(scalars["objective_physical_w"]),
            "gradient": projected,
            "gradient_l2": float(np.linalg.norm(projected)) if gradient else None}


def run_controller(exe: Path, outdir: Path, report: dict, case: dict,
                   center: dict) -> dict:
    delta = START_CONTROLS.copy()
    H = case["initial_metric"].copy()
    report["source_gradient_initial"] = case["source_gradient"].tolist()
    report["native_gradient_initial"] = center["gradient"].tolist()
    report["initial_source_native_gradient_difference_l2"] = float(
        np.linalg.norm(center["gradient"]-case["source_gradient"]))
    report["optimizer_run"] = False
    report["updates"] = []
    current = center
    report["terminal_gradient"] = {"call_label": current["label"],
        "delta": current["delta"].tolist(), "objective": current["objective"],
        "gradient": current["gradient"].tolist(),
        "gradient_l2": float(np.linalg.norm(current["gradient"])),
        "threshold": GRADIENT_THRESHOLD,
        "reached": float(np.linalg.norm(current["gradient"])) < GRADIENT_THRESHOLD}
    for update_index in range(MAX_UPDATES):
        g = current["gradient"]
        if float(np.linalg.norm(g)) < GRADIENT_THRESHOLD:
            report.update(status="completed_gradient_threshold", terminal_gradient={
                "call_label": current["label"], "delta": current["delta"].tolist(),
                "objective": current["objective"], "gradient": g.tolist(),
                "gradient_l2": float(np.linalg.norm(g)),
                "threshold": GRADIENT_THRESHOLD, "reached": True})
            return report
        direction = -(H @ g)
        slope = float(g @ direction)
        update = {"update": update_index+1, "delta": current["delta"].tolist(),
                  "objective": current["objective"], "gradient": g.tolist(),
                  "gradient_l2": float(np.linalg.norm(g)),
                  "direction": direction.tolist(), "direction_norm": float(np.linalg.norm(direction)),
                  "gradient_slope": slope, "line_search": []}
        report["updates"].append(update)
        report["optimizer_run"] = True
        if not np.isfinite(slope) or slope >= 0.0:
            update["decision"] = "incomplete_non_descent_direction"
            break
        accepted = None
        for backtrack in range(MAX_BACKTRACKS):
            alpha = 0.5**backtrack
            trial_delta = current["delta"] + alpha*direction
            label = f"trial_u{update_index}_b{backtrack}"
            try:
                trial = evaluate(exe, outdir, outdir/"inputs", case, center["arrays"],
                                 report, trial_delta, label, False)
            except (driver.BracketMismatchError, driver.AdmissibilityGuardError) as error:
                update["line_search"].append({"alpha": alpha, "accepted": False,
                                              "reason": str(error),
                                              "decision": type(error).__name__})
                driver.write_json(outdir / "report.json", report)
                continue
            bound = current["objective"] + ARMIJO*alpha*slope
            take = trial["objective"] < current["objective"] and trial["objective"] <= bound
            update["line_search"].append({"alpha": alpha, "delta": trial_delta.tolist(),
                                           "objective": trial["objective"],
                                           "armijo_bound": bound, "accepted": bool(take),
                                           "call_label": label})
            driver.write_json(outdir / "report.json", report)
            if take:
                accepted = trial
                update.update(decision="accepted", accepted_delta=trial_delta.tolist(),
                              accepted_objective=trial["objective"], alpha=alpha)
                break
        if accepted is None:
            update["decision"] = "incomplete_no_accepted_actual_cost_step"
            break
        fresh = evaluate(exe, outdir, outdir/"inputs", case, center["arrays"],
                         report, accepted["delta"], f"gradient_u{update_index+1}", True)
        same_trial_and_gradient(accepted, fresh)
        s = fresh["delta"] - current["delta"]
        y = fresh["gradient"] - current["gradient"]
        H, bfgs = inverse.safe_bfgs_inverse_update(H, s, y)
        update["secant_update"] = bfgs
        current = fresh
        report["terminal_gradient"] = {"call_label": current["label"],
            "delta": current["delta"].tolist(), "objective": current["objective"],
            "gradient": current["gradient"].tolist(),
            "gradient_l2": current["gradient_l2"], "threshold": GRADIENT_THRESHOLD,
            "reached": current["gradient_l2"] < GRADIENT_THRESHOLD}
        driver.write_json(outdir / "report.json", report)
    if report.get("terminal_gradient", {}).get("reached"):
        report["status"] = "completed_gradient_threshold"
    else:
        report["status"] = "incomplete_update_limit_or_line_search"
        report["terminal_gradient"] = {"call_label": current["label"],
            "delta": current["delta"].tolist(), "objective": current["objective"],
            "gradient": current["gradient"].tolist(),
            "gradient_l2": float(np.linalg.norm(current["gradient"])),
            "threshold": GRADIENT_THRESHOLD, "reached": False}
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--outdir", type=Path, required=True)
    parser.add_argument("--preflight-only", action="store_true",
                        help="run descriptor, source RHS probes, and replay gate only")
    args = parser.parse_args()
    exe, outdir = args.exe.resolve(), args.outdir.resolve()
    if not exe.is_file() or (outdir.exists() and any(outdir.iterdir())):
        parser.error("--exe must exist and --outdir must be new or empty")
    outdir.mkdir(parents=True, exist_ok=True)
    (outdir/"inputs").mkdir(parents=True, exist_ok=True)
    report_path = outdir/"report.json"
    report = {"schema": "source-generated-y-wave-inverse-v1", "status": "preflight",
              "grid": list(GRID), "steps": STEPS, "dt": DT, "sigma": SIGMA,
              "times": list(TIMES), "truth_controls": TRUTH_CONTROLS.tolist(),
              "starting_controls": START_CONTROLS.tolist(), "w_amplitude": W_AMPLITUDE,
              "rhs_direction_scale": RHS_DIRECTION_SCALE,
              "rhs_nominal_QA_W_amplitudes": [
                  W_AMPLITUDE*RHS_DIRECTION_SCALE*width for width in RHS_WIDTHS],
              "rhs_centered_difference_widths": list(RHS_WIDTHS),
              "rhs_budget": {
                  "relative_l2_tolerance": RHS_RELATIVE_L2_TOLERANCE,
                  "combination_rule": "max(relative_l2_tolerance*source_signal_l2, fixed_FP64_floor_l2)",
                  "floor_formula": "64*eps64*predeclared_block_scale*sqrt(packed_block_length)",
                  "block_scales": RHS_BLOCK_SCALES,
                  "below_floor_rule": "if source signal <= floor, report absolute error and gate against floor",
                  "floor_type": "engineering floor",
                  "floor_scope": "predeclared FP64 scale floor; not a formal bound; excludes RHS evaluation/model error"},
              "max_updates": MAX_UPDATES, "max_backtracks": MAX_BACKTRACKS,
              "armijo": ARMIJO, "gradient_threshold": GRADIENT_THRESHOLD,
              "calls": [], "optimizer_run": False,
              "source_truth_scope": "linear 41-component source Y dynamics with PH-perturbed W-height sampling; not a nonlinear native trajectory",
              "convergence_claim": "none", "formal_Eg_bound": False,
              "full_state_stationarity_checked": False,
              "platform": platform.platform(), "python": platform.python_version(),
              "numpy": np.__version__, "source": source_receipt(exe)}
    driver.write_json(report_path, report)
    try:
        descriptor_data = descriptor(exe, outdir, report)
        case = make_case(descriptor_data, outdir)
        report.update(status="source_case_ready",
            descriptor_csv_sha256=driver.sha(descriptor_data["path"]),
            input_sha256={name: driver.sha(outdir/"inputs"/name) for name in
                          ("initial_state.txt", "observations.txt", "canonical_basis.npy",
                           "source_H_BG.npy")},
            source_observation_xyz=case["xyz"].tolist(),
            source_observations=case["observations"].tolist(),
            source_mode={"eigenvalue_real": float(case["mode"]["eigenvalue"].real),
                         "eigenvalue_imag": float(case["mode"]["eigenvalue"].imag),
                         "angular_frequency": case["mode"]["angular_frequency"],
                         "period_s": case["mode"]["period_s"],
                         "background_nmax": case["mode"]["background_nmax"],
                         "eigenpair_relative_residual_by_block": case["mode"]["eigenpair_relative_residual_by_block"]},
            quadrature_metadata=case["quadrature_info"],
            y_symbol_check=case["symbols"], ky_zero_reduction=case["ky0_reduction"],
            B_shape=list(case["basis"].shape), B_sha256=inverse.digest(case["basis"]),
            source_H_BG_shape=list(case["source_H_BG"].shape),
            source_H_BG_rank=case["rank"],
            source_H_BG_singular_values=case["singular_values"].tolist(),
            source_initial_metric_definition="inverse[(H_BG/sigma).T@(H_BG/sigma)]",
            source_initial_metric=case["initial_metric"].tolist(),
            local_chart="native_initial=descriptor_base+B@absolute_control_coefficients",
            source_initial_gradient=case["source_gradient"].tolist(),
            source_truth_state=[[float(value.real), float(value.imag)]
                                for value in case["truth_source_state"]],
            literal_base_state_sha256=inverse.digest(case["initial_state"]),
            packed_start_state_sha256=inverse.digest(case["packed_start_state"]))
        report["rhs_budget"].update(
            predeclared_absolute_floor_l2_by_block=case["rhs_abs_floor"],
            source_signal_l2_by_quadrature=case["rhs_source_signals"],
            frozen_before_native_rhs_samples=True)
        driver.write_json(report_path, report)

        gate = run_gate(exe, outdir, report, case, descriptor_data["arrays"])
        rhs = run_quadratic_probe(exe, outdir, report, case)
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
            raise ValueError("native/source centered RHS comparison exceeded a declared block budget")
        if args.preflight_only:
            report.update(status="completed_preflight", optimizer_run=False,
                          total_native_calls=len(report.get("calls", [])),
                          native_executable_sha256=driver.sha(exe),
                          convergence_claim="preflight only; no 960-step VJP or optimization call")
            driver.write_json(report_path, report)
            return 0
        center = evaluate(exe, outdir, outdir/"inputs", case,
                          None, report,
                          START_CONTROLS, "gradient_u0", True)
        if not np.allclose(center["gradient"], case["source_gradient"],
                           rtol=1e-5, atol=1e-7):
            report["source_native_initial_gradient_comparison"] = {
                "native": center["gradient"].tolist(),
                "source": case["source_gradient"].tolist(),
                "difference_l2": float(np.linalg.norm(center["gradient"]-case["source_gradient"]))}
        report = run_controller(exe, outdir, report, case, center)
        report.update(final_delta=report.get("terminal_gradient", {}).get("delta"),
                      final_objective=report.get("terminal_gradient", {}).get("objective"),
                      total_native_calls=len(report.get("calls", [])),
                      native_executable_sha256=driver.sha(exe))
        if report["status"] == "completed_gradient_threshold":
            report["convergence_claim"] = "two-control projected gradient below 1e-5; no full Eg bound"
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
