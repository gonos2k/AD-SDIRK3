#!/usr/bin/env python3
"""Probe four fixed-chart gradient components with bounded-tape forwards only."""
import argparse
import hashlib
import json
import platform
from pathlib import Path
import subprocess
import sys
import time
import zipfile

import numpy as np
import scipy

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import test_fine_wave_inverse as inverse  # noqa: E402
import probe_fine_returned_accuracy as accuracy  # noqa: E402

FIXTURE_SHA = "e170be5d1e2b0d87efcdc9ebe0f4dd1d381aceb9e0777dcd074e0dcfe1329d8c"
CPP_SHA = "e1cb768ac81f10872817af3c05ed34da5ce6b5ca818b0eba462e12dcf05e2e3a"
H5_CSV_SHA = "c21feea5512d8c4974c3031a6b4f98e57d3a03f1280c0095d844272a6f0415d7"
GRID = (16, 12, 8)
SIGMA = 3e-4
EPSILONS = (0.005, 0.0025)
CONTEXT_ARRAYS = accuracy.CONTEXT_ARRAYS
TIME_LABEL = "Elapsed (wall clock) time (h:mm:ss or m:ss)"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_report(path, report):
    path.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")


def main(exe, fixture_zip, outdir):
    assert digest(fixture_zip) == FIXTURE_SHA, "unexpected pinned fixture"
    assert digest(ROOT / "external/libtorch_wrf/sdirk3/tests/test_native_wave_refinement.cpp") == CPP_SHA, "unexpected native test source"
    outdir.mkdir(parents=True, exist_ok=True)
    inputs = outdir / "inputs"
    inputs.mkdir(exist_ok=True)
    with zipfile.ZipFile(fixture_zip) as archive:
        assert archive.testzip() is None
        manifest = json.loads(archive.read("manifest.json"))
        assert manifest["schema"] == "pr282-fixed-literal-incremental-controls-v3"
        for name, record in manifest["files"].items():
            data = archive.read(name)
            assert hashlib.sha256(data).hexdigest() == record["sha256"], name
            (inputs / name).write_bytes(data)

    state = accuracy.load_vector(inputs / "initial_state.txt")
    baseline_path = inputs / "baseline_h5.csv"
    assert digest(baseline_path) == H5_CSV_SHA
    baseline = inverse.read_payload(baseline_path)
    desc_saved = inverse.read_payload(inputs / "descriptor_fine.csv")
    meta0, arrays0, scalars0, text0 = baseline
    basis = np.load(inputs / "canonical_basis.npy", allow_pickle=False)
    assert basis.shape == (8480, 4) and basis.dtype == np.float64 and np.isfinite(basis).all()
    assert inverse.digest(basis) == manifest["canonical_basis"]["array_sha256"]
    assert np.array_equal(state, arrays0["initial_state"])
    assert inverse.digest(state) == manifest["initial_array_sha256"]
    assert meta0["trajectory_steps"] == 60 and meta0["trajectory_dt_fp32"] == 5.0
    assert meta0["physical_observation_sigma_m_s"] == SIGMA
    assert meta0["forward_only"] == 0 and meta0["pullback_requested"] == 1
    assert text0["checkpoint_precision"] == "retained_fp64_trajectory"
    assert "initial_pullback" in arrays0 and np.isfinite(arrays0["initial_pullback"]).all()
    for key in CONTEXT_ARRAYS:
        assert np.array_equal(arrays0[key], desc_saved[1][key]), key
    projected_saved_gradient = basis.T @ arrays0["initial_pullback"]
    assert np.isfinite(projected_saved_gradient).all()

    report = {
        "schema": "fine-returned-gradient-components-v1",
        "status": "preflight",
        "source_revision": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "source_cpp_sha256": CPP_SHA,
        "executable_sha256": digest(exe),
        "fixture_sha256": FIXTURE_SHA,
        "h5_reference_csv_sha256": H5_CSV_SHA,
        "baseline_provenance": manifest,
        "canonical_basis_sha256": inverse.digest(basis),
        "local_chart": "x(j, +/- eps) = literal_saved_state +/- eps * canonical_basis[:,j]",
        "epsilons": list(EPSILONS),
        "coordinate_count": 4,
        "calls": [],
        "projected_saved_h5_gradient": projected_saved_gradient.tolist(),
        "claim": "four-coordinate local-chart finite-difference diagnostics; not full Eg certification or stationarity",
        "optimizer_run": False,
        "observations_changed": False,
        "platform": platform.platform(),
        "python": platform.python_version(),
        "numpy": np.__version__,
        "scipy": scipy.__version__,
    }
    target = outdir / "report.json"
    write_report(target, report)

    def invoke(label, state_path=None, bounded_tape=False, descriptor_only=False):
        output = outdir / f"{label}.csv"
        if descriptor_only:
            command = [str(exe), *map(str, GRID), str(output), "--descriptor-only"]
        else:
            command = [str(exe), *map(str, GRID), str(output),
                       "--physical-wave-inverse", str(state_path), str(inputs / "observations.txt"),
                       "60", "5.0", "--newton-tol", "1e-14", "--krylov-tol", "1e-12"]
            if bounded_tape:
                command.append("--bounded-tape-forward-only")
        usage = outdir / f"{label}.time.txt"
        timed_command = ["/usr/bin/time", "-v", "-o", str(usage), *command]
        started = time.monotonic()
        print(f"native start {label}", flush=True)
        result = subprocess.run(timed_command, capture_output=True, text=True)
        (outdir / f"{label}.stdout.log").write_text(result.stdout)
        (outdir / f"{label}.stderr.log").write_text(result.stderr)
        usage_text = usage.read_text() if usage.is_file() else ""
        call = {
            "label": label,
            "command": command,
            "timed_command": timed_command,
            "return_code": result.returncode,
            "seconds": time.monotonic() - started,
            "process_elapsed_wall": accuracy.parse_time_value(usage_text, TIME_LABEL),
            "peak_rss_kib": accuracy.parse_time_value(usage_text, "Maximum resident set size (kbytes)"),
            "time_report": str(usage),
            "output_sha256": digest(output) if output.is_file() else None,
        }
        report["calls"].append(call)
        write_report(target, report)
        print(f"native end {label} rc={result.returncode}", flush=True)
        if result.returncode:
            report.update(status="native_call_failed", failed_call=label)
            write_report(target, report)
            raise RuntimeError(f"{label} failed; no retry")
        try:
            accuracy.assert_time_metrics(call)
        except AssertionError as error:
            report.update(status="telemetry_failed", failed_call=label, failure=str(error))
            write_report(target, report)
            raise
        return inverse.read_payload(output)

    def require(condition, message, status, label=None):
        if not condition:
            report.update(status=status, failure=message)
            if label:
                report["failed_call"] = label
            write_report(target, report)
            raise AssertionError(message)

    def assert_bounded_metadata(payload, label, status="perturbation_validation_failed"):
        meta, _, _, text = payload
        expected = {
            "physical_wave_inverse": 1,
            "trajectory_steps": 60,
            "trajectory_dt_fp32": 5.0,
            "trajectory_seconds": 300,
            "observation_count_per_time": 105,
            "observation_time_150_seconds": 150,
            "observation_time_300_seconds": 300,
            "observation_xyz_from_input": 1,
            "observation_height_uses_current_PH": 1,
            "physical_observation_sigma_m_s": SIGMA,
            "physical_wave_newton_tol": float(np.float32(1e-14)),
            "physical_wave_krylov_tol": float(np.float32(1e-12)),
            "forward_only": 1,
            "retains_tape": 1,
            "tape_window_steps": 1,
            "pullback_requested": 0,
            "internal_fp64": 1,
            "internal_fp64_state_carry": 1,
            "retain_graph_for_adjoint": 1,
            "admissibility_checked_every_step": 1,
        }
        for key, value in expected.items():
            require(meta.get(key) == value, f"bounded-tape metadata mismatch {key}: {meta.get(key)} != {value}", status, label)
        require(text.get("execution_mode") == "single_step_tape_handoff", "bounded-tape execution mode mismatch", status, label)
        require(text.get("checkpoint_precision") == "single_step_retained_fp64_handoff", "bounded-tape checkpoint precision mismatch", status, label)
        require(text.get("physical_observation_domain_guard") == "passed", "physical observation guard failed", status, label)
        require(text.get("vertical_height_monotonic_guard") == "passed", "vertical height guard failed", status, label)

    # Confirm the fresh model context, then require bounded-tape h5 trajectory/J
    # parity before any perturbed bounded-tape finite-difference call is allowed.
    descriptor_path = outdir / "descriptor_fine.csv"
    desc = invoke("descriptor_fine", descriptor_only=True)
    for key in CONTEXT_ARRAYS:
        require(np.array_equal(desc[1][key], desc_saved[1][key]), f"context mismatch: {key}", "context_parity_failed", "descriptor_fine")
    report["context_exact_to_pinned_descriptor"] = True
    write_report(target, report)

    baseline_fresh = invoke("baseline_h5_parity", inputs / "initial_state.txt", bounded_tape=True)
    assert_bounded_metadata(baseline_fresh, "baseline_h5_parity", "baseline_parity_failed")
    _, arrays_base, scalars_base, _ = baseline_fresh
    require("initial_pullback" not in arrays_base, "baseline parity call unexpectedly performed a VJP", "baseline_parity_failed", "baseline_h5_parity")
    exact_keys = ("initial_state", "checkpoint_150", "checkpoint_300", "predicted_150", "predicted_300",
                  "observed_150", "observed_300", "bracket_index_150", "bracket_index_300")
    for key in exact_keys:
        require(np.array_equal(arrays_base[key], arrays0[key]), f"h5 exact parity failed: {key}", "baseline_parity_failed", "baseline_h5_parity")
    require(scalars_base["objective_physical_w"] == scalars0["objective_physical_w"], "h5 exact objective parity failed", "baseline_parity_failed", "baseline_h5_parity")
    report["baseline_h5_parity_gate"] = {
        "passed": True,
        "exact_arrays": list(exact_keys),
        "objective_exact": True,
        "objective": scalars0["objective_physical_w"],
        "output_sha256": report["calls"][-1]["output_sha256"],
    }
    write_report(target, report)

    predictions = {eps: {"plus": {}, "minus": {}} for eps in EPSILONS}
    unit_roundoff = np.finfo(np.float64).eps
    observation_count = 105
    gamma_n = observation_count * unit_roundoff / (1.0 - observation_count * unit_roundoff)
    for eps in EPSILONS:
        for j in range(basis.shape[1]):
            for sign, side in ((1.0, "plus"), (-1.0, "minus")):
                trial_state = state + sign * eps * basis[:, j]
                require(np.isfinite(trial_state).all(), f"nonfinite chart state eps={eps} j={j} {side}", "chart_state_invalid")
                state_path = outdir / f"state_eps{eps:g}_j{j}_{side}.txt"
                inverse.write_vector(state_path, trial_state)
                require(np.array_equal(accuracy.load_vector(state_path), trial_state), "serialized chart state changed", "chart_state_invalid")
                label = f"eps{eps:g}_j{j}_{side}"
                payload = invoke(label, state_path, True)
                assert_bounded_metadata(payload, label)
                meta, arrays, scalars, text = payload
                require("initial_pullback" not in arrays, "forward-only bounded call emitted a pullback", "perturbation_validation_failed", label)
                require(all(np.isfinite(value) for value in meta.values()), "nonfinite output metadata", "perturbation_validation_failed", label)
                require(np.array_equal(arrays["initial_state"], trial_state), "native initial state differs from requested chart point", "perturbation_validation_failed", label)
                require(all(np.isfinite(a).all() for a in arrays.values()), "nonfinite output array", "perturbation_validation_failed", label)
                require(all(np.isfinite(v) for v in scalars.values()), "nonfinite output scalar", "perturbation_validation_failed", label)
                for t in (150, 300):
                    require(np.array_equal(arrays[f"observed_{t}"], arrays0[f"observed_{t}"]), f"observations changed at {t}", "perturbation_validation_failed", label)
                    require(np.array_equal(arrays[f"bracket_index_{t}"], arrays0[f"bracket_index_{t}"]), f"observation bracket changed at {t}", "perturbation_validation_failed", label)
                predictions[eps][side][j] = arrays
                report["completed_perturbations"] = report.get("completed_perturbations", 0) + 1
                write_report(target, report)

    finite_differences = {}
    operand_floor_proxy = {}
    for eps in EPSILONS:
        per_time = {"150": [], "300": []}
        floor_per_time = {"150": [], "300": []}
        for j in range(basis.shape[1]):
            plus = predictions[eps]["plus"][j]
            minus = predictions[eps]["minus"][j]
            for t in (150, 300):
                p_plus = plus[f"predicted_{t}"]
                p_minus = minus[f"predicted_{t}"]
                observed = arrays0[f"observed_{t}"]
                r_plus = p_plus - observed
                r_minus = p_minus - observed
                residual_average = 0.5 * (r_plus + r_minus)
                prediction_difference = p_plus - p_minus
                denom = 2.0 * eps * SIGMA**2
                value = float(np.dot(residual_average, prediction_difference) / denom)
                # Dot reduction and operand-formation floor proxy only; it
                # excludes model, solver, and discretization errors.
                operand_proxy = unit_roundoff * (
                    float(np.dot(np.abs(residual_average), np.abs(p_plus) + np.abs(p_minus)))
                    + 0.5 * float(np.dot(np.abs(r_plus) + np.abs(r_minus), np.abs(prediction_difference)))
                ) + gamma_n * float(np.dot(np.abs(residual_average), np.abs(prediction_difference)))
                per_time[str(t)].append(value)
                floor_per_time[str(t)].append(operand_proxy / denom)
        time_vectors = {t: np.asarray(v, dtype=np.float64) for t, v in per_time.items()}
        floor_vectors = {t: np.asarray(v, dtype=np.float64) for t, v in floor_per_time.items()}
        finite_differences[str(eps)] = {
            "per_time_gradient": {t: v.tolist() for t, v in time_vectors.items()},
            "total_gradient": (time_vectors["150"] + time_vectors["300"]).tolist(),
        }
        operand_floor_proxy[str(eps)] = {
            "per_time": {t: v.tolist() for t, v in floor_vectors.items()},
            "total": (floor_vectors["150"] + floor_vectors["300"]).tolist(),
            "scope": "float64 operand-formation and dot-reduction proxy only; not a full error bound",
        }

    coarse_key, fine_key = (str(EPSILONS[0]), str(EPSILONS[1]))
    coarse = np.asarray(finite_differences[coarse_key]["total_gradient"])
    fine = np.asarray(finite_differences[fine_key]["total_gradient"])
    richardson = (4.0 * fine - coarse) / 3.0
    projected = projected_saved_gradient
    per_time_richardson = {}
    for t in ("150", "300"):
        coarse_t = np.asarray(finite_differences[coarse_key]["per_time_gradient"][t])
        fine_t = np.asarray(finite_differences[fine_key]["per_time_gradient"][t])
        per_time_richardson[t] = ((4.0 * fine_t - coarse_t) / 3.0).tolist()
    report.update(
        status="completed_four_coordinate_forward_diagnostic",
        finite_difference_vectors=finite_differences,
        per_time_richardson_gradient=per_time_richardson,
        richardson_gradient=richardson.tolist(),
        discrepancies_from_projected_saved_h5_gradient={
            coarse_key: (coarse - projected).tolist(),
            fine_key: (fine - projected).tolist(),
            "richardson": (richardson - projected).tolist(),
        },
        observed_width_difference_proxy={
            "fine_minus_coarse": (fine - coarse).tolist(),
            "absolute": np.abs(fine - coarse).tolist(),
            "per_time": {
                t: (np.asarray(finite_differences[fine_key]["per_time_gradient"][t])
                    - np.asarray(finite_differences[coarse_key]["per_time_gradient"][t])).tolist()
                for t in ("150", "300")
            },
            "scope": "difference between two central-difference widths; diagnostic proxy, not an error bound",
        },
        operand_floor_proxy=operand_floor_proxy,
        projection_basis_map="Z0(delta)=literal_saved_state+Bcan@delta; four canonical directions",
        gradient_certification="not claimed",
        full_Eg_certification="not established by this four-coordinate probe",
    )
    write_report(target, report)
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    main(args.exe.resolve(), args.fixture.resolve(), args.out.resolve())
