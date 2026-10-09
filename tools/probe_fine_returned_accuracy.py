#!/usr/bin/env python3
"""One fixed-state fine-grid timestep/VJP evaluation; no optimization."""
import argparse
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

FIXTURE_SHA = "4c2881bf1140ee42f2bc83642cb2669fc96bd853926b6a238a7d19f93491cbdd"
CPP_SHA = "db525e726ce28843282b1d479f6b8d94daf716b23fdc29378b9b542da5737424"
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


def run(exe, fixture_zip, outdir, schedule):
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
        assert manifest["schema"] == "pr282-exact-fine-returned-time-ladder-v2"
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
        result = subprocess.run(command, capture_output=True, text=True)
        (outdir / f"{label}.stdout.log").write_text(result.stdout)
        (outdir / f"{label}.stderr.log").write_text(result.stderr)
        report["calls"].append({"command": command, "return_code": result.returncode,
                                "seconds": time.monotonic()-started,
                                "output_sha256": sha(output) if output.is_file() else None})
        target.write_text(json.dumps(report, indent=2)+"\n")
        print(f"native end {label} rc={result.returncode}", flush=True)
        if result.returncode:
            report["status"] = "native_call_failed"
            target.write_text(json.dumps(report, indent=2)+"\n")
        assert result.returncode == 0, f"{label} failed; no retry"
        return inverse.read_payload(output)
    fresh = invoke("descriptor", ["--descriptor-only"])
    for key in CONTEXT_ARRAYS:
        assert np.array_equal(fresh[1][key], desc_saved[1][key]), f"context changed: {key}"
    coarse_case = inverse.reference.build_case(8, 4, native_csv=inputs / "descriptor_coarse.csv")
    fine_case = inverse.reference.build_case(16, 8, native_csv=inputs / "descriptor_fine.csv")
    _, basis = inverse.profile_basis(coarse_case, fine_case)
    B = np.column_stack([inverse.pack_source_mode(q, GRID) for q in basis])
    mapped = fresh[1]["base"] + B @ np.asarray(manifest["controls"])
    assert np.array_equal(mapped, state), "fresh basis must reproduce the exact returned state"
    g_previous = B.T @ baseline[1]["initial_pullback"]
    assert np.allclose(g_previous, manifest["gradients_by_previous_dt"][str(previous_dt)], rtol=0, atol=1e-12)
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
    p.add_argument("--schedule", choices=("h2.5", "h1.25"), default="h2.5")
    args = p.parse_args()
    run(args.exe.resolve(), args.fixture.resolve(), args.out.resolve(), args.schedule)
