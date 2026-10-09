#!/usr/bin/env python3
"""Fixed-state fine-grid accuracy diagnostics; no optimization."""
import argparse
import gzip
import hashlib
import json
import platform
from pathlib import Path
import subprocess
import time
import zipfile

import numpy as np
import scipy
import test_fine_wave_inverse as inverse

FIXTURE_SHA = "e170be5d1e2b0d87efcdc9ebe0f4dd1d381aceb9e0777dcd074e0dcfe1329d8c"
CPP_SHA = "a058e570e1527b5167c0f999188ecc3f520c48a6421cf18c8f2c7098320b9967"
HEADER_SHA = "244970b6c874ef50d3fc85477d463bc846c32b440a9ff834740590728cb9439f"
IMPL_SHA = "c0fc5c3fd7aa0e46c0f9f044e8405fc0f203209156f1a15a4629622a5cfac99c"
H125_CSV_SHA = "d1554e15ccfff09a3625f03a564b5ca8bd9ad6f469a1b4dac965e1ae5e28feec"
H0625_CSV_SHA = "f21fa165aa8534bc4370164ed464c7bb31ab1203394e071243524acd6f3dd391"
H0625_GZIP_SHA = "12e16ed872b2adc5e3430181d348cf94625a9d692400f9f408f01b2950ba3b26"
H0625_REPORT_SHA = "13f483e304d5fe6040172dad9079ffc97b66d9af3fea02fda632e670d745de10"
CONTEXT_ARRAYS = ("base", "phb", "pbase", "thbase_perturb", "mubase")
GRID = (16, 12, 8)
SIGMA = 3e-4
GRAD_TARGET = 1e-5


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_vector(path):
    words = path.read_text().split()
    value = np.asarray([float(x) for x in words[1:]], dtype=np.float64)
    assert int(words[0]) == value.size == 8480 and np.isfinite(value).all()
    return value


def parse_time_value(text, label):
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


def assert_time_metrics(call):
    assert isinstance(call["peak_rss_kib"], int) and call["peak_rss_kib"] > 0, "missing positive peak RSS from /usr/bin/time"
    assert isinstance(call["process_elapsed_wall"], str) and call["process_elapsed_wall"], "missing elapsed time from /usr/bin/time"


def run_bounded_carry_schedule(exe, fixture_zip, outdir, schedule):
    """Gate bounded-tape h2.5 parity before one explicit fine-step call."""
    schedules = {
        "carry-h.625": {"steps": 480, "dt": 0.625, "reference_dt": 1.25,
                        "reference_label": "h1.25", "reference_member": "baseline_h1_25.csv",
                        "run_label": "handoff_h0_625_N14_K12"},
        "carry-h.3125": {"steps": 960, "dt": 0.3125, "reference_dt": 0.625,
                         "reference_label": "h0.625", "run_label": "handoff_h0_3125_N14_K12"},
    }
    config = schedules[schedule]
    assert sha(fixture_zip) == FIXTURE_SHA
    outdir.mkdir(parents=True, exist_ok=True)
    inputs = outdir / "inputs"
    inputs.mkdir(exist_ok=True)
    with zipfile.ZipFile(fixture_zip) as z:
        assert z.testzip() is None
        manifest = json.loads(z.read("manifest.json"))
        assert manifest["schema"] == "pr282-fixed-literal-incremental-controls-v3"
        for name, record in manifest["files"].items():
            data = z.read(name)
            assert hashlib.sha256(data).hexdigest() == record["sha256"]
            (inputs / name).write_bytes(data)
    baseline_h25 = inverse.read_payload(inputs / "baseline_h2_5.csv")
    desc_saved = inverse.read_payload(inputs / "descriptor_fine.csv")
    state = load_vector(inputs / "initial_state.txt")
    assert np.array_equal(state, baseline_h25[1]["initial_state"])
    assert inverse.digest(state) == manifest["initial_array_sha256"]
    assert sha(inverse.ROOT / "external/libtorch_wrf/sdirk3/tests/test_native_wave_refinement.cpp") == CPP_SHA
    assert sha(inverse.ROOT / "external/libtorch_wrf/sdirk3/wrf_sdirk3_tile_unified.h") == HEADER_SHA
    assert sha(inverse.ROOT / "external/libtorch_wrf/sdirk3/wrf_sdirk3_tile_unified_impl.cpp") == IMPL_SHA
    assert baseline_h25[0]["physical_observation_sigma_m_s"] == SIGMA
    for key in CONTEXT_ARRAYS:
        assert np.array_equal(baseline_h25[1][key], desc_saved[1][key]), key

    if schedule == "carry-h.625":
        reference_path = inputs / config["reference_member"]
        assert sha(reference_path) == H125_CSV_SHA
        reference_gzip_sha = None
        reference_csv_sha = H125_CSV_SHA
    else:
        reference_gzip_path = inverse.ROOT / "tools/fixtures/pr282-h0625.csv.gz"
        reference_gzip_sha = H0625_GZIP_SHA
        assert sha(reference_gzip_path) == reference_gzip_sha
        reference_bytes = gzip.decompress(reference_gzip_path.read_bytes())
        reference_csv_sha = hashlib.sha256(reference_bytes).hexdigest()
        assert reference_csv_sha == H0625_CSV_SHA
        reference_path = inputs / "baseline_h0_625.csv"
        reference_path.write_bytes(reference_bytes)
    baseline_reference = inverse.read_payload(reference_path)
    meta_reference, arrays_reference, scalars_reference, text_reference = baseline_reference
    assert np.array_equal(state, arrays_reference["initial_state"])
    assert meta_reference["trajectory_steps"] == (240 if schedule == "carry-h.625" else 480)
    assert meta_reference["trajectory_dt_fp32"] == config["reference_dt"]
    assert meta_reference["physical_observation_sigma_m_s"] == SIGMA
    assert meta_reference["observation_count_per_time"] == 105
    assert meta_reference["observation_time_150_seconds"] == 150
    assert meta_reference["observation_time_300_seconds"] == 300
    assert text_reference["physical_observation_domain_guard"] == "passed"
    assert text_reference["vertical_height_monotonic_guard"] == "passed"
    assert all(np.isfinite(array).all() for array in arrays_reference.values())
    assert all(np.isfinite(value) for value in scalars_reference.values())
    if schedule == "carry-h.3125":
        assert meta_reference["forward_only"] == 1
        assert meta_reference["pullback_requested"] == 0
        assert meta_reference["tape_window_steps"] == 1
        assert meta_reference["internal_fp64_state_carry"] == 1
        assert meta_reference["retain_graph_for_adjoint"] == 1
        assert meta_reference["admissibility_checked_every_step"] == 1
        assert text_reference["execution_mode"] == "single_step_tape_handoff"
        assert text_reference["checkpoint_precision"] == "single_step_retained_fp64_handoff"
    for key in CONTEXT_ARRAYS:
        assert np.array_equal(arrays_reference[key], desc_saved[1][key]), key
    for t in (150, 300):
        assert np.array_equal(arrays_reference[f"observed_{t}"], baseline_h25[1][f"observed_{t}"])

    report = {"schema": "fine-returned-bounded-tape-forward-time-v1", "status": "preflight",
              "probe_schedule": {"steps": config["steps"], "dt": config["dt"]},
              "parity_gate_schedule": {"steps": 120, "dt": 2.5},
              "source_revision": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=inverse.ROOT, text=True).strip(),
              "source_cpp_sha256": CPP_SHA, "source_header_sha256": HEADER_SHA,
              "source_impl_sha256": IMPL_SHA, "executable_sha256": sha(exe),
              "fixture_sha256": FIXTURE_SHA,
              "comparison_reference": {"label": config["reference_label"],
                  "dt": config["reference_dt"], "csv_sha256": reference_csv_sha,
                  "gzip_sha256": reference_gzip_sha,
                  "source_run": 37905770290 if schedule == "carry-h.625" else 37911606374,
                  "source_csv_path": manifest["files"]["baseline_h1_25.csv"]["source_member"] if schedule == "carry-h.625" else "/private/tmp/pr282-h0625-37911606374/measurement/handoff_h0_625_N14_K12.csv",
                  "source_report_path": "/private/tmp/pr282-h125-37905770290/measurement/report.json" if schedule == "carry-h.625" else "/private/tmp/pr282-h0625-37911606374/measurement/report.json",
                  "source_report_sha256": "e86d065c732b0c941a133427ca28422dd1089b168b1e78c052c4c3e2ab1d0e8b" if schedule == "carry-h.625" else H0625_REPORT_SHA,
                  "source_revision": "d84315cb861138452757cf0fb5180677d592ecef" if schedule == "carry-h.625" else "b1a1a5e39e7df4197d7353671c7b17eef6326ac2",
                  "source_cpp_sha256": CPP_SHA,
                  "source_executable_sha256": "3e0f041a04ffb915e79d5e4cefcdad27b321ba58a28d631274805f61ba06c46c" if schedule == "carry-h.625" else "a33a1a9bbb2d5e474c363c6c53e2adbd6ffe9f4b8bfc8a587c3ea5a5452ad4a7"},
              "baseline_provenance": manifest, "platform": platform.platform(),
              "python": platform.python_version(), "numpy": np.__version__, "scipy": scipy.__version__,
              "calls": [], "claim": "forward time sensitivity only; no gradient, VJP, or stationarity claim",
              "optimization_run": False, "observations_changed": False}
    target = outdir / "report.json"

    def invoke(label, steps, dt):
        output = outdir / f"{label}.csv"
        command = [str(exe), *map(str, GRID), str(output),
                   "--physical-wave-inverse", str(inputs / "initial_state.txt"),
                   str(inputs / "observations.txt"), str(steps), str(dt),
                   "--newton-tol", "1e-14", "--krylov-tol", "1e-12",
                   "--bounded-tape-forward-only"]
        usage = outdir / f"{label}.time.txt"
        timed_command = ["/usr/bin/time", "-v", "-o", str(usage), *command]
        started = time.monotonic()
        print(f"native start {label}", flush=True)
        result = subprocess.run(timed_command, capture_output=True, text=True)
        (outdir / f"{label}.stdout.log").write_text(result.stdout)
        (outdir / f"{label}.stderr.log").write_text(result.stderr)
        usage_text = usage.read_text() if usage.is_file() else ""
        call = {"command": command, "timed_command": timed_command,
                "return_code": result.returncode, "seconds": time.monotonic()-started,
                "process_elapsed_wall": parse_time_value(usage_text, "Elapsed (wall clock) time (h:mm:ss or m:ss)"),
                "peak_rss_kib": parse_time_value(usage_text, "Maximum resident set size (kbytes)"),
                "time_report": str(usage),
                "output_sha256": sha(output) if output.is_file() else None}
        report["calls"].append(call)
        target.write_text(json.dumps(report, indent=2)+"\n")
        print(f"native end {label} rc={result.returncode}", flush=True)
        if result.returncode:
            report["status"] = "native_call_failed"
            target.write_text(json.dumps(report, indent=2)+"\n")
        assert result.returncode == 0, f"{label} failed; no retry"
        assert_time_metrics(call)
        return inverse.read_payload(output)

    def assert_bounded_tape_metadata(payload, steps, dt):
        meta, _, _, text = payload
        expected = {"trajectory_steps": steps, "trajectory_dt_fp32": dt,
                    "trajectory_seconds": 300, "observation_count_per_time": 105,
                    "observation_time_150_seconds": 150, "observation_time_300_seconds": 300,
                    "physical_observation_sigma_m_s": SIGMA,
                    "physical_wave_newton_tol": float(np.float32(1e-14)),
                    "physical_wave_krylov_tol": float(np.float32(1e-12)),
                    "forward_only": 1, "retains_tape": 1, "tape_window_steps": 1,
                    "pullback_requested": 0, "internal_fp64": 1,
                    "internal_fp64_state_carry": 1, "retain_graph_for_adjoint": 1,
                    "admissibility_checked_every_step": 1}
        for key, value in expected.items():
            assert meta.get(key) == value, f"{key}: got {meta.get(key)}, expected {value}"
        assert text["checkpoint_precision"] == "single_step_retained_fp64_handoff"
        assert text["execution_mode"] == "single_step_tape_handoff"
        assert text["physical_observation_domain_guard"] == "passed"
        assert text["objective_normalization"] == "0.5_sum_over_times_and_points_of_residual_over_sigma_squared"

    # This short call is the hard gate. No selected long call occurs unless the
    # bounded single-step tape handoff reproduces the pinned retained h2.5 result.
    h25 = invoke("handoff_h2_5_N14_K12", 120, 2.5)
    try:
        assert_bounded_tape_metadata(h25, 120, 2.5)
        _, arrays25, scalars25, _ = h25
        _, arrays_ref, scalars_ref, _ = baseline_h25
        assert np.array_equal(arrays25["initial_state"], state)
        for key in ("checkpoint_150", "checkpoint_300", "predicted_150", "predicted_300",
                    "observed_150", "observed_300", "bracket_index_150", "bracket_index_300"):
            assert np.array_equal(arrays25[key], arrays_ref[key]), f"h2.5 parity gate failed: {key}"
        assert scalars25["objective_physical_w"] == scalars_ref["objective_physical_w"], "h2.5 objective parity gate failed"
    except AssertionError as error:
        report.update(status="h2_5_parity_gate_failed", h2_5_parity_error=str(error))
        target.write_text(json.dumps(report, indent=2)+"\n")
        raise
    report["h2_5_parity_gate"] = {"passed": True,
        "exact_arrays": ["initial_state", "checkpoint_150", "checkpoint_300", "predicted_150",
                         "predicted_300", "observed_150", "observed_300", "bracket_index_150", "bracket_index_300"],
        "objective_exact": True, "baseline_objective": scalars_ref["objective_physical_w"],
        "handoff_objective": scalars25["objective_physical_w"],
        "output_sha256": report["calls"][-1]["output_sha256"]}
    target.write_text(json.dumps(report, indent=2)+"\n")

    # Recheck the solver's freshly constructed model context only after the
    # required 120-step parity gate, and still before the selected long call.
    descriptor_path = outdir / "descriptor_fine.csv"
    descriptor_command = [str(exe), *map(str, GRID), str(descriptor_path), "--descriptor-only"]
    descriptor_usage = outdir / "descriptor_fine.time.txt"
    timed_descriptor_command = ["/usr/bin/time", "-v", "-o", str(descriptor_usage), *descriptor_command]
    started = time.monotonic()
    descriptor_result = subprocess.run(timed_descriptor_command, capture_output=True, text=True)
    (outdir / "descriptor_fine.stdout.log").write_text(descriptor_result.stdout)
    (outdir / "descriptor_fine.stderr.log").write_text(descriptor_result.stderr)
    usage_text = descriptor_usage.read_text() if descriptor_usage.is_file() else ""
    report["calls"].append({"command": descriptor_command, "timed_command": timed_descriptor_command,
        "return_code": descriptor_result.returncode, "seconds": time.monotonic()-started,
        "process_elapsed_wall": parse_time_value(usage_text, "Elapsed (wall clock) time (h:mm:ss or m:ss)"),
        "peak_rss_kib": parse_time_value(usage_text, "Maximum resident set size (kbytes)"),
        "time_report": str(descriptor_usage),
        "output_sha256": sha(descriptor_path) if descriptor_path.is_file() else None})
    target.write_text(json.dumps(report, indent=2)+"\n")
    assert descriptor_result.returncode == 0, "fresh descriptor call failed; no long call"
    assert_time_metrics(report["calls"][-1])
    fresh_descriptor = inverse.read_payload(descriptor_path)
    for key in CONTEXT_ARRAYS:
        assert np.array_equal(fresh_descriptor[1][key], desc_saved[1][key]), f"context changed: {key}"
    report["context_exact_to_pinned_descriptor"] = True
    target.write_text(json.dumps(report, indent=2)+"\n")

    fine = invoke(config["run_label"], config["steps"], config["dt"])
    assert_bounded_tape_metadata(fine, config["steps"], config["dt"])
    meta, arrays, scalars, _ = fine
    _, arrays_reference, scalars_reference, _ = baseline_reference
    assert np.array_equal(arrays["initial_state"], state)
    assert all(np.isfinite(a).all() for a in arrays.values())
    assert all(np.isfinite(value) for value in scalars.values())
    times = {}
    for t in (150, 300):
        key = f"predicted_{t}"
        assert np.array_equal(arrays[f"observed_{t}"], arrays_reference[f"observed_{t}"])
        delta = arrays[key] - arrays_reference[key]
        residual = arrays_reference[key] - arrays_reference[f"observed_{t}"]
        rms = float(np.sqrt(np.mean(delta**2)))
        cross = float(np.dot(residual, delta)/SIGMA**2)
        quadratic = float(0.5*np.dot(delta, delta)/SIGMA**2)
        times[str(t)] = {"rms_probe_minus_reference_m_s": rms,
            "rms_probe_minus_reference_sigma": rms/SIGMA,
            "cost_delta_cross": cross, "cost_delta_quadratic": quadratic,
            "stable_cost_delta": cross+quadratic,
            "brackets_match_reference": bool(np.array_equal(arrays[f"bracket_index_{t}"], arrays_reference[f"bracket_index_{t}"]))}
    report.update(status="completed_forward_time_evaluation", times=times,
        comparison_reference_label=config["reference_label"],
        comparison_reference_dt=config["reference_dt"],
        objective_reference=scalars_reference["objective_physical_w"],
        objective_probe=scalars["objective_physical_w"],
        recorded_cost_delta=scalars["objective_physical_w"]-scalars_reference["objective_physical_w"],
        stable_cost_delta_sum=sum(row["stable_cost_delta"] for row in times.values()),
        prediction_target_kind="diagnostic per-time physical RMS; not an error bound",
        predeclared_prediction_target_sigma=0.1,
        prediction_target_met=all(row["rms_probe_minus_reference_sigma"] < 0.1 for row in times.values()),
        forward_only=True, vjp_performed=False, gradient_claimed=False,
        basis_sha256=manifest["canonical_basis"]["array_sha256"])
    target.write_text(json.dumps(report, indent=2)+"\n")
    return report


def run(exe, fixture_zip, outdir, schedule):
    if schedule in ("carry-h.625", "carry-h.3125"):
        return run_bounded_carry_schedule(exe, fixture_zip, outdir, schedule)
    steps, dt = {"h2.5": (120, 2.5), "h1.25": (240, 1.25)}[schedule]
    previous_dt = 5.0 if dt == 2.5 else 2.5
    previous_file = "baseline_h5.csv" if dt == 2.5 else "baseline_h2_5.csv"
    older_file = "baseline_h10.csv" if dt == 2.5 else "baseline_h5.csv"
    assert sha(fixture_zip) == FIXTURE_SHA
    outdir.mkdir(parents=True, exist_ok=True)
    inputs = outdir / "inputs"
    inputs.mkdir(exist_ok=True)
    with zipfile.ZipFile(fixture_zip) as z:
        assert z.testzip() is None
        manifest = json.loads(z.read("manifest.json"))
        assert manifest["schema"] == "pr282-fixed-literal-incremental-controls-v3"
        for name, record in manifest["files"].items():
            data = z.read(name)
            assert hashlib.sha256(data).hexdigest() == record["sha256"]
            (inputs / name).write_bytes(data)
    baseline = inverse.read_payload(inputs / previous_file)
    coarse_time = inverse.read_payload(inputs / older_file)
    desc_saved = inverse.read_payload(inputs / "descriptor_fine.csv")
    state = load_vector(inputs / "initial_state.txt")
    assert np.array_equal(state, baseline[1]["initial_state"])
    assert inverse.digest(state) == manifest["initial_array_sha256"]
    assert sha(inverse.ROOT / "external/libtorch_wrf/sdirk3/tests/test_native_wave_refinement.cpp") == CPP_SHA
    assert baseline[0]["physical_observation_sigma_m_s"] == SIGMA
    for key in CONTEXT_ARRAYS:
        assert np.array_equal(baseline[1][key], desc_saved[1][key]), key
    report = {"schema": "fine-returned-fixed-time-v2", "status": "preflight",
              "probe_dt": dt, "previous_dt": previous_dt,
              "source_revision": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=inverse.ROOT, text=True).strip(),
              "source_cpp_sha256": CPP_SHA, "executable_sha256": sha(exe),
              "fixture_sha256": FIXTURE_SHA, "baseline_provenance": manifest,
              "platform": platform.platform(), "calls": [],
              "python": platform.python_version(), "numpy": np.__version__, "scipy": scipy.__version__,
              "predeclared_prediction_target_sigma": 0.1,
              "prediction_target_kind": "diagnostic engineering target, not an error bound",
              "predeclared_gradient_target": GRAD_TARGET,
              "claim": "single halving sensitivity; no order/continuum/error-bound claim"}
    target = outdir / "report.json"
    def invoke(label, args):
        output = outdir / f"{label}.csv"
        command = [str(exe), *map(str, GRID), str(output), *args]
        started = time.monotonic()
        print(f"native start {label}", flush=True)
        usage = outdir / f"{label}.time.txt"
        timed_command = ["/usr/bin/time", "-v", "-o", str(usage), *command]
        result = subprocess.run(timed_command, capture_output=True, text=True)
        (outdir / f"{label}.stdout.log").write_text(result.stdout)
        (outdir / f"{label}.stderr.log").write_text(result.stderr)
        usage_text = usage.read_text() if usage.is_file() else ""
        report["calls"].append({"command": command, "timed_command": timed_command,
                                "return_code": result.returncode,
                                "seconds": time.monotonic()-started,
                                "process_elapsed_wall": parse_time_value(usage_text, "Elapsed (wall clock) time (h:mm:ss or m:ss)"),
                                "peak_rss_kib": parse_time_value(usage_text, "Maximum resident set size (kbytes)"),
                                "time_report": str(usage),
                                "output_sha256": sha(output) if output.is_file() else None})
        target.write_text(json.dumps(report, indent=2)+"\n")
        print(f"native end {label} rc={result.returncode}", flush=True)
        if result.returncode:
            report["status"] = "native_call_failed"
            target.write_text(json.dumps(report, indent=2)+"\n")
        assert result.returncode == 0, f"{label} failed; no retry"
        assert_time_metrics(report["calls"][-1])
        return inverse.read_payload(output)
    fresh = invoke("descriptor", ["--descriptor-only"])
    for key in CONTEXT_ARRAYS:
        assert np.array_equal(fresh[1][key], desc_saved[1][key]), f"context changed: {key}"
    # The physical center is the literal saved state. Directions are immutable
    # inputs: Z0(delta)=state+B@delta, delta=0. No eigensolver or state remap.
    B = np.load(inputs / "canonical_basis.npy", allow_pickle=False)
    assert B.shape == (8480, 4) and B.dtype == np.float64 and np.isfinite(B).all()
    assert inverse.digest(B) == manifest["canonical_basis"]["array_sha256"]
    g_previous = B.T @ baseline[1]["initial_pullback"]
    g_saved = np.asarray(manifest["canonical_gradients_by_dt"][str(previous_dt)])
    eps = np.finfo(float).eps
    gamma = (2*8480*eps)/(1-2*8480*eps)
    projection_bound = gamma*np.sum(np.abs(B*baseline[1]["initial_pullback"][:, None]), axis=0)
    assert np.all(np.abs(g_previous-g_saved) <= projection_bound)
    older_gradient = None
    if dt == 1.25:
        older_gradient = B.T @ coarse_time[1]["initial_pullback"]
        older_saved = np.asarray(manifest["canonical_gradients_by_dt"]["5.0"])
        older_bound = gamma*np.sum(np.abs(B*coarse_time[1]["initial_pullback"][:, None]), axis=0)
        assert np.all(np.abs(older_gradient-older_saved) <= older_bound)
        report["older_gradient_projection_arithmetic_bound"] = older_bound.tolist()
        report["older_gradient_canonical"] = older_gradient.tolist()
    report.update(canonical_basis_sha256=inverse.digest(B),
        control_map="Z0(delta)=literal_saved_state+Bcan@delta; center delta=0",
        original_absolute_coordinate_equivalence_claimed=False,
        previous_gradient_projection_error=(g_previous-g_saved).tolist(),
        previous_gradient_projection_arithmetic_bound=projection_bound.tolist())
    target.write_text(json.dumps(report, indent=2)+"\n")
    current = invoke(f"h{dt:g}_N14_K12", ["--physical-wave-inverse", str(inputs/"initial_state.txt"),
                     str(inputs/"observations.txt"), str(steps), str(dt), "--newton-tol", "1e-14",
                     "--krylov-tol", "1e-12"])
    meta, arrays, scalars, text = current
    assert meta["trajectory_steps"] == steps and meta["trajectory_dt_fp32"] == dt
    assert meta["trajectory_seconds"] == 300 and meta["observation_count_per_time"] == 105
    assert meta["observation_time_150_seconds"] == 150 and meta["observation_time_300_seconds"] == 300
    assert meta["physical_observation_sigma_m_s"] == SIGMA
    assert meta["physical_wave_newton_tol"] == float(np.float32(1e-14))
    assert meta["physical_wave_krylov_tol"] == float(np.float32(1e-12))
    assert meta["forward_only"] == 0 and meta["pullback_requested"] == 1
    assert text["checkpoint_precision"] == "retained_fp64_trajectory"
    assert text["physical_observation_domain_guard"] == "passed"
    assert text["objective_normalization"] == "0.5_sum_over_times_and_points_of_residual_over_sigma_squared"
    assert np.array_equal(arrays["initial_state"], state)
    assert all(np.isfinite(a).all() for a in arrays.values())
    assert all(np.isfinite(value) for value in scalars.values())
    g_probe = B.T @ arrays["initial_pullback"]
    single_gamma = (8480*eps)/(1-8480*eps)
    new_projection_bound = single_gamma*np.sum(np.abs(B*arrays["initial_pullback"][:, None]), axis=0)
    times = {}
    for t in (150, 300):
        key = f"predicted_{t}"
        assert np.array_equal(arrays[f"observed_{t}"], baseline[1][f"observed_{t}"])
        delta = arrays[key] - baseline[1][key]
        older_delta = baseline[1][key] - coarse_time[1][key]
        residual = baseline[1][key] - arrays[f"observed_{t}"]
        cross = float(np.dot(residual, delta)/SIGMA**2)
        quadratic = float(0.5*np.dot(delta, delta)/SIGMA**2)
        times[str(t)] = {"rms_probe_minus_previous_m_s": float(np.sqrt(np.mean(delta**2))),
            "rms_probe_minus_previous_sigma": float(np.sqrt(np.mean(delta**2))/SIGMA),
            "rms_previous_minus_older_sigma": float(np.sqrt(np.mean(older_delta**2))/SIGMA),
            "cost_delta_cross": cross, "cost_delta_quadratic": quadratic,
            "stable_cost_delta": cross+quadratic,
            "brackets_match_previous": bool(np.array_equal(arrays[f"bracket_index_{t}"], baseline[1][f"bracket_index_{t}"]))}
    margin = GRAD_TARGET - np.linalg.norm(g_previous)
    report.update(status="completed_fixed_state_evaluation", times=times,
        objective_previous=baseline[2]["objective_physical_w"], objective_probe=scalars["objective_physical_w"],
        gradient_previous=g_previous.tolist(), gradient_probe=g_probe.tolist(), gradient_probe_norm=float(np.linalg.norm(g_probe)),
        gradient_delta_norm=float(np.linalg.norm(g_probe-g_previous)), remaining_gradient_margin=float(margin),
        gradient_probe_projection_arithmetic_bound=new_projection_bound.tolist(),
        projection_bound_scope="dot reduction only; not Newton/RHS/time/model-gradient error",
        raw_gate_test_applicable=bool(margin>0),
        timestep_gate_robust=bool(margin>0 and np.linalg.norm(g_probe-g_previous)<margin and np.linalg.norm(g_probe)<GRAD_TARGET),
        prediction_target_met=all(row["rms_probe_minus_previous_sigma"]<0.1 for row in times.values()),
        basis_sha256=inverse.digest(B), optimization_run=False, observations_changed=False)
    report["stable_cost_delta_sum"] = sum(row["stable_cost_delta"] for row in times.values())
    report["recorded_cost_delta"] = scalars["objective_physical_w"]-baseline[2]["objective_physical_w"]
    target.write_text(json.dumps(report, indent=2)+"\n")
    return report


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--exe", type=Path, required=True)
    p.add_argument("--fixture", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--schedule", choices=("h2.5", "h1.25", "carry-h.625", "carry-h.3125"), default="h2.5")
    args = p.parse_args()
    run(args.exe.resolve(), args.fixture.resolve(), args.out.resolve(), args.schedule)
