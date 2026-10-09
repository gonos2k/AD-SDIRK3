#!/usr/bin/env python3
"""Run a gated, bounded h=.3125 inverse in the PR282 four-control chart.

The archived component-FD responses define only a fixed Gauss-Newton metric.
Every objective and gradient used by the line search comes from the current
native executable at the literal saved state plus Bcan @ delta.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import platform
import subprocess
import sys
import time
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import test_fine_wave_inverse as inverse  # noqa: E402
import probe_fine_split_pullbacks as split  # noqa: E402

FIXTURE_SHA = "e170be5d1e2b0d87efcdc9ebe0f4dd1d381aceb9e0777dcd074e0dcfe1329d8c"
FD_ZIP_SHA = "a21609ddb1ec0839279dbd712a53fae5bdfd26e426d7570f70fa55158be0d7c8"
FD_EPSILON = 0.0025
GRID = (16, 12, 8)
STEPS = 960
DT = 0.3125
SIGMA = 3.0e-4
TIMES = (150, 300)
CONTEXT = split.CONTEXT_ARRAYS
MAX_UPDATES = 2
MAX_BACKTRACKS = 4
ARMIJO = 1.0e-4
MAX_STEP_NORM = 0.05


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path: Path, value: dict) -> None:
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n")


def write_vector(path: Path, values: np.ndarray) -> None:
    values = np.asarray(values, dtype=np.float64)
    with path.open("w") as stream:
        stream.write(f"{values.size}\n")
        np.savetxt(stream, values, fmt="%.17g")


def load_inputs(fixture_zip: Path, fd_zip: Path, outdir: Path):
    if sha(fixture_zip) != FIXTURE_SHA:
        raise ValueError("returned-state fixture SHA mismatch")
    if sha(fd_zip) != FD_ZIP_SHA:
        raise ValueError("existing component-FD ZIP SHA mismatch")
    inputs = outdir / "inputs"
    inputs.mkdir(parents=True, exist_ok=True)
    import zipfile

    with zipfile.ZipFile(fixture_zip) as archive:
        if archive.testzip() is not None:
            raise ValueError("returned-state fixture ZIP failed CRC verification")
        manifest = json.loads(archive.read("manifest.json"))
        if manifest.get("schema") != "pr282-fixed-literal-incremental-controls-v3":
            raise ValueError("unexpected fixed literal-control fixture schema")
        for name, record in manifest["files"].items():
            raw = archive.read(name)
            if hashlib.sha256(raw).hexdigest() != record["sha256"]:
                raise ValueError(f"fixture member SHA mismatch: {name}")
            (inputs / name).write_bytes(raw)
    state = split.read_literal_vector(inputs / "initial_state.txt")
    if (state.size != 8480 or not np.isfinite(state).all() or
            inverse.digest(state) != manifest.get("initial_array_sha256")):
        raise ValueError("literal saved state failed shape/finite/hash checks")
    basis = np.load(inputs / "canonical_basis.npy", allow_pickle=False)
    if (basis.shape != (8480, 4) or basis.dtype != np.float64 or
            not np.isfinite(basis).all() or
            inverse.digest(basis) != manifest["canonical_basis"]["array_sha256"]):
        raise ValueError("canonical Bcan failed shape/finite/hash checks")
    fd_report, fd_basis, endpoints = split.read_fd_bundle(fd_zip, outdir, state)
    if not np.array_equal(basis, fd_basis):
        raise ValueError("fixture Bcan differs from the Bcan pinned in component-FD ZIP")
    return inputs, manifest, state, basis, fd_report, endpoints


def fixed_metric(endpoints: dict) -> tuple[np.ndarray, dict]:
    # Only the four archived native FD response columns enter this metric.
    columns = []
    for j in range(4):
        response_by_time = []
        plus = endpoints[(FD_EPSILON, j, "plus")]
        minus = endpoints[(FD_EPSILON, j, "minus")]
        for t in TIMES:
            response_by_time.append((plus[f"predicted_{t}"] - minus[f"predicted_{t}"]) /
                                    (2.0 * FD_EPSILON))
        columns.append(np.concatenate(response_by_time))
    A = np.column_stack(columns)
    scaled = A / SIGMA
    G = scaled.T @ scaled
    eig = np.linalg.eigvalsh(G)
    rank = int(np.linalg.matrix_rank(scaled))
    if not np.isfinite(G).all() or rank != 4 or eig[0] <= 0.0:
        raise ValueError(f"fixed FD metric is not SPD/full-rank: rank={rank}, eig={eig.tolist()}")
    return G, {"response_shape": list(A.shape), "rank": rank,
               "eigenvalues": eig.tolist(), "condition_number": float(eig[-1] / eig[0]),
               "source": "existing native component-FD eps=.0025 predictions; preconditioner only",
               "response_sha256": hashlib.sha256(A.tobytes()).hexdigest()}


def parse_time_value(text: str, label: str):
    prefix = label + ":"
    for line in text.splitlines():
        line = line.strip()
        if line.startswith(prefix):
            value = line[len(prefix):].strip()
            try:
                return int(value.replace(",", ""))
            except ValueError:
                return value
    return None


class BracketMismatchError(ValueError):
    pass


class AdmissibilityGuardError(ValueError):
    pass


def validate_replay_gate(path: Path, expected_exe_sha: str, exe: Path,
                         state: np.ndarray, saved_descriptor: dict) -> dict:
    if sha(exe) != expected_exe_sha:
        raise ValueError("short replay gate executable SHA does not match --exe")
    meta, arrays, scalars, text = inverse.read_payload(path)
    required_meta = {"physical_wave_replay_smoke": 1, "replay_block_steps": 8,
                     "replay_steps": 16, "replay_dt_fp32": DT,
                     "replay_steps_checked": 16, "replay_optimizer_run": 0,
                     "replay_gradient_gate_tolerance": 1.0e-8,
                     "newton_tol_config": float(np.float32(1.0e-14)),
                     "krylov_tol_config": float(np.float32(1.0e-12)),
                     "nk_adaptive_tol_config": 1,
                     "ewt_rtol_config": float(np.float32(1.0e-6)),
                     "replay_snapshot_0_stage2_predictor_defined": 1,
                     "replay_snapshot_0_stage3_predictor_defined": 1,
                     "replay_snapshot_8_stage2_predictor_defined": 1,
                     "replay_snapshot_8_stage3_predictor_defined": 1,
                     "replay_block_1_state_digest_matches_snapshot": 1,
                     "replay_block_1_stage2_predictor_restore_match": 1,
                     "replay_block_1_stage3_predictor_restore_match": 1,
                     "replay_block_2_state_digest_matches_snapshot": 1,
                     "replay_block_2_stage2_predictor_restore_match": 1,
                     "replay_block_2_stage3_predictor_restore_match": 1,
                     "physical_state_admissibility_checks": 17,
                     "replay_snapshot_profile_checks": 2,
                     "replay_endpoints_admissible_via_exact_parity": 1}
    mismatch = {key: (meta.get(key), value) for key, value in required_meta.items()
                if meta.get(key) != value}
    required_text = {"replay_baseline_status": "completed",
                     "replay_status": "completed_diagnostic_only",
                     "replay_endpoint_comparison": "bitwise_exact_required",
                     "replay_endpoint_parity": "all_16_bitwise_equal",
                     "replay_gradient_block_gate": "passed",
                     "replay_solver_state_snapshot": "NewtonCarriedState"}
    mismatch.update({key: (text.get(key), value) for key, value in required_text.items()
                    if text.get(key) != value})
    if mismatch:
        raise ValueError(f"short replay gate metadata mismatch: {mismatch}")
    if "initial_state" not in arrays or not np.array_equal(arrays["initial_state"], state):
        raise ValueError("short replay gate used a different literal initial state")
    retained = arrays.get("replay_retained_initial_pullback")
    replay = arrays.get("replay_initial_pullback")
    if (retained is None or replay is None or retained.shape != (8480,) or
            replay.shape != (8480,) or not np.isfinite(retained).all() or
            not np.isfinite(replay).all() or not all(np.isfinite(v) for v in scalars.values())):
        raise ValueError("short replay gate gradients/metrics are missing or nonfinite")
    block_sizes = (1632, 1664, 1728, 1728, 1536, 192)
    block_rows = []
    offset = 0
    for name, size in zip(("u", "v", "w", "ph", "theta", "mu"), block_sizes):
        ref = retained[offset:offset + size]
        got = replay[offset:offset + size]
        ref_norm = float(np.linalg.norm(ref))
        diff_norm = float(np.linalg.norm(got - ref))
        rel = 0.0 if ref_norm == 0.0 and diff_norm == 0.0 else (
            float("inf") if ref_norm == 0.0 else diff_norm / ref_norm)
        if rel > 1.0e-8:
            raise ValueError(f"short replay gate {name} relative gradient error {rel:g} > 1e-8")
        block_rows.append({"component": name, "reference_norm": ref_norm,
                           "difference_norm": diff_norm, "relative_error": rel})
        offset += size
    full_ref = float(np.linalg.norm(retained))
    full_diff = float(np.linalg.norm(replay - retained))
    full_rel = 0.0 if full_ref == 0.0 and full_diff == 0.0 else (
        float("inf") if full_ref == 0.0 else full_diff / full_ref)
    if full_rel > 1.0e-8:
        raise ValueError(f"short replay gate full gradient relative error {full_rel:g} > 1e-8")
    for key in CONTEXT:
        if not np.array_equal(arrays.get(key), saved_descriptor[key]):
            raise ValueError(f"short replay gate context differs from pinned fixture: {key}")
    return {"csv_sha256": sha(path), "executable_sha256": expected_exe_sha,
            "full_gradient_relative_error": full_rel,
            "component_gradient_relative_errors": block_rows,
            "literal_initial_state_match": True,
            "fixture_context_match": True,
            "endpoints_bitwise_equal": True}


def validate_trial(meta, arrays, scalars, text, *, expected_initial,
                   center_arrays, gradient: bool):
    expected = {"trajectory_steps": STEPS, "trajectory_dt_fp32": DT,
                "trajectory_seconds": 300, "observation_count_per_time": 105,
                "observation_time_150_seconds": 150,
                "observation_time_300_seconds": 300,
                "physical_observation_sigma_m_s": SIGMA,
                "internal_fp64": 1, "internal_fp64_state_carry": 1,
                "retain_graph_for_adjoint": 1,
                "admissibility_checked_every_step": 1}
    if gradient:
        expected.update({"bounded_replay_pullback": 1, "replay_exact": 1,
                         "replay_steps_checked": STEPS, "forward_only": 0,
                         "retains_tape": 1, "tape_window_steps": 8,
                         "replay_snapshot_count": 121,
                         "retained_fp64_endpoint_count": 961,
                         "replay_block_count": 120,
                         "physical_state_admissibility_checks": 961,
                         "replay_snapshot_profile_checks": 121,
                         "replay_endpoints_admissible_via_exact_parity": 1,
                         "pullback_requested": 1,
                         "newton_tol_config": float(np.float32(1.0e-14)),
                         "krylov_tol_config": float(np.float32(1.0e-12)),
                         "ewt_rtol_config": float(np.float32(1.0e-6)),
                         "nk_adaptive_tol_config": 1})
    else:
        expected.update({"forward_only": 1, "pullback_requested": 0,
                         "retains_tape": 1, "tape_window_steps": 1,
                         "newton_tol_config": float(np.float32(1.0e-14)),
                         "krylov_tol_config": float(np.float32(1.0e-12)),
                         "ewt_rtol_config": float(np.float32(1.0e-6)),
                         "nk_adaptive_tol_config": 1})
    bad = {key: (meta.get(key), val) for key, val in expected.items()
           if meta.get(key) != val}
    if bad:
        raise ValueError(f"native mode metadata mismatch: {bad}")
    expected_text = {"execution_mode": "bounded_replay_pullback",
                     "checkpoint_precision": "detached_fp64_endpoint_history",
                     "objective_normalization": "0.5_sum_over_times_and_points_of_residual_over_sigma_squared",
                     "replay_status": "completed",
                     "replay_endpoint_parity": "all_960_bitwise_equal"} if gradient else {
                         "execution_mode": "single_step_tape_handoff",
                         "checkpoint_precision": "single_step_retained_fp64_handoff",
                         "objective_normalization": "0.5_sum_over_times_and_points_of_residual_over_sigma_squared"}
    text_bad = {key: (text.get(key), value) for key, value in expected_text.items()
                if text.get(key) != value}
    if text_bad:
        raise ValueError(f"native mode text metadata mismatch: {text_bad}")
    if text.get("physical_observation_domain_guard") != "passed" or \
            text.get("vertical_height_monotonic_guard") != "passed":
        raise AdmissibilityGuardError("native observation/admissibility guard did not pass")
    if not np.array_equal(arrays.get("initial_state"), expected_initial):
        raise ValueError("native initial state differs from local chart state")
    if center_arrays is not None:
        for key in (*CONTEXT, "observed_150", "observed_300"):
            if not np.array_equal(arrays.get(key), center_arrays[key]):
                raise ValueError(f"trial input/context/observation drift: {key}")
        for key in ("bracket_index_150", "bracket_index_300"):
            if not np.array_equal(arrays.get(key), center_arrays[key]):
                raise BracketMismatchError(f"observation bracket changed: {key}")
    if not all(np.isfinite(v).all() for v in arrays.values()) or \
            not all(np.isfinite(v) for v in scalars.values()):
        raise ValueError("native output contains NaN or Inf")
    for t in TIMES:
        if arrays[f"predicted_{t}"].shape != (105,):
            raise ValueError(f"predicted_{t} has an unexpected shape")
    if "objective_physical_w" not in scalars:
        raise ValueError("native output has no objective_physical_w")
    if gradient and ("initial_pullback" not in arrays or
                     not np.isfinite(arrays["initial_pullback"]).all()):
        raise ValueError("native gradient call has no finite initial_pullback")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--outdir", type=Path, required=True)
    parser.add_argument("--fixture", type=Path, default=ROOT / "tools/fixtures/pr282-exact-returned-state.zip")
    parser.add_argument("--fd-zip", type=Path, default=Path("/private/tmp/pr282-components-37913010518.zip"))
    parser.add_argument("--preflight-only", action="store_true",
                        help="extract pinned inputs and validate artifacts without native calls")
    parser.add_argument("--gate-csv", type=Path,
                        help="successful 16-step --replay-smoke CSV from the same executable")
    parser.add_argument("--gate-exe-sha",
                        help="SHA256 receipt for the executable used by the short replay gate")
    args = parser.parse_args()
    outdir = args.outdir.resolve()
    outdir.mkdir(parents=True, exist_ok=True)
    report_path = outdir / "report.json"
    report = {"schema": "pr283-small-step-local-chart-inverse-v1",
              "status": "preflight", "steps": STEPS, "dt": DT,
              "sigma": SIGMA, "grid": list(GRID), "calls": [],
              "updates_max": MAX_UPDATES, "backtracks_max": MAX_BACKTRACKS,
              "armijo": ARMIJO, "max_step_norm": MAX_STEP_NORM,
              "optimizer_run": False, "new_fd_runs": 0,
              "gradient_claim": "native bounded-replay pullback at evaluated points only; no full Eg/stationarity claim",
              "platform": platform.platform(), "python": platform.python_version(),
              "source_git_head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
              "source_sha256": {}, "executable_sha256": sha(args.exe),
              "fixture_sha256": sha(args.fixture) if args.fixture.is_file() else None,
              "fd_zip_sha256": sha(args.fd_zip) if args.fd_zip.is_file() else None,
              "replay_gate_csv": str(args.gate_csv) if args.gate_csv else None,
              "replay_gate_executable_sha256": args.gate_exe_sha}
    write_json(report_path, report)
    try:
        if not args.exe.is_file() or not args.exe.is_absolute():
            raise ValueError("--exe must name an existing absolute executable")
        source_files = [
            Path(__file__).resolve(),
            ROOT / "external/libtorch_wrf/sdirk3/wrf_sdirk3_newton_solver.h",
            ROOT / "external/libtorch_wrf/sdirk3/wrf_sdirk3_newton_solver.cpp",
            ROOT / "external/libtorch_wrf/sdirk3/wrf_sdirk3_config.h",
            ROOT / "external/libtorch_wrf/sdirk3/wrf_sdirk3_tile_unified.h",
            ROOT / "external/libtorch_wrf/sdirk3/wrf_sdirk3_tile_unified_impl.cpp",
            ROOT / "external/libtorch_wrf/sdirk3/tests/test_native_wave_refinement.cpp",
            ROOT / "tools/test_native_wave_refinement.py",
        ]
        inputs, manifest, state, basis, fd_report, endpoints = load_inputs(
            args.fixture, args.fd_zip, outdir)
        _, saved_descriptor, _, _ = inverse.read_payload(inputs / "descriptor_fine.csv")
        G, metric = fixed_metric(endpoints)
        report["source_sha256"] = {str(p.relative_to(ROOT)): sha(p) for p in source_files}
        report.update({"status": "preflight_ready", "fixture_manifest": manifest,
                       "canonical_basis_sha256": inverse.digest(basis),
                       "literal_state_sha256": inverse.digest(state),
                       "input_paths": {"initial_state": str(inputs / "initial_state.txt"),
                                       "observations": str(inputs / "observations.txt")},
                       "input_sha256": {"initial_state": sha(inputs / "initial_state.txt"),
                                        "observations": sha(inputs / "observations.txt")},
                       "fd_report_sha256": split.FD_REPORT_SHA,
                       "fd_source_revision": fd_report["source_revision"],
                       "fd_executable_sha256": fd_report["executable_sha256"],
                       "fixed_metric": metric,
                       "local_chart": "Z0(delta)=literal_saved_state+Bcan@delta; delta starts at zero",
                       "preflight_failure_report_preserved": False})
        write_json(report_path, report)
        if args.preflight_only:
            report.update(status="preflight_only_ready", native_calls_started=False)
            write_json(report_path, report)
            return 0
        if args.gate_csv is None or not args.gate_exe_sha:
            raise ValueError("normal execution requires --gate-csv and --gate-exe-sha")
        gate_receipt = validate_replay_gate(args.gate_csv, args.gate_exe_sha,
                                            args.exe, state, saved_descriptor)
        report["short_replay_gate"] = gate_receipt
        write_json(report_path, report)
        center_arrays = None
        delta = np.zeros(4, dtype=np.float64)
        def invoke(label: str, point: np.ndarray, gradient: bool):
            initial = state.copy() if not np.any(point) else state + basis @ point
            state_path = outdir / f"{label}.initial_state.txt"
            write_vector(state_path, initial)
            output = outdir / f"{label}.csv"
            command = [str(args.exe), *map(str, GRID), str(output),
                       "--physical-wave-inverse", str(state_path),
                       str(inputs / "observations.txt"), str(STEPS), str(DT),
                       "--newton-tol", "1e-14", "--krylov-tol", "1e-12"]
            command.append("--bounded-replay-pullback" if gradient else "--bounded-tape-forward-only")
            usage = outdir / f"{label}.time.txt"
            timed = ["/usr/bin/time", "-v", "-o", str(usage), *command]
            started = time.monotonic()
            result = subprocess.run(timed, capture_output=True, text=True)
            elapsed = time.monotonic() - started
            (outdir / f"{label}.stdout.log").write_text(result.stdout)
            (outdir / f"{label}.stderr.log").write_text(result.stderr)
            usage_text = usage.read_text() if usage.is_file() else ""
            call = {"label": label, "kind": "gradient" if gradient else "forward_trial",
                    "delta": point.tolist(), "state_sha256": inverse.digest(initial),
                    "initial_state_file": str(state_path), "output_csv": str(output),
                    "command": command, "timed_command": timed,
                    "return_code": result.returncode, "seconds": elapsed,
                    "process_elapsed_wall": parse_time_value(usage_text, "Elapsed (wall clock) time (h:mm:ss or m:ss)"),
                    "peak_rss_kib": parse_time_value(usage_text, "Maximum resident set size (kbytes)"),
                    "time_report": str(usage),
                    "output_sha256": sha(output) if output.is_file() else None}
            report["calls"].append(call)
            write_json(report_path, report)
            if result.returncode:
                raise RuntimeError(f"native call {label} failed rc={result.returncode}; see logs")
            payload = inverse.read_payload(output)
            meta, arrays, scalars, text = payload
            call["objective"] = scalars["objective_physical_w"]
            call["predictions"] = {str(t): arrays[f"predicted_{t}"].tolist() for t in TIMES}
            call["checkpoints"] = {str(t): {"file": str(output),
                "array_sha256": inverse.digest(arrays[f"checkpoint_{t}"]),
                "l2": float(np.linalg.norm(arrays[f"checkpoint_{t}"]))}
                for t in TIMES}
            call["guards"] = {"physical_observation_domain": text["physical_observation_domain_guard"],
                              "vertical_height_monotonic": text["vertical_height_monotonic_guard"],
                              "replay_exact": meta.get("replay_exact"),
                              "replay_steps_checked": meta.get("replay_steps_checked"),
                              "replay_snapshot_count": meta.get("replay_snapshot_count"),
                              "retained_fp64_endpoint_count": meta.get("retained_fp64_endpoint_count"),
                              "replay_block_count": meta.get("replay_block_count"),
                              "physical_state_admissibility_checks": meta.get("physical_state_admissibility_checks"),
                              "replay_snapshot_profile_checks": meta.get("replay_snapshot_profile_checks"),
                              "replay_endpoints_admissible_via_exact_parity": meta.get("replay_endpoints_admissible_via_exact_parity"),
                              "tape_semantics": "retains_tape=1 covers the active 8-step VJP window only; detached FP64 endpoint history is 961 states"}
            write_json(report_path, report)
            validate_trial(meta, arrays, scalars, text, expected_initial=initial,
                           center_arrays=center_arrays, gradient=gradient)
            return meta, arrays, scalars, text

        report["status"] = "running_gradient"
        write_json(report_path, report)
        accepted = 0
        current = None
        for update_index in range(MAX_UPDATES):
            meta, arrays, scalars, text = invoke(f"gradient_u{update_index}", delta, True)
            if center_arrays is None:
                center_arrays = arrays
                report["center_input_guards"] = "pinned from current-executable delta=0 call"
            g = basis.T @ arrays["initial_pullback"]
            if not np.isfinite(g).all():
                raise ValueError("projected native gradient is nonfinite")
            p = -np.linalg.solve(G, g)
            raw_norm = float(np.linalg.norm(p))
            if raw_norm > MAX_STEP_NORM:
                p *= MAX_STEP_NORM / raw_norm
            slope = float(g @ p)
            item = {"update": update_index + 1, "delta": delta.tolist(),
                    "objective": float(scalars["objective_physical_w"]),
                    "gradient": g.tolist(), "gradient_l2": float(np.linalg.norm(g)),
                    "direction": p.tolist(), "direction_norm": float(np.linalg.norm(p)),
                    "unclipped_direction_norm": raw_norm, "gradient_slope": slope,
                    "line_search": []}
            report.setdefault("updates", []).append(item)
            if not np.isfinite(slope) or slope >= 0.0:
                item["decision"] = "stop_non_descent_direction"
                break
            current = (arrays, scalars)
            accepted_trial = None
            for backtrack in range(MAX_BACKTRACKS + 1):
                alpha = 0.5 ** backtrack
                candidate_delta = delta + alpha * p
                try:
                    candidate = invoke(f"trial_u{update_index}_b{backtrack}", candidate_delta, False)
                except (BracketMismatchError, AdmissibilityGuardError) as error:
                    decision = ("reject_bracket_change" if isinstance(error, BracketMismatchError)
                                else "reject_admissibility_guard")
                    item["line_search"].append({"backtrack": backtrack, "alpha": alpha,
                        "delta": candidate_delta.tolist(), "accepted": False,
                        "decision": decision, "reason": str(error)})
                    write_json(report_path, report)
                    continue
                _, candidate_arrays, candidate_scalars, _ = candidate
                candidate_cost = float(candidate_scalars["objective_physical_w"])
                bound = float(scalars["objective_physical_w"] + ARMIJO * alpha * slope)
                ok = candidate_cost < float(scalars["objective_physical_w"]) and candidate_cost <= bound
                item["line_search"].append({"backtrack": backtrack, "alpha": alpha,
                    "delta": candidate_delta.tolist(), "objective": candidate_cost,
                    "armijo_bound": bound, "accepted": bool(ok)})
                write_json(report_path, report)
                if ok:
                    accepted_trial = (candidate_delta, candidate_arrays, candidate_scalars)
                    break
            if accepted_trial is None:
                item["decision"] = "stop_armijo_exhausted"
                break
            delta, accepted_arrays, accepted_scalars = accepted_trial
            cost_decrease = float(scalars["objective_physical_w"] - accepted_scalars["objective_physical_w"])
            if cost_decrease <= 0.0:
                raise ValueError("Armijo trial did not strictly decrease the objective")
            accepted += 1
            item["decision"] = "accepted"
            item["accepted_delta"] = delta.tolist()
            item["accepted_objective"] = float(accepted_scalars["objective_physical_w"])
            item["accepted_cost_decrease"] = cost_decrease
            report["accepted_updates"] = accepted
            report["status"] = "running_updates"
            write_json(report_path, report)
            current = (accepted_arrays, accepted_scalars)
        if accepted < 1:
            report.update(status="no_accepted_update", optimizer_run=True,
                          accepted_updates=0,
                          convergence_claim="none; line search did not accept an update")
            write_json(report_path, report)
            return 2
        report.update(status="completed_bounded_updates", optimizer_run=True,
                      accepted_updates=accepted, final_delta=delta.tolist(),
                      final_cost=(float(current[1]["objective_physical_w"]) if current else None),
                      convergence_claim="none; at most two Armijo accepted updates, no full Eg/stationarity check")
        write_json(report_path, report)
        return 0
    except Exception as error:
        report.update(status="preflight_failed" if not report.get("calls") else "native_or_validation_failed",
                      failure_type=type(error).__name__, failure=str(error),
                      preflight_failure_report_preserved=True)
        write_json(report_path, report)
        raise


if __name__ == "__main__":
    raise SystemExit(main())
