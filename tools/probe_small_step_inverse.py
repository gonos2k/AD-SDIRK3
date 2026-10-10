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
PR283_RESUME_ZIP_SHA = "ad31d82325c780230ff816d4c70340f9b3dfc09abce14f6bd8725448e3c69728"
NATIVE_SOURCES = (
    "external/libtorch_wrf/sdirk3/wrf_sdirk3_newton_solver.h",
    "external/libtorch_wrf/sdirk3/wrf_sdirk3_newton_solver.cpp",
    "external/libtorch_wrf/sdirk3/wrf_sdirk3_config.h",
    "external/libtorch_wrf/sdirk3/wrf_sdirk3_tile_unified.h",
    "external/libtorch_wrf/sdirk3/wrf_sdirk3_tile_unified_impl.cpp",
    "external/libtorch_wrf/sdirk3/tests/test_native_wave_refinement.cpp",
    "tools/test_native_wave_refinement.py",
)


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


def validate_resume_report(parent_report: dict, parent_dir: Path, *, exe: Path,
                           state: np.ndarray, basis: np.ndarray, manifest: dict,
                           saved_descriptor: dict, G: np.ndarray) -> dict:
    if parent_report.get("status") not in ("completed_bounded_updates", "completed_bounded_continuation"):
        raise ValueError("resume source report is not a completed accepted-update run")
    if (parent_report.get("fixture_sha256") != FIXTURE_SHA or
            parent_report.get("canonical_basis_sha256") != inverse.digest(basis) or
            parent_report.get("literal_state_sha256") != inverse.digest(state) or
            parent_report.get("fd_zip_sha256") != FD_ZIP_SHA):
        raise ValueError("resume report input pins differ from the current fixture/FD inputs")
    input_hashes = parent_report.get("input_sha256", {})
    for name in ("initial_state.txt", "observations.txt"):
        expected = manifest["files"][name]["sha256"]
        archived = parent_dir / "inputs" / name
        if input_hashes.get(name.removesuffix(".txt")) != expected or not archived.is_file() or sha(archived) != expected:
            raise ValueError(f"resume {name} differs from the fixed fixture input")
    observation_rows = np.loadtxt(parent_dir / "inputs" / "observations.txt", skiprows=2)
    if observation_rows.shape != (105, 5):
        raise ValueError("resume observation input has an unexpected shape")
    exe_sha = sha(exe)
    if (parent_report.get("executable_sha256") != exe_sha or
            parent_report.get("short_replay_gate", {}).get("executable_sha256") != exe_sha):
        raise ValueError("resume executable differs from the accepted PR283 executable")
    source_hashes = parent_report.get("source_sha256", {})
    for rel in NATIVE_SOURCES:
        expected = source_hashes.get(rel)
        if expected is None or sha(ROOT / rel) != expected:
            raise ValueError(f"resume native source fingerprint differs: {rel}")
    if parent_report.get("accepted_updates", 0) < 2:
        raise ValueError("resume source has fewer than two accepted cost-Armijo updates")
    accepted_updates = [u for u in parent_report.get("updates", [])
                        if u.get("decision") == "accepted" and "accepted_delta" in u]
    origin_count = int(parent_report.get("resume_origin_accepted_updates", 2))
    if len(accepted_updates) != parent_report["accepted_updates"] or len(accepted_updates) < 2:
        raise ValueError("resume report update history is incomplete or inconsistent")
    if not np.array_equal(np.asarray(accepted_updates[-1]["accepted_delta"], dtype=np.float64),
                          np.asarray(parent_report.get("final_delta"), dtype=np.float64)):
        raise ValueError("resume final delta does not match its final accepted update")

    gate_rel = parent_report.get("short_replay_gate", {}).get("bundle_csv", "replay_gate.csv")
    gate_path = parent_dir / gate_rel
    if not gate_path.is_file():
        raise ValueError(f"resume artifact is missing its short replay gate: {gate_rel}")
    gate = validate_replay_gate(gate_path, exe_sha, exe, state, saved_descriptor)
    if gate["csv_sha256"] != parent_report["short_replay_gate"].get("csv_sha256"):
        raise ValueError("resume short replay gate digest differs from its report")

    center_arrays = None
    gradients = {}
    objectives = {}
    arrays_by_label = {}
    for call in parent_report.get("calls", []):
        label = call["label"]
        csv_rel = call.get("bundle_csv", f"{label}.csv")
        state_rel = call.get("bundle_initial_state", f"{label}.initial_state.txt")
        csv_path, state_path = parent_dir / csv_rel, parent_dir / state_rel
        if not csv_path.is_file() or not state_path.is_file():
            raise ValueError(f"resume call files are missing: {label}")
        if sha(csv_path) != call.get("output_sha256"):
            raise ValueError(f"resume native CSV digest mismatch: {label}")
        meta, arrays, scalars, text_meta = inverse.read_payload(csv_path)
        delta = np.asarray(call["delta"], dtype=np.float64)
        expected_initial = state.copy() if not np.any(delta) else state + basis @ delta
        if (not np.array_equal(split.read_literal_vector(state_path), expected_initial) or
                not np.array_equal(arrays.get("initial_state"), expected_initial)):
            raise ValueError(f"resume local-chart state mismatch: {label}")
        is_gradient = call.get("kind") == "gradient"
        validate_trial(meta, arrays, scalars, text_meta, expected_initial=expected_initial,
                       center_arrays=center_arrays, gradient=is_gradient)
        if center_arrays is None:
            if not is_gradient or not np.array_equal(arrays["initial_state"], state):
                raise ValueError("resume history does not start at literal delta zero")
            for key in CONTEXT:
                if not np.array_equal(arrays[key], saved_descriptor[key]):
                    raise ValueError(f"resume fixture context mismatch: {key}")
            for t, column in ((150, 3), (300, 4)):
                if not np.array_equal(arrays[f"observed_{t}"], observation_rows[:, column]):
                    raise ValueError(f"resume fixture observations differ from pinned values: t={t}")
            center_arrays = arrays
        for t in TIMES:
            if not np.array_equal(arrays[f"observed_{t}"], center_arrays[f"observed_{t}"]):
                raise ValueError(f"resume observation data changed: {label}, t={t}")
            if not np.array_equal(arrays[f"bracket_index_{t}"], center_arrays[f"bracket_index_{t}"]):
                raise ValueError(f"resume observation bracket changed: {label}, t={t}")
        cost = float(scalars["objective_physical_w"])
        if not np.isclose(cost, call.get("objective"), rtol=1e-12, atol=1e-10):
            raise ValueError(f"resume cost/report mismatch: {label}")
        objectives[label] = cost
        arrays_by_label[label] = arrays
        if is_gradient:
            g = basis.T @ arrays["initial_pullback"]
            linked = [u for u in parent_report["updates"]
                      if u.get("gradient_call_label") == label]
            if not linked:
                # Original PR283 calls predate explicit gradient links; their
                # gradient_uN labels remain in later continuation bundles.
                suffix = label.removeprefix("gradient_u")
                index = int(suffix) if suffix.isdigit() else None
                if index is not None and index < origin_count:
                    linked = accepted_updates[index:index + 1]
            if any(not np.allclose(g, u["gradient"], rtol=2e-13, atol=1e-10)
                   for u in linked):
                raise ValueError(f"resume projected gradient/report mismatch: {label}")
            gradients[label] = g

    if center_arrays is None or len(gradients) < parent_report["accepted_updates"]:
        raise ValueError("resume report lacks actual native gradient outputs for its accepted steps")
    terminal = parent_report.get("terminal_gradient")
    if terminal is not None:
        call = next((item for item in parent_report["calls"]
                     if item["label"] == terminal.get("call_label")), None)
        if call is None or call.get("kind") != "gradient":
            raise ValueError("resume terminal gradient has no matching native gradient call")
        gradient = gradients.get(call["label"])
        if (gradient is None or not np.allclose(gradient, terminal.get("gradient"),
                                                rtol=2e-13, atol=1e-10) or
                not np.array_equal(np.asarray(call["delta"], dtype=np.float64),
                                   np.asarray(terminal.get("delta"), dtype=np.float64)) or
                not np.array_equal(np.asarray(terminal.get("delta"), dtype=np.float64),
                                   np.asarray(parent_report.get("final_delta"), dtype=np.float64)) or
                not np.isclose(objectives[call["label"]], terminal.get("objective"),
                               rtol=1e-12, atol=1e-10)):
            raise ValueError("resume terminal gradient/state/objective receipt is inconsistent")
        final_label = call["label"]
    else:
        last = accepted_updates[-1]
        final_label = last.get("accepted_call_label")
        if final_label is None:
            accepted_delta = np.asarray(last["accepted_delta"], dtype=np.float64)
            matching = [c for c in parent_report["calls"] if c.get("kind") == "forward_trial" and
                        np.array_equal(np.asarray(c["delta"], dtype=np.float64), accepted_delta)]
            final_label = matching[-1]["label"] if matching else None
    if (final_label is None or final_label not in objectives or
            not np.isclose(objectives[final_label], parent_report.get("final_cost"),
                           rtol=1e-12, atol=1e-10)):
        raise ValueError("resume final objective differs from its last accepted endpoint")

    for i, update in enumerate(accepted_updates):
        g = np.asarray(update["gradient"], dtype=np.float64)
        if i < origin_count:
            p = -np.linalg.solve(G, g)
        else:
            H = np.linalg.solve(G, np.eye(G.shape[0], dtype=np.float64))
            for j in range(i):
                s = (np.asarray(accepted_updates[j]["accepted_delta"], dtype=np.float64) -
                     np.asarray(accepted_updates[j]["delta"], dtype=np.float64))
                y = (np.asarray(accepted_updates[j+1]["gradient"], dtype=np.float64) -
                     np.asarray(accepted_updates[j]["gradient"], dtype=np.float64))
                H, _ = inverse.safe_bfgs_inverse_update(H, s, y)
            p = -(H @ g)
        raw_norm = float(np.linalg.norm(p))
        if raw_norm > MAX_STEP_NORM:
            p *= MAX_STEP_NORM / raw_norm
        if (not np.allclose(p, update["direction"], rtol=2e-12, atol=1e-12) or
                not np.isclose(float(g @ p), update["gradient_slope"], rtol=2e-12, atol=1e-10)):
            raise ValueError(f"resume historical direction/slope mismatch at update {i+1}")
        trial = next((c for c in parent_report["calls"]
                      if c.get("kind") == "forward_trial" and
                      np.allclose(c.get("delta"), update["accepted_delta"], rtol=0.0, atol=2e-16)), None)
        if trial is None:
            raise ValueError(f"resume accepted trial CSV is missing for update {i+1}")
        current_cost = float(update["objective"])
        candidate_cost = objectives[trial["label"]]
        alpha = next((float(item["alpha"]) for item in update.get("line_search", [])
                      if item.get("accepted")), None)
        if alpha is None:
            raise ValueError(f"resume accepted Armijo alpha is missing for update {i+1}")
        bound = current_cost + ARMIJO * alpha * float(g @ p)
        if not candidate_cost < current_cost or candidate_cost > bound:
            raise ValueError(f"resume accepted trial failed strict actual-cost Armijo at update {i+1}")
    return {"report": parent_report, "gate": gate, "center_arrays": center_arrays,
            "gradients": gradients, "objectives": objectives,"arrays_by_label":arrays_by_label,
            "accepted_updates": accepted_updates, "executable_sha256": exe_sha}


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


def invoke_native(*, exe: Path, outdir: Path, inputs: Path, state: np.ndarray,
                  basis: np.ndarray, delta: np.ndarray, label: str, gradient: bool,
                  center_arrays: dict | None, report: dict,
                  update_index: int | None = None):
    """Run and validate one native objective or bounded-replay gradient call."""
    initial = state.copy() if not np.any(delta) else state + basis @ delta
    state_path = outdir / f"{label}.initial_state.txt"
    write_vector(state_path, initial)
    output = outdir / f"{label}.csv"
    command = [str(exe), *map(str, GRID), str(output), "--physical-wave-inverse",
               str(state_path), str(inputs / "observations.txt"), str(STEPS), str(DT),
               "--newton-tol", "1e-14", "--krylov-tol", "1e-12",
               "--bounded-replay-pullback" if gradient else "--bounded-tape-forward-only"]
    usage = outdir / f"{label}.time.txt"
    timed = ["/usr/bin/time", "-v", "-o", str(usage), *command]
    report["active_call"] = {"label": label, "kind": "gradient" if gradient else "forward_trial",
                             "delta": delta.tolist(), "command": command}
    write_json(outdir / "report.json", report)
    print(f"native start {label}", flush=True)
    started = time.monotonic()
    result = subprocess.run(timed, capture_output=True, text=True)
    elapsed = time.monotonic() - started
    print(f"native end {label} rc={result.returncode} elapsed_s={elapsed:.3f}", flush=True)
    (outdir / f"{label}.stdout.log").write_text(result.stdout)
    (outdir / f"{label}.stderr.log").write_text(result.stderr)
    usage_text = usage.read_text() if usage.is_file() else ""
    call = {"label": label, "kind": "gradient" if gradient else "forward_trial",
            "update_index": update_index, "delta": delta.tolist(),
            "state_sha256": inverse.digest(initial),
            "initial_state_file": str(state_path), "output_csv": str(output),
            "bundle_csv": f"{label}.csv", "bundle_initial_state": f"{label}.initial_state.txt",
            "command": command, "timed_command": timed,
            "return_code": result.returncode, "seconds": elapsed,
            "process_elapsed_wall": parse_time_value(usage_text, "Elapsed (wall clock) time (h:mm:ss or m:ss)"),
            "peak_rss_kib": parse_time_value(usage_text, "Maximum resident set size (kbytes)"),
            "time_report": str(usage), "output_sha256": sha(output) if output.is_file() else None}
    report["calls"].append(call)
    report["active_call"] = None
    write_json(outdir / "report.json", report)
    if result.returncode:
        raise RuntimeError(f"native call {label} failed rc={result.returncode}; see logs")
    meta, arrays, scalars, text = inverse.read_payload(output)
    call["objective"] = float(scalars["objective_physical_w"])
    call["predictions"] = {str(t): arrays[f"predicted_{t}"].tolist() for t in TIMES}
    call["checkpoints"] = {str(t): {"array_sha256": inverse.digest(arrays[f"checkpoint_{t}"]),
                                   "l2": float(np.linalg.norm(arrays[f"checkpoint_{t}"]))}
                           for t in TIMES}
    call["guards"] = {"physical_observation_domain": text["physical_observation_domain_guard"],
                      "vertical_height_monotonic": text["vertical_height_monotonic_guard"],
                      **{key: meta.get(key) for key in (
                          "replay_exact", "replay_steps_checked", "replay_snapshot_count",
                          "retained_fp64_endpoint_count", "replay_block_count",
                          "physical_state_admissibility_checks", "replay_snapshot_profile_checks",
                          "replay_endpoints_admissible_via_exact_parity")}}
    if gradient:
        call["guards"]["tape_semantics"] = (
            "retains_tape=1 covers the active 8-step VJP window only; "
            "detached FP64 endpoint history is 961 states")
    write_json(outdir / "report.json", report)
    validate_trial(meta, arrays, scalars, text, expected_initial=initial,
                   center_arrays=center_arrays, gradient=gradient)
    return meta, arrays, scalars, text


def copy_resume_bundle(resume_zip: Path, expected_sha: str, destination: Path) -> tuple[dict, Path, str]:
    report_sha = None
    if sha(resume_zip) != expected_sha:
        raise ValueError("resume ZIP SHA256 does not match explicit predecessor receipt")
    import zipfile

    destination.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(resume_zip) as archive:
        if archive.testzip() is not None:
            raise ValueError("resume ZIP failed CRC verification")
        names = set(archive.namelist())
        prefix = "measurement/" if "measurement/report.json" in names else ""
        report_member = prefix + "report.json"
        if report_member not in names:
            raise ValueError("resume ZIP has no report.json at its artifact root")
        for member in archive.infolist():
            if member.is_dir() or not member.filename.startswith(prefix):
                continue
            relative = member.filename[len(prefix):]
            target = (destination / relative).resolve()
            if destination.resolve() not in target.parents:
                raise ValueError(f"unsafe resume ZIP member: {member.filename}")
            target.parent.mkdir(parents=True, exist_ok=True)
            raw = archive.read(member.filename)
            target.write_bytes(raw)
            if relative == "report.json":
                report_sha = hashlib.sha256(raw).hexdigest()
    report_path = destination / "report.json"
    report = json.loads(report_path.read_text())
    if report.get("schema") not in ("pr283-small-step-local-chart-inverse-v1",
                                     "pr284-small-step-local-chart-continuation-v1"):
        raise ValueError(f"unsupported resume report schema: {report.get('schema')}")
    if (report["schema"] == "pr283-small-step-local-chart-inverse-v1" and
            expected_sha != PR283_RESUME_ZIP_SHA):
        raise ValueError("PR283 predecessor does not match the pinned accepted artifact SHA256")
    return report, destination, report_sha


def source_fingerprints() -> dict:
    paths = [Path(__file__).resolve(), ROOT / "tools/test_fine_wave_inverse.py",
             *(ROOT / item for item in NATIVE_SOURCES)]
    return {str(path.relative_to(ROOT)): sha(path) for path in paths}


def resume_preflight(args) -> int:
    if not args.exe.is_file() or not args.exe.is_absolute():
        raise ValueError("--exe must name an existing absolute executable")
    outdir = args.outdir.resolve()
    outdir.mkdir(parents=True, exist_ok=True)
    inputs, manifest, state, basis, fd_report, endpoints = load_inputs(
        args.fixture, args.fd_zip, outdir)
    _, saved_descriptor, _, _ = inverse.read_payload(inputs / "descriptor_fine.csv")
    G, metric = fixed_metric(endpoints)
    parent_report, parent_dir, parent_report_sha = copy_resume_bundle(
        args.resume_zip, args.resume_zip_sha256, outdir / "predecessor")
    verified = validate_resume_report(parent_report, parent_dir, exe=args.exe,
                                      state=state, basis=basis, manifest=manifest,
                                      saved_descriptor=saved_descriptor, G=G)
    gate_rel = parent_report.get("short_replay_gate", {}).get("bundle_csv", "replay_gate.csv")
    gate_src = parent_dir / gate_rel
    gate_out = outdir / "replay_gate.csv"
    gate_out.write_bytes(gate_src.read_bytes())
    result = {"schema": "pr284-small-step-resume-preflight-v1",
              "status": "resume_preflight_passed", "native_calls_started": False,
              "resume_zip_sha256": args.resume_zip_sha256,
              "resume_report_sha256": parent_report_sha,
              "resume_report_status": parent_report["status"],
              "resume_report_schema": parent_report["schema"],
              "resume_final_delta": parent_report["final_delta"],
              "resume_final_cost": parent_report["final_cost"],
              "resume_accepted_updates": parent_report["accepted_updates"],
              "resume_validated_native_calls": len(verified["objectives"]),
              "fixture_sha256": sha(args.fixture), "fd_zip_sha256": sha(args.fd_zip),
              "canonical_basis_sha256": inverse.digest(basis),
              "fixed_metric": metric, "executable_sha256": sha(args.exe),
              "native_source_sha256": source_fingerprints(),
              "input_paths": {"initial_state": str(inputs / "initial_state.txt"),
                              "observations": str(inputs / "observations.txt"),
                              "replay_gate": str(gate_out)}}
    write_json(outdir / "resume_preflight.json", result)
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


def run_resume(args) -> int:
    exe = args.exe.resolve()
    if not exe.is_file() or not exe.is_absolute():
        raise ValueError("--exe must name an existing absolute executable")
    if args.gate_csv is None or not args.gate_exe_sha:
        raise ValueError("resume execution requires the extracted --gate-csv and --gate-exe-sha")
    outdir = args.outdir.resolve()
    outdir.mkdir(parents=True, exist_ok=True)
    inputs, manifest, state, basis, fd_report, endpoints = load_inputs(
        args.fixture, args.fd_zip, outdir)
    _, saved_descriptor, _, _ = inverse.read_payload(inputs / "descriptor_fine.csv")
    G, metric = fixed_metric(endpoints)
    parent_report, parent_dir, parent_report_sha = copy_resume_bundle(
        args.resume_zip, args.resume_zip_sha256, outdir / "predecessor")
    verified = validate_resume_report(parent_report, parent_dir, exe=exe,
                                     state=state, basis=basis, manifest=manifest,
                                     saved_descriptor=saved_descriptor, G=G)
    gate_rel = parent_report.get("short_replay_gate", {}).get("bundle_csv", "replay_gate.csv")
    archived_gate = parent_dir / gate_rel
    if sha(args.gate_csv) != sha(archived_gate):
        raise ValueError("--gate-csv differs from the validated predecessor gate")
    if sha(exe) != args.gate_exe_sha:
        raise ValueError("short-gate executable SHA does not match --exe")

    # Copy the validated parent bundle into the output so later bounded resumes
    # can validate the complete call chain without rewriting the predecessor.
    native_files = dict(source_fingerprints())
    prior_updates = list(parent_report["updates"])
    prior_calls = []
    for call in parent_report["calls"]:
        item = dict(call)
        label = item["label"]
        item["bundle_csv"] = "predecessor/" + item.get("bundle_csv", f"{label}.csv")
        item["bundle_initial_state"] = "predecessor/" + item.get(
            "bundle_initial_state", f"{label}.initial_state.txt")
        prior_calls.append(item)
    prior_gate = dict(parent_report["short_replay_gate"])
    prior_gate["bundle_csv"] = "predecessor/" + gate_rel
    delta = np.asarray(parent_report["final_delta"], dtype=np.float64)
    parent_count = int(parent_report["accepted_updates"])
    report_path = outdir / "report.json"
    report = {"schema": "pr284-small-step-local-chart-continuation-v1",
              "status": "resume_preflight_passed", "steps": STEPS, "dt": DT,
              "sigma": SIGMA, "grid": list(GRID), "calls": prior_calls,
              "updates": prior_updates, "updates_max": args.max_updates,
              "accepted_updates": parent_count, "accepted_updates_this_run": 0,
              "armijo": ARMIJO, "max_step_norm": MAX_STEP_NORM,
              "max_backtracks_per_update": MAX_BACKTRACKS,
              "optimizer_run": False, "new_fd_runs": 0,
              "gradient_claim": "bounded native gradients at evaluated points only; no full Eg/stationarity claim",
              "source_git_head": subprocess.check_output(
                  ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
              "source_sha256": native_files, "executable_sha256": sha(exe),
              "fixture_sha256": sha(args.fixture), "fd_zip_sha256": sha(args.fd_zip),
              "canonical_basis_sha256": inverse.digest(basis),
              "literal_state_sha256": inverse.digest(state),
              "input_paths": {"initial_state": str(inputs / "initial_state.txt"),
                              "observations": str(inputs / "observations.txt")},
              "input_sha256": {"initial_state": sha(inputs / "initial_state.txt"),
                               "observations": sha(inputs / "observations.txt")},
              "fixed_metric": metric, "local_chart": "Z0(delta)=literal_saved_state+Bcan@delta",
              "short_replay_gate": prior_gate,
              "resume_parent": {"zip_sha256": args.resume_zip_sha256,
                                "report_sha256": parent_report_sha,
                                "report_schema": parent_report["schema"],
                                "report_status": parent_report["status"],
                                "final_delta": delta.tolist(),
                                "final_cost": float(parent_report["final_cost"]),
                                "accepted_updates": parent_count,
                                "validated_calls": len(verified["objectives"])},
              "resume_origin_accepted_updates": int(
                  parent_report.get("resume_origin_accepted_updates", parent_count)),
              "resume_secant_updates": [], "gradient_evaluations": [],
              "terminal_gradient": None, "final_delta": delta.tolist(),
              "final_cost": float(parent_report["final_cost"]),
              "whole_Eg": "open", "convergence_claim": "none"}
    write_json(report_path, report)

    # Rebuild H from G^{-1} and the already measured secants between prior
    # accepted states. The final pending secant is added after the fresh VJP at δ.
    H = np.linalg.solve(G, np.eye(4, dtype=np.float64))
    accepted_parent = verified["accepted_updates"]
    for i in range(len(accepted_parent) - 1):
        old = accepted_parent[i]
        nxt = accepted_parent[i + 1]
        old_delta = np.asarray(old["delta"], dtype=np.float64)
        accepted_delta = np.asarray(old["accepted_delta"], dtype=np.float64)
        next_delta = np.asarray(nxt["delta"], dtype=np.float64)
        if not np.array_equal(accepted_delta, next_delta):
            raise ValueError(f"predecessor secant points do not join at update {i+1}")
        s = accepted_delta - old_delta
        y = np.asarray(nxt["gradient"], dtype=np.float64) - np.asarray(old["gradient"], dtype=np.float64)
        H, detail = inverse.safe_bfgs_inverse_update(H, s, y)
        report["resume_secant_updates"].append({"from_update": i + 1,
            "s": s.tolist(), "y": y.tolist(), **detail})
        write_json(report_path, report)

    center_arrays = verified["center_arrays"]
    current_meta = current_arrays = current_scalars = current_text = None

    def invoke(label: str, point: np.ndarray, gradient: bool,
               update_index: int | None = None):
        return invoke_native(exe=exe, outdir=outdir, inputs=inputs, state=state,
                             basis=basis, delta=point, label=label, gradient=gradient,
                             center_arrays=center_arrays, report=report,
                             update_index=update_index)

    accepted_this_run = 0
    old_last_update = accepted_parent[-1]
    try:
        report["status"] = "running_resume_gradient"
        write_json(report_path, report)
        current_meta, current_arrays, current_scalars, current_text = invoke(
            f"resume_gradient_u{parent_count}_start", delta, True, parent_count)
        g = basis.T @ current_arrays["initial_pullback"]
        current_cost = float(current_scalars["objective_physical_w"])
        if not np.isclose(current_cost, parent_report["final_cost"], rtol=0.0, atol=1e-10):
            raise ValueError("fresh resume gradient objective differs from predecessor final objective")
        previous_trial = next(c for c in reversed(parent_report["calls"])
                              if c.get("kind") == "forward_trial" and
                              np.allclose(c.get("delta"), delta, rtol=0.0, atol=2e-16))
        previous_arrays = verified["arrays_by_label"][previous_trial["label"]]
        for t in TIMES:
            for suffix in (f"predicted_{t}", f"checkpoint_{t}", f"bracket_index_{t}"):
                if not np.array_equal(current_arrays[suffix], previous_arrays[suffix]):
                    raise ValueError(f"fresh resume point differs from accepted predecessor {suffix}")

        pending_s = delta - np.asarray(old_last_update["delta"], dtype=np.float64)
        pending_y = g - np.asarray(old_last_update["gradient"], dtype=np.float64)
        H, pending_detail = inverse.safe_bfgs_inverse_update(H, pending_s, pending_y)
        report["resume_secant_updates"].append({"from_update": parent_count,
            "pending_at_resumed_gradient": True, "s": pending_s.tolist(),
            "y": pending_y.tolist(), **pending_detail})
        report["gradient_evaluations"].append({"call_label": f"resume_gradient_u{parent_count}_start",
            "delta": delta.tolist(), "objective": current_cost, "gradient": g.tolist(),
            "gradient_l2": float(np.linalg.norm(g))})
        current_gradient_label = f"resume_gradient_u{parent_count}_start"
        report["terminal_gradient"] = {"call_label": current_gradient_label,
            "delta": delta.tolist(), "objective": current_cost,
            "gradient": g.tolist(), "gradient_l2": float(np.linalg.norm(g)),
            "threshold": 1.0e-5,
            "threshold_reached": bool(np.linalg.norm(g) < 1.0e-5)}
        report["final_delta"] = delta.tolist()
        report["final_cost"] = current_cost
        write_json(report_path, report)

        for local_index in range(args.max_updates):
            attempt_index = len(report["updates"])
            if float(np.linalg.norm(g)) < 1.0e-5:
                report["bounded_stop_reason"] = "projected_gradient_threshold_reached_at_evaluated_point"
                break
            hsym = 0.5 * (H + H.T)
            if not np.isfinite(H).all() or np.linalg.norm(H - H.T, ord=np.inf) > 1e-12 * max(1.0, np.linalg.norm(H, ord=np.inf)):
                raise ValueError("resume inverse metric is nonfinite or materially nonsymmetric")
            np.linalg.cholesky(hsym)
            p = -(H @ g)
            raw_norm = float(np.linalg.norm(p))
            if raw_norm > MAX_STEP_NORM:
                p *= MAX_STEP_NORM / raw_norm
            slope = float(g @ p)
            update = {"update": attempt_index + 1, "accepted_update_index": parent_count + accepted_this_run + 1,
                      "delta": delta.tolist(),
                      "objective": current_cost, "gradient": g.tolist(),
                      "gradient_l2": float(np.linalg.norm(g)), "direction": p.tolist(),
                      "direction_metric": "inverse_bfgs_from_fixed_FD_G_inverse",
                      "direction_norm": float(np.linalg.norm(p)),
                      "unclipped_direction_norm": raw_norm, "gradient_slope": slope,
                      "inverse_metric": H.tolist(), "gradient_call_label": current_gradient_label,
                      "line_search": []}
            report["updates"].append(update)
            if not np.isfinite(slope) or slope >= 0.0:
                update["decision"] = "stop_non_descent_direction"
                break
            accepted_trial = None
            previous_delta = delta.copy()
            previous_gradient = g.copy()
            previous_cost = current_cost
            for backtrack in range(MAX_BACKTRACKS + 1):
                alpha = 0.5 ** backtrack
                candidate_delta = delta + alpha * p
                label = f"resume_trial_u{attempt_index}_b{backtrack}"
                try:
                    candidate = invoke(label, candidate_delta, False, attempt_index)
                except (BracketMismatchError, AdmissibilityGuardError) as error:
                    reason = "reject_bracket_change" if isinstance(error, BracketMismatchError) else "reject_admissibility_guard"
                    update["line_search"].append({"backtrack": backtrack, "alpha": alpha,
                        "delta": candidate_delta.tolist(), "accepted": False,
                        "decision": reason, "reason": str(error)})
                    write_json(report_path, report)
                    continue
                _, trial_arrays, trial_scalars, _ = candidate
                trial_cost = float(trial_scalars["objective_physical_w"])
                armijo_bound = previous_cost + ARMIJO * alpha * slope
                ok = trial_cost < previous_cost and trial_cost <= armijo_bound
                update["line_search"].append({"backtrack": backtrack, "alpha": alpha,
                    "delta": candidate_delta.tolist(), "objective": trial_cost,
                    "armijo_bound": armijo_bound, "accepted": bool(ok), "call_label": label})
                write_json(report_path, report)
                if ok:
                    accepted_trial = (candidate_delta, trial_arrays, trial_scalars, label, alpha)
                    break
            if accepted_trial is None:
                update["decision"] = "stop_armijo_exhausted"
                break

            delta, trial_arrays, trial_scalars, trial_label, alpha = accepted_trial
            cost_decrease = previous_cost - float(trial_scalars["objective_physical_w"])
            if cost_decrease <= 0.0:
                raise ValueError("resume Armijo trial did not strictly decrease actual objective")
            accepted_this_run += 1
            report["accepted_updates"] = parent_count + accepted_this_run
            report["accepted_updates_this_run"] = accepted_this_run
            update.update({"decision": "accepted", "accepted_delta": delta.tolist(),
                           "accepted_objective": float(trial_scalars["objective_physical_w"]),
                           "accepted_cost_decrease": cost_decrease,
                           "accepted_call_label": trial_label, "alpha": alpha})
            report["status"] = "running_resume_updates"
            report["final_delta"] = delta.tolist()
            report["final_cost"] = float(trial_scalars["objective_physical_w"])
            report["terminal_gradient"] = None
            write_json(report_path, report)

            # Evaluate every accepted endpoint, including the last requested update.
            grad_label = f"resume_gradient_u{parent_count + accepted_this_run}_terminal"
            next_meta, next_arrays, next_scalars, next_text = invoke(
                grad_label, delta, True, attempt_index)
            g_next = basis.T @ next_arrays["initial_pullback"]
            next_cost = float(next_scalars["objective_physical_w"])
            if not np.isclose(next_cost, trial_scalars["objective_physical_w"],
                              rtol=0.0, atol=1e-10):
                raise ValueError("accepted forward cost differs from fresh terminal gradient cost")
            for t in TIMES:
                for suffix in (f"predicted_{t}", f"checkpoint_{t}", f"bracket_index_{t}"):
                    if not np.array_equal(next_arrays[suffix], trial_arrays[suffix]):
                        raise ValueError(f"accepted terminal gradient differs from trial {suffix}")
            s_new = delta - previous_delta
            y_new = g_next - previous_gradient
            H, update_detail = inverse.safe_bfgs_inverse_update(H, s_new, y_new)
            report["resume_secant_updates"].append({"from_update": parent_count + accepted_this_run,
                "accepted_state_delta": delta.tolist(), "s": s_new.tolist(),
                "y": y_new.tolist(), **update_detail})
            report["gradient_evaluations"].append({"call_label": grad_label,
                "delta": delta.tolist(), "objective": next_cost,
                "gradient": g_next.tolist(), "gradient_l2": float(np.linalg.norm(g_next))})
            g = g_next
            current_cost = next_cost
            current_arrays = next_arrays
            current_scalars = next_scalars
            current_meta = next_meta
            current_text = next_text
            report["terminal_gradient"] = {"call_label": grad_label,
                "delta": delta.tolist(), "objective": next_cost,
                "gradient": g_next.tolist(), "gradient_l2": float(np.linalg.norm(g_next)),
                "threshold": 1.0e-5,
                "threshold_reached": bool(np.linalg.norm(g_next) < 1.0e-5)}
            report["final_delta"] = delta.tolist()
            report["final_cost"] = next_cost
            current_gradient_label = grad_label
            write_json(report_path, report)

        if accepted_this_run < 1:
            if report["terminal_gradient"]["threshold_reached"]:
                report.update(status="bounded_continuation_gradient_threshold_stop",
                              optimizer_run=True,
                              convergence_claim="none; projected gradient threshold reached at evaluated point; no full Eg/stationarity certificate")
                write_json(report_path, report)
                return 0
            report.update(status="no_accepted_resume_update", optimizer_run=True,
                          convergence_claim="none; no resumed strict actual-cost Armijo update accepted")
            write_json(report_path, report)
            return 2
        report.update(status="completed_bounded_continuation", optimizer_run=True,
                      accepted_updates=parent_count + accepted_this_run,
                      accepted_updates_this_run=accepted_this_run,
                      final_delta=delta.tolist(), final_cost=current_cost,
                      terminal_gradient_evaluated=True,
                      whole_Eg="open",
                      convergence_claim="none; bounded strict cost-Armijo continuation with a terminal gradient, no full Eg/stationarity certificate")
        write_json(report_path, report)
        return 0
    except Exception as error:
        report.update(status="native_or_validation_failed" if report.get("calls") else "resume_preflight_failed",
                      failure_type=type(error).__name__, failure=str(error),
                      preflight_failure_report_preserved=True)
        write_json(report_path, report)
        raise


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
    parser.add_argument("--resume-zip", type=Path,
                        help="pinned predecessor diagnostic artifact to continue from")
    parser.add_argument("--resume-zip-sha256",
                        help="required SHA256 receipt for --resume-zip")
    parser.add_argument("--max-updates", type=int, choices=(1, 2), default=MAX_UPDATES,
                        help="bounded continuation updates; each accepted endpoint gets a gradient")
    args = parser.parse_args()
    if args.resume_zip is not None:
        if not args.resume_zip_sha256:
            parser.error("--resume-zip requires --resume-zip-sha256")
        if args.preflight_only:
            return resume_preflight(args)
        return run_resume(args)
    if args.resume_zip_sha256:
        parser.error("--resume-zip-sha256 requires --resume-zip")
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
            return invoke_native(exe=args.exe, outdir=outdir, inputs=inputs, state=state,
                                 basis=basis, delta=point, label=label, gradient=gradient,
                                 center_arrays=center_arrays, report=report)

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
