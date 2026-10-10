#!/usr/bin/env python3
"""Compare two accepted Y-wave states with source truth through cold 300 s restarts."""
from __future__ import annotations

import argparse
import hashlib
import json
import platform
import subprocess
import sys
import tempfile
from pathlib import Path
import zipfile

import numpy as np
from scipy.linalg import expm

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import probe_small_step_inverse as driver  # noqa: E402
import test_fine_wave_inverse as inverse  # noqa: E402
import test_y_wave_inverse as y_inverse  # noqa: E402
import wave_y_reference as ywave  # noqa: E402
import wave_quadratic_reference as reference  # noqa: E402

GRID = (16, 12, 8)
STEPS, DT = 960, 0.3125
TIMES = (150, 300, 600, 900)
OBSERVATION_SIGMA = 3.0e-4

SOURCE_PATHS = (
    "tools/test_y_wave_forecast.py", "tools/test_y_wave_inverse.py",
    "tools/probe_small_step_inverse.py", "tools/test_fine_wave_inverse.py",
    "tools/wave_y_reference.py", "tools/wave_quadratic_reference.py",
    "tools/wave_energy_spatial_reference.py", "tools/wave_quadratic_transport.py",
    "external/libtorch_wrf/sdirk3/tests/test_native_wave_refinement.cpp",
    "external/libtorch_wrf/sdirk3/wrf_sdirk3_newton_solver.h",
    "external/libtorch_wrf/sdirk3/wrf_sdirk3_newton_solver.cpp",
    "external/libtorch_wrf/sdirk3/wrf_sdirk3_config.h",
    "external/libtorch_wrf/sdirk3/wrf_sdirk3_tile_unified.h",
    "external/libtorch_wrf/sdirk3/wrf_sdirk3_tile_unified_impl.cpp",
)


def sha_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _literal_vector_from_bytes(data: bytes) -> np.ndarray:
    words = data.decode("utf-8").split()
    values = np.asarray([float(value) for value in words[1:]], dtype=np.float64)
    if not words or int(words[0]) != values.size or not np.isfinite(values).all():
        raise ValueError("invalid literal vector in accepted inverse artifact")
    return values


def _member(archive: zipfile.ZipFile, prefix: str, relative: str) -> bytes:
    rel = Path(relative)
    if rel.is_absolute() or ".." in rel.parts:
        raise ValueError(f"unsafe artifact member path: {relative}")
    candidates = (prefix + rel.as_posix(), rel.as_posix())
    names = set(archive.namelist())
    for candidate in candidates:
        if candidate in names:
            return archive.read(candidate)
    suffix = "/" + rel.as_posix()
    matches = [name for name in names if name.endswith(suffix)]
    if len(matches) != 1:
        raise ValueError(f"artifact member {relative!r} is missing or ambiguous")
    return archive.read(matches[0])


def _read_report(archive: zipfile.ZipFile) -> tuple[dict, str, str]:
    report_members = [name for name in archive.namelist()
                      if name.endswith("report.json")]
    valid = []
    for name in report_members:
        data = archive.read(name)
        try:
            report = json.loads(data)
        except (UnicodeDecodeError, json.JSONDecodeError):
            continue
        if report.get("schema") == "source-generated-y-wave-inverse-v1":
            valid.append((report, name, sha_bytes(data)))
    if len(valid) != 1:
        raise ValueError("accepted ZIP must contain exactly one Y-wave inverse report")
    report, name, digest = valid[0]
    return report, name.rsplit("/", 1)[0] + "/" if "/" in name else "", digest


def _call_bundle(archive: zipfile.ZipFile, prefix: str, row: dict) -> tuple[dict, dict, dict, dict, np.ndarray]:
    csv_name = row.get("bundle_csv")
    state_name = row.get("bundle_initial_state")
    if not csv_name or not state_name:
        raise ValueError(f"inverse call {row.get('label')} lacks bundled output or input state")
    csv_bytes = _member(archive, prefix, csv_name)
    state_bytes = _member(archive, prefix, state_name)
    if row.get("output_sha256") != sha_bytes(csv_bytes):
        raise ValueError(f"inverse output CSV hash mismatch for {row['label']}")
    state_sha = row.get("state_sha256", row.get("initial_state_sha256"))
    if state_sha and state_sha != inverse.digest(_literal_vector_from_bytes(state_bytes)):
        raise ValueError(f"inverse literal initial-state digest mismatch for {row['label']}")
    temp = Path(tempfile.mkdtemp(prefix="pr287-inverse-call-"))
    csv_path = temp / "call.csv"
    csv_path.write_bytes(csv_bytes)
    meta, arrays, scalars, text = inverse.read_payload(csv_path)
    literal = _literal_vector_from_bytes(state_bytes)
    if not np.array_equal(arrays.get("initial_state"), literal):
        raise ValueError(f"inverse output initial_state differs from literal call input: {row['label']}")
    return meta, arrays, scalars, text, literal


def _verify_source_and_inputs(report: dict, archive: zipfile.ZipFile,
                              prefix: str, exe: Path, accepted_sha: str) -> dict:
    if report.get("status") != "completed_gradient_threshold":
        raise ValueError(f"inverse report is not complete: {report.get('status')}")
    if report.get("grid") != list(GRID) or report.get("steps") != STEPS or report.get("dt") != DT:
        raise ValueError("inverse report grid or time schedule mismatch")
    if report.get("native_executable_sha256") != driver.sha(exe):
        raise ValueError("forecast executable SHA differs from the accepted inverse executable")
    source = report.get("source", {})
    if source.get("executable_sha256") != driver.sha(exe):
        raise ValueError("source receipt executable SHA differs from the selected executable")
    current_hashes = {}
    for name, expected in source.get("sha256", {}).items():
        path = ROOT / name
        if not path.is_file() or driver.sha(path) != expected:
            raise ValueError(f"accepted inverse source changed since execution: {name}")
        current_hashes[name] = expected
    if not current_hashes:
        raise ValueError("inverse report has no source fingerprint receipt")
    input_hashes = report.get("input_sha256", {})
    input_bytes = {}
    for name, expected in input_hashes.items():
        data = _member(archive, prefix, f"inputs/{name}")
        if sha_bytes(data) != expected:
            raise ValueError(f"accepted inverse input hash mismatch: {name}")
        input_bytes[name] = data
    required = {"initial_state.txt", "observations.txt", "canonical_basis.npy", "source_H_BG.npy"}
    if required - input_bytes.keys():
        raise ValueError(f"inverse archive lacks required input pins: {sorted(required-input_bytes.keys())}")
    return {"source_revision": source.get("revision"),
            "source_sha256": current_hashes,
            "executable_sha256": driver.sha(exe),
            "accepted_zip_sha256": accepted_sha,
            "input_sha256": input_hashes,
            "input_bytes": input_bytes}


def _state_blocks(nx: int, ny: int, nz: int) -> dict:
    sizes = (ny*nz*(nx+1), (ny+1)*nz*nx, ny*(nz+1)*nx,
             ny*(nz+1)*nx, ny*nz*nx, ny*nx)
    starts = np.r_[0, np.cumsum(sizes[:-1])].astype(int)
    return {name: (int(start), int(size)) for name, start, size in zip(
        ("u", "v", "w", "ph", "theta", "mu"), starts, sizes)}


def _owned(values: np.ndarray, name: str) -> np.ndarray:
    nx, ny, nz = GRID
    if name == "u":
        return values.reshape(ny, nz, nx+1)[:, :, :-1]
    if name == "v":
        return values.reshape(ny+1, nz, nx)
    if name in ("w", "ph"):
        return values.reshape(ny, nz+1, nx)[:, 1:, :]
    if name == "theta":
        return values.reshape(ny, nz, nx)
    if name == "mu":
        return values.reshape(ny, nx)
    raise KeyError(name)


def owned_state_metrics(actual: np.ndarray, source: np.ndarray) -> dict:
    """Per-variable owned spatial error RMS/max; never combine mixed units."""
    a = np.asarray(actual, dtype=np.float64)
    b = np.asarray(source, dtype=np.float64)
    if a.shape != (8480,) or b.shape != (8480,) or not np.isfinite(a).all() or not np.isfinite(b).all():
        raise ValueError("whole state must be finite packed 8480-vectors")
    policy = {"u": ("m s-1", "exclude periodic duplicate x face"),
              "v": ("m s-1", "include physical Y faces; symmetric wall faces are zero"),
              "w": ("m s-1", "exclude fixed lower z face; include dynamic top"),
              "ph": ("m2 s-2", "exclude fixed lower z face; include dynamic top"),
              "theta": ("K", "all mass points"), "mu": ("Pa", "all mass points")}
    result = {}
    slices = _state_blocks(*GRID)
    expected_counts = {"u": 1536, "v": 1664, "w": 1536, "ph": 1536, "theta": 1536, "mu": 192}
    for name, (offset, length) in slices.items():
        error = _owned(a[offset:offset+length]-b[offset:offset+length], name)
        source_owned = _owned(b[offset:offset+length], name)
        if error.size != expected_counts[name]:
            raise AssertionError(f"owned sample count mismatch for {name}: {error.size}")
        result[name] = {"unit": policy[name][0], "owned_point_policy": policy[name][1],
                        "owned_point_count": int(error.size),
                        "absolute_error_rms": float(np.sqrt(np.mean(error*error))),
                        "absolute_error_max": float(np.max(np.abs(error))),
                        "expected_total_state_rms": float(np.sqrt(np.mean(source_owned*source_owned)))}
    return result


def _source_arrays(report: dict, artifact: dict, prefix: str,
                   archive: zipfile.ZipFile, outdir: Path) -> dict:
    input_bytes = artifact["input_bytes"]
    inputs = outdir / "inputs"
    inputs.mkdir(parents=True, exist_ok=True)
    for name, data in input_bytes.items():
        (inputs/name).write_bytes(data)
    descriptor_name = "descriptor_16x12x8.csv"
    descriptor_bytes = _member(archive, prefix, descriptor_name)
    if sha_bytes(descriptor_bytes) != report.get("descriptor_csv_sha256"):
        raise ValueError("accepted inverse descriptor CSV hash mismatch")
    descriptor_path = inputs / descriptor_name
    descriptor_path.write_bytes(descriptor_bytes)
    descriptor_path.write_bytes(descriptor_bytes)
    _, descriptor_arrays, _, _ = inverse.read_payload(descriptor_path)
    column = reference.build_case(16, 8, native_csv=descriptor_path)
    base = np.asarray(descriptor_arrays["base"], dtype=np.float64)
    if base.shape != (8480,) or not np.isfinite(base).all():
        raise ValueError("descriptor equilibrium base is invalid")

    # The archived basis and H_BG are the exact arrays used by the inverse.
    # Keep their producer values; do not reselect eigenvectors on this host.
    B = np.load(inputs/"canonical_basis.npy", allow_pickle=False)
    if (B.shape != (8480, 2) or not np.isfinite(B).all() or
            inverse.digest(B) != report.get("B_sha256")):
        raise ValueError("accepted inverse canonical basis is invalid or unpinned")
    xyz = ywave.source_observation_xyz(column)
    report_xyz = np.asarray(report.get("source_observation_xyz"), dtype=np.float64)
    if not np.array_equal(xyz, report_xyz):
        raise ValueError("source observation XYZ differs from accepted inverse report")
    H = np.load(inputs/"source_H_BG.npy", allow_pickle=False)
    if report.get("sigma") != OBSERVATION_SIGMA:
        raise ValueError("accepted inverse observation sigma differs from the fixed source case")
    scaled_H = H / OBSERVATION_SIGMA
    singular_values = np.linalg.svd(scaled_H, compute_uv=False)
    if (H.shape != (210, 2) or not np.isfinite(H).all() or
            report.get("source_H_BG_shape") != [210, 2] or
            report.get("source_H_BG_rank") != 2 or np.linalg.matrix_rank(scaled_H) != 2 or
            not np.allclose(singular_values,
                            np.asarray(report.get("source_H_BG_singular_values"), dtype=np.float64),
                            rtol=1e-12, atol=1e-12)):
        raise ValueError("accepted inverse whitened H_BG matrix/rank receipt is invalid")

    truth_controls = np.asarray(report["truth_controls"], dtype=np.float64)
    source_pairs = np.asarray(report.get("source_truth_state"), dtype=np.float64)
    if truth_controls.shape != (2,) or source_pairs.shape != (41, 2):
        raise ValueError("accepted source truth controls/state have invalid shape")
    truth_q = source_pairs[:, 0] + 1j*source_pairs[:, 1]
    if not np.isfinite(truth_q).all():
        raise ValueError("accepted source truth mode is nonfinite")
    packed_truth = ywave.pack_source_mode(column, truth_q, 12)
    if not np.allclose(packed_truth, B @ truth_controls, rtol=2e-12, atol=1e-14):
        raise ValueError("accepted source truth state does not match the archived two-control basis")

    source_truth = np.asarray(report.get("source_observations"), dtype=np.float64)
    if source_truth.shape != (2, 105) or not np.isfinite(source_truth).all():
        raise ValueError("accepted source observation truth has invalid shape or values")
    obs_file = np.loadtxt(inputs/"observations.txt", skiprows=2)
    if obs_file.shape != (105, 5) or not np.array_equal(obs_file[:, :3], xyz) or \
            not np.array_equal(obs_file[:, 3:].T, source_truth):
        raise ValueError("accepted observations file differs from the archived source truth")
    # Recompute the source-defined 150/300 observations. A small allowance
    # covers host BLAS/LAPACK last-bit differences; the archived literal file
    # remains the one passed to the native calls.
    local_truth = ywave.source_observations(column, truth_q, 12, xyz,
                                            times=(150.0, 300.0), linear_geometry=False)
    reconstruction_error = float(np.max(np.abs(local_truth-source_truth)))
    reconstruction_tol = 256*np.finfo(np.float64).eps*max(1.0, float(np.max(np.abs(source_truth))))
    if reconstruction_error > reconstruction_tol:
        raise ValueError(f"local source observation reconstruction drifted: {reconstruction_error:g}")
    saved_initial = _literal_vector_from_bytes(input_bytes["initial_state.txt"])
    start_controls = np.asarray(report["starting_controls"], dtype=np.float64)
    if not np.array_equal(saved_initial, base+B@start_controls):
        raise ValueError("accepted source initial_state is not descriptor base + B@start_controls")
    return {"column": column, "descriptor_arrays": descriptor_arrays,
            "equilibrium_base": base, "source_matrix": ywave.source_y_matrix(column, ny=12),
            "basis": B, "xyz": xyz, "source_truth_controls": truth_controls,
            "source_truth_state": truth_q, "source_truth_observations": source_truth,
            "source_truth_reconstruction_max_abs": reconstruction_error,
            "source_truth_reconstruction_tolerance": reconstruction_tol,
            "source_H_BG": H, "input_dir": inputs}


def _load_inverse_call(archive: zipfile.ZipFile, prefix: str, call: dict,
                       base: np.ndarray, B: np.ndarray, controls: np.ndarray,
                       descriptor_arrays: dict, source_truth_observations: np.ndarray) -> dict:
    if call.get("kind") != "gradient":
        raise ValueError(f"selected inverse anchor is not an evaluated gradient: {call.get('label')}")
    delta = np.asarray(call.get("delta"), dtype=np.float64)
    if not np.array_equal(delta, controls):
        raise ValueError(f"inverse call delta mismatch for {call['label']}")
    meta, arrays, scalars, text, literal = _call_bundle(archive, prefix, call)
    expected_initial = base + B @ controls
    if not np.array_equal(literal, expected_initial):
        raise ValueError(f"inverse call {call['label']} initial state differs from absolute control chart")
    driver.validate_trial(meta, arrays, scalars, text,
                          expected_initial=expected_initial, center_arrays=None, gradient=True)
    y_inverse.require_inactive_extra_physics(meta)
    for key in driver.CONTEXT:
        if not np.array_equal(arrays.get(key), descriptor_arrays[key]):
            raise ValueError(f"inverse call {call['label']} context differs from descriptor: {key}")
    for index, key in enumerate(("observed_150", "observed_300")):
        if not np.array_equal(arrays.get(key), source_truth_observations[index]):
            raise ValueError(f"inverse call {call['label']} observations differ from source truth: {key}")
    return {"call": call, "delta": delta, "literal_initial": literal,
            "meta": meta, "arrays": arrays, "scalars": scalars, "text": text}


def _build_anchors(report: dict, source: dict, archive: zipfile.ZipFile,
                   prefix: str) -> dict:
    by_label = {call.get("label"): call for call in report.get("calls", [])}
    terminal = report.get("terminal_gradient", {})
    start_label = "gradient_u0"
    end_label = terminal.get("call_label")
    if start_label not in by_label or not end_label or end_label not in by_label:
        raise ValueError("inverse report lacks gradient_u0 or terminal gradient call")
    start_controls = np.asarray(report["starting_controls"], dtype=np.float64)
    end_controls = np.asarray(terminal.get("delta"), dtype=np.float64)
    if (terminal.get("reached") is not True or
            terminal.get("threshold") != y_inverse.GRADIENT_THRESHOLD or
            not float(terminal.get("gradient_l2", float("inf"))) < y_inverse.GRADIENT_THRESHOLD):
        raise ValueError("inverse terminal gradient did not reach the helper's declared threshold")
    if (not np.array_equal(end_controls, np.asarray(by_label[end_label].get("delta"), dtype=np.float64)) or
            not np.array_equal(end_controls, np.asarray(report.get("final_delta"), dtype=np.float64))):
        raise ValueError("terminal report/final delta differs from its linked gradient call")
    start = _load_inverse_call(archive, prefix, by_label[start_label],
                               source["equilibrium_base"], source["basis"], start_controls,
                               source["descriptor_arrays"], source["source_truth_observations"])
    end = _load_inverse_call(archive, prefix, by_label[end_label],
                             source["equilibrium_base"], source["basis"], end_controls,
                             source["descriptor_arrays"], source["source_truth_observations"])
    if np.array_equal(start_controls, end_controls):
        raise ValueError("terminal inverse state equals starting controls; no continuation to compare")
    anchors = {}
    for name, call_data in (("start", start), ("terminal", end)):
        arrays = call_data["arrays"]
        state_times = {0: arrays["initial_state"], 150: arrays["checkpoint_150"],
                       300: arrays["checkpoint_300"]}
        anchors[name] = {"controls": call_data["delta"], "call": call_data["call"],
                         "states": state_times, "predictions": {
                             150: arrays["predicted_150"], 300: arrays["predicted_300"]},
                         "context_arrays": arrays}
    return anchors


def _source_state_at(source: dict, time_s: int) -> np.ndarray:
    qtime = expm(float(time_s)*source["source_matrix"]) @ source["source_truth_state"]
    return source["equilibrium_base"] + ywave.pack_source_mode(source["column"], qtime, 12)


def _validate_static_context(arrays: dict, reference_arrays: dict, label: str) -> None:
    for key in (*driver.CONTEXT, "observed_150", "observed_300"):
        if key not in arrays or not np.array_equal(arrays[key], reference_arrays[key]):
            raise ValueError(f"forecast static context/input drift at {label}: {key}")


def _run_segments(exe: Path, outdir: Path, report: dict, source: dict,
                  anchors: dict, runner=None) -> None:
    if runner is None:
        runner = driver.invoke_native
    zero_basis = np.zeros((8480, 2), dtype=np.float64)
    zero_delta = np.zeros(2, dtype=np.float64)
    inputs = source["input_dir"]
    for branch in ("start", "terminal"):
        actual = anchors[branch]["states"]
        state_300 = actual[300]
        relative_reference = anchors[branch]["context_arrays"]
        for absolute_end in (600, 900):
            absolute_start = absolute_end - 300
            label = f"{branch}_{absolute_start}_to_{absolute_end}"
            result = runner(exe=exe, outdir=outdir, inputs=inputs,
                            state=state_300, basis=zero_basis, delta=zero_delta,
                            label=label, gradient=False,
                            center_arrays=None, report=report)
            meta, arrays, scalars, text = result
            driver.validate_trial(meta, arrays, scalars, text,
                                  expected_initial=state_300,
                                  center_arrays=None, gradient=False)
            y_inverse.require_inactive_extra_physics(meta)
            _validate_static_context(arrays, relative_reference, label)
            if not np.array_equal(arrays["initial_state"], state_300):
                raise ValueError(f"forecast {label} did not start from the previous actual checkpoint")
            call = report["calls"][-1]
            call.update({"absolute_start_s": absolute_start,
                         "absolute_end_s": absolute_end,
                         "forecast_scope": "cold_restart_state_only_forward",
                         "checkpoint_300_sha256": inverse.digest(arrays["checkpoint_300"]),
                         "checkpoint_150_sha256": inverse.digest(arrays["checkpoint_150"]),
                         "guards": {"physical_observation_domain": text["physical_observation_domain_guard"],
                                    "vertical_height_monotonic": text["vertical_height_monotonic_guard"],
                                    "admissibility_checked_every_step": meta.get("admissibility_checked_every_step")}})
            if absolute_end == 600:
                anchors[branch]["states"][600] = arrays["checkpoint_300"]
                anchors[branch]["predictions"][600] = arrays["predicted_300"]
            else:
                anchors[branch]["states"][900] = arrays["checkpoint_300"]
                anchors[branch]["predictions"][900] = arrays["predicted_300"]
            state_300 = arrays["checkpoint_300"]
            driver.write_json(outdir/"report.json", report)


def _comparison_report(report: dict, source: dict, anchors: dict) -> dict:
    result = {branch: {} for branch in ("start", "terminal")}
    source_states = {}
    for time_s in (0, 150, 300, 600, 900):
        source_states[time_s] = _source_state_at(source, time_s)
    for branch in ("start", "terminal"):
        for time_s in (0, 150, 300, 600, 900):
            if time_s not in anchors[branch]["states"]:
                raise ValueError(f"missing {branch} actual state at {time_s}s")
            result[branch][str(time_s)] = owned_state_metrics(
                anchors[branch]["states"][time_s], source_states[time_s])
    truth_600_900 = ywave.source_observations(
        source["column"], source["source_truth_state"], 12, source["xyz"],
        times=(600.0, 900.0), linear_geometry=False)
    pred_errors = {branch: {} for branch in ("start", "terminal")}
    for branch in ("start", "terminal"):
        for time_s in (150, 300, 600, 900):
            if time_s in (150, 300):
                truth = source["source_truth_observations"][0 if time_s == 150 else 1]
            else:
                truth = truth_600_900[0 if time_s == 600 else 1]
            pred = np.asarray(anchors[branch]["predictions"][time_s], dtype=np.float64)
            error = pred-truth
            rms = float(np.sqrt(np.mean(error*error)))
            pred_errors[branch][str(time_s)] = {
                "rms_m_s": rms, "max_abs_m_s": float(np.max(np.abs(error))),
                "rms_sigma": rms/OBSERVATION_SIGMA}
    return {"owned_state_error_vs_source_truth": result,
            "source_truth_state_absolute_control_coordinates": source["source_truth_controls"].tolist(),
            "actual_start_control_coordinates": anchors["start"]["controls"].tolist(),
            "actual_terminal_control_coordinates": anchors["terminal"]["controls"].tolist(),
            "physical_W_prediction_error_vs_source_truth": pred_errors,
            "source_truth_scope": "equilibrium descriptor base + linear 41-component source exp(A t), packed to the native grid; W observation samples include PH-perturbed vertical brackets. This is source-generated synthetic truth, not a nonlinear native trajectory.",
            "whole_state_scope": "per-variable owned-point spatial RMS/max. U excludes periodic duplicate x face; W/PH exclude fixed lower z face and include dynamic top; V includes physical Y faces; theta/MU include all mass points. No mixed-unit aggregate or fixed-Eulerian-height interpolation is claimed.",
            "cold_restart_scope": "two state-only native restarts per branch: actual inverse checkpoint at 300 to 600, then actual 600 checkpoint to 900; four calls total. No internal solver snapshot is restored.",
            "formal_Eg_bound": False, "optimizer_or_convergence_reassessment": False}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--accepted-zip", type=Path, required=True)
    parser.add_argument("--accepted-sha256", required=True)
    parser.add_argument("--outdir", type=Path, required=True)
    args = parser.parse_args()
    exe, accepted_zip, outdir = args.exe.resolve(), args.accepted_zip.resolve(), args.outdir.resolve()
    if not exe.is_file() or not exe.is_absolute():
        parser.error("--exe must name an existing absolute executable")
    if driver.sha(accepted_zip) != args.accepted_sha256:
        parser.error("--accepted-zip SHA256 differs from --accepted-sha256")
    if outdir.exists() and any(outdir.iterdir()):
        parser.error("--outdir must be new or empty")
    outdir.mkdir(parents=True, exist_ok=True)
    report_path = outdir/"report.json"
    report = {"schema": "y-wave-state-only-forecast-v1", "status": "preflight",
              "calls": [], "active_call": None,
              "accepted_zip_sha256": args.accepted_sha256,
              "forecast_source_revision": subprocess.check_output(
                  ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
              "forecast_executable_sha256": driver.sha(exe),
              "forecast_source_sha256": {p: driver.sha(ROOT/p) for p in SOURCE_PATHS},
              "platform": platform.platform(), "python": platform.python_version(),
              "numpy": np.__version__, "native_calls_started": False,
              "formal_Eg_bound": False}
    driver.write_json(report_path, report)
    try:
        with zipfile.ZipFile(accepted_zip) as archive:
            if archive.testzip() is not None:
                raise ValueError("accepted inverse ZIP failed CRC verification")
            parent, prefix, parent_sha = _read_report(archive)
            provenance = _verify_source_and_inputs(parent, archive, prefix, exe, args.accepted_sha256)
            source = _source_arrays(parent, provenance, prefix, archive, outdir)
            anchors = _build_anchors(parent, source, archive, prefix)
            report.update(status="accepted_inverse_verified",
                          inverse_report_sha256=parent_sha,
                          inverse_report_schema=parent["schema"],
                          inverse_status=parent["status"],
                          inverse_source_revision=provenance["source_revision"],
                          inverse_native_executable_sha256=provenance["executable_sha256"],
                          inverse_inputs_sha256=provenance["input_sha256"],
                          descriptor_csv_sha256=parent["descriptor_csv_sha256"],
                          source_truth_controls=source["source_truth_controls"].tolist(),
                          start_controls=anchors["start"]["controls"].tolist(),
                          terminal_controls=anchors["terminal"]["controls"].tolist(),
                          start_call_label=anchors["start"]["call"]["label"],
                          terminal_call_label=anchors["terminal"]["call"]["label"],
                          source_truth_reconstruction={"max_abs_m_s": source["source_truth_reconstruction_max_abs"],
                                         "roundoff_tolerance_m_s": source["source_truth_reconstruction_tolerance"]},
                          input_paths={"observations": str(source["input_dir"]/"observations.txt"),
                                       "descriptor": str(source["input_dir"]/"descriptor_16x12x8.csv"),
                                       "canonical_basis": str(source["input_dir"]/"canonical_basis.npy"),
                                       "source_H_BG": str(source["input_dir"]/"source_H_BG.npy"),
                                       "initial_state": str(source["input_dir"]/"initial_state.txt")},
                          native_calls_started=False)
            driver.write_json(report_path, report)
        # The four 960-step native forecast calls start only after all archive,
        # executable, source, basis, observations, and actual checkpoint gates pass.
        _run_segments(exe, outdir, report, source, anchors)
        comparison = _comparison_report(parent, source, anchors)
        report.update(status="completed_state_only_forecast", native_calls_started=True,
                      total_native_calls=len(report["calls"]), comparison=comparison,
                      final_executable_sha256=driver.sha(exe),
                      final_source_sha256={p: driver.sha(ROOT/p) for p in SOURCE_PATHS})
        driver.write_json(report_path, report)
        print(json.dumps({"status": report["status"], "calls": len(report["calls"]),
                          "report": str(report_path),
                          "formal_Eg_bound": False}, sort_keys=True))
        return 0
    except Exception as error:
        report.update(status="forecast_preflight_or_native_failure",
                      failure_type=type(error).__name__, failure=str(error),
                      native_calls_started=bool(report.get("calls")),
                      formal_Eg_bound=False)
        driver.write_json(report_path, report)
        raise


if __name__ == "__main__":
    raise SystemExit(main())
