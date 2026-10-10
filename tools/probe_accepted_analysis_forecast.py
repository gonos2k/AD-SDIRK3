#!/usr/bin/env python3
"""Compare state-only cold restarts from the saved analyses at 600 and 900 s."""
from __future__ import annotations
import argparse, hashlib, json, subprocess, sys, time
from pathlib import Path

import numpy as np
from scipy.linalg import expm

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import test_fine_wave_inverse as inverse  # noqa: E402
import probe_fine_split_pullbacks as split  # noqa: E402
import probe_small_step_inverse as driver  # noqa: E402
import wave_quadratic_reference as source  # noqa: E402
import wave_energy_spatial_reference as source_core  # noqa: E402
import wave_quadratic_transport as source_transport  # noqa: E402
SOURCE_MODULES = (("wave_quadratic_reference", source), ("wave_energy_spatial_reference", source_core), ("wave_quadratic_transport", source_transport))
FOURTH_ZIP_SHA = "8dbe3f9d20ac2d5591cab455c67086978bd51676ed7b2acb8c167d370372bcbd"
PR283_ZIP_SHA = "ad31d82325c780230ff816d4c70340f9b3dfc09abce14f6bd8725448e3c69728"
EXE_SHA = "5f232add9d964b59f1af29402a534285a5d433d73e8f8e03dc8bf25cd9988bc0"
GRID = (16, 12, 8)
STEPS, DT, SIGMA = 960, 0.3125, 3.0e-4
CONTEXT = split.CONTEXT_ARRAYS

def sha(path: Path) -> str: return hashlib.sha256(path.read_bytes()).hexdigest()

def save(path: Path, value: dict) -> None: path.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n")

def source_module_receipt():
    return {name: {"path": str(Path(m.__file__).resolve()), "sha256": sha(Path(m.__file__).resolve())}
            for name, m in SOURCE_MODULES}


def complex_array_receipt(values: np.ndarray) -> dict:
    array = np.ascontiguousarray(values, dtype=np.complex128)
    return {"dtype": array.dtype.str, "shape": list(array.shape),
            "sha256": hashlib.sha256(array.tobytes()).hexdigest(),
            "real_sha256": hashlib.sha256(np.ascontiguousarray(array.real).tobytes()).hexdigest(),
            "imag_sha256": hashlib.sha256(np.ascontiguousarray(array.imag).tobytes()).hexdigest()}

def gradient_call(root: Path, report: dict, label: str):
    call = next(c for c in report["calls"] if c["label"] == label)
    csv = root / call.get("bundle_csv", f"{label}.csv")
    statefile = root / call.get("bundle_initial_state", f"{label}.initial_state.txt")
    state = split.read_literal_vector(statefile)
    if sha(csv) != call["output_sha256"] or inverse.digest(state) != call["state_sha256"]: raise ValueError(f"{label} state/CSV hash mismatch")
    meta, arrays, scalars, text = inverse.read_payload(csv)
    driver.validate_trial(meta, arrays, scalars, text, expected_initial=state, center_arrays=None, gradient=True)
    if not np.array_equal(arrays["initial_state"], state): raise ValueError(f"{label} CSV initial state mismatch")
    return call, statefile, state, arrays

def source_reference(coarse_csv: Path, fine_csv: Path, observations: Path) -> dict:
    coarse, fine = source.build_case(8, 4, native_csv=coarse_csv), source.build_case(16, 8, native_csv=fine_csv)
    _, fine_basis = inverse.profile_basis(coarse, fine)
    qtruth = sum((inverse.TRUTH[j] * fine_basis[j] for j in range(4)), np.zeros(4 * int(fine["nz"]) + 1, dtype=np.complex128))
    xyz, pinned_source = inverse.source_observation_data(fine, qtruth, (8, 6, 4), coarse)
    observed = np.loadtxt(observations, skiprows=2)
    if observed.shape != (105, 5) or not np.isfinite(observed).all() or not np.isfinite(pinned_source).all() or not np.array_equal(xyz, observed[:, :3]):
        raise ValueError("source observation locations differ from pinned inputs")
    max_obs_delta = float(np.max(np.abs(pinned_source.T - observed[:, 3:5])))
    source_tol = 128 * np.finfo(float).eps * max(1.0, float(np.max(np.abs(observed[:, 3:5]))))
    if max_obs_delta > source_tol:
        raise ValueError(f"source qtruth does not reproduce pinned 150/300 observations: {max_obs_delta:g}")
    A = source.source_matrix(fine)
    future = {t: inverse.source_physical_w_samples(fine, expm(t * A) @ qtruth,
              (16, 12, 8), xyz) for t in (600, 900)}
    if any(not np.isfinite(v).all() for v in future.values()): raise ValueError("source truth is nonfinite")
    return {"qtruth": qtruth, "future": future, "observed": observed, "max_obs_delta": max_obs_delta, "roundoff_tolerance": source_tol}

def error_metrics(prediction: np.ndarray, truth: np.ndarray) -> dict:
    delta = np.asarray(prediction) - np.asarray(truth); norm = float(np.linalg.norm(truth)); rms = float(np.sqrt(np.mean(delta * delta)))
    return {"error_max_abs_m_s": float(np.max(np.abs(delta))), "error_rms_m_s": rms, "error_rms_sigma": rms / SIGMA,
            "error_relative_l2": float(np.linalg.norm(delta) / norm) if norm else None}

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("exe", "accepted-zip", "baseline-zip", "outdir"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--accepted-sha256", required=True)
    args = parser.parse_args()
    out = args.outdir.resolve()
    out.mkdir(parents=True, exist_ok=False)
    report_path = out / "report.json"
    report = {"schema": "pr284-accepted-analysis-unused-time-forecast-v1",
              "status": "preflight", "native_calls_started": False,
              "protocol": "state-only cold restarts: t=300->600, then 600->900",
              "optimizer_run": False, "observation_file_changed": False,
              "objective_interpretation": "ignored on restart calls; CLI predictions are used only at absolute endpoints 600/900"}
    save(report_path, report)
    try:
        exe = args.exe.resolve()
        if not exe.is_absolute() or not exe.is_file() or sha(exe) != EXE_SHA:
            raise ValueError("--exe is not the pinned 5f232a native executable")
        if args.accepted_sha256 != FOURTH_ZIP_SHA or sha(args.accepted_zip) != FOURTH_ZIP_SHA:
            raise ValueError("accepted ZIP is not the pinned delta10 fourth artifact")
        if sha(args.baseline_zip) != PR283_ZIP_SHA:
            raise ValueError("baseline ZIP is not the pinned PR283 gradient_u0 artifact")
        accepted, ar, _ = driver.copy_resume_bundle(args.accepted_zip, FOURTH_ZIP_SHA, out / "accepted")
        baseline, br, _ = driver.copy_resume_bundle(args.baseline_zip, PR283_ZIP_SHA, out / "baseline")
        profiles = ((accepted, "completed_bounded_continuation"), (baseline, "completed_bounded_updates"))
        if any((r.get("status"), r.get("steps"), r.get("dt"), r.get("grid")) !=
               (status, STEPS, DT, list(GRID)) for r, status in profiles):
            raise ValueError("forecast source artifacts do not match the frozen h=.3125 profile")
        if any(r["executable_sha256"] != sha(exe) for r in (accepted, baseline)):
            raise ValueError("accepted and baseline executable hashes must match --exe")
        current_sources = driver.source_fingerprints()
        if accepted.get("source_sha256") != current_sources:
            raise ValueError("current accepted artifact source fingerprints differ from this worktree")
        old_sources = baseline.get("source_sha256", {})
        if any(old_sources.get(name) != current_sources[name] for name in driver.NATIVE_SOURCES):
            raise ValueError("PR283 baseline native source fingerprints differ from the accepted executable sources")
        if any(r["fixture_sha256"] != driver.FIXTURE_SHA for r in (accepted, baseline)):
            raise ValueError("fixed-state fixture hash mismatch")
        if accepted["input_sha256"] != baseline["input_sha256"]:
            raise ValueError("accepted and baseline fixed input hashes differ")
        inputs_a, inputs_b = ar / "inputs", br / "inputs"
        fixed_files = ("initial_state.txt", "observations.txt", "descriptor_coarse.csv", "descriptor_fine.csv")
        for name in fixed_files:
            if sha(inputs_a / name) != sha(inputs_b / name):
                raise ValueError(f"accepted/baseline fixed input differs: {name}")
        source = source_reference(inputs_a / "descriptor_coarse.csv", inputs_a / "descriptor_fine.csv",
                                  inputs_a / "observations.txt")
        state_label = accepted["terminal_gradient"]["call_label"]
        terminal, _, _, arrays_a = gradient_call(ar, accepted, state_label)
        if (terminal["kind"] != "gradient" or
                terminal["delta"] != accepted["final_delta"] or
                accepted["terminal_gradient"]["delta"] != accepted["final_delta"]):
            raise ValueError("accepted terminal gradient is not at the final delta10 state")
        baseline_call, _, _, arrays_b = gradient_call(br, baseline, "gradient_u0")
        if baseline_call["delta"] != [0.0, 0.0, 0.0, 0.0]:
            raise ValueError("PR283 baseline is not the saved-analysis gradient_u0 point")
        _, descriptor, _, _ = inverse.read_payload(inputs_a / "descriptor_fine.csv")
        if any(not np.array_equal(arrays_a[k], descriptor[k]) for k in CONTEXT):
            raise ValueError("accepted context differs from pinned fine descriptor")
        if (not np.array_equal(arrays_a["observed_150"], source["observed"][:, 3]) or
                not np.array_equal(arrays_a["observed_300"], source["observed"][:, 4])):
            raise ValueError("accepted observations differ from pinned observation file")
        for key in (*CONTEXT, "observed_150", "observed_300", "bracket_index_150", "bracket_index_300"):
            if not np.array_equal(arrays_a[key], arrays_b[key]):
                raise ValueError(f"saved-analysis contexts/observations differ: {key}")
        report["input_hashes"] = {key: {n: sha(directory / n) for n in fixed_files} for key, directory in (("accepted", inputs_a), ("baseline", inputs_b))}
        truth_hashes = {str(t): inverse.digest(v) for t, v in source["future"].items()}
        report.update({"source_git_head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
            "native_source_sha256": current_sources, "forecast_helper_sha256": sha(Path(__file__).resolve()),
            "source_reference_modules": source_module_receipt(),
            "executable_sha256": sha(exe), "accepted_zip_sha256": FOURTH_ZIP_SHA, "baseline_zip_sha256": PR283_ZIP_SHA,
            "accepted_delta": accepted["final_delta"], "accepted_cost_h03125": accepted["final_cost"], "accepted_gradient_l2": accepted["terminal_gradient"]["gradient_l2"],
            "source_truth_controls": inverse.TRUTH.tolist(), "source_qtruth": complex_array_receipt(source["qtruth"]),
            "source_observation_reconstruction_max_abs_m_s": source["max_obs_delta"],
            "source_observation_roundoff_tolerance_m_s": source["roundoff_tolerance"],
            "checkpoints_300_sha256": {"analysis": inverse.digest(arrays_a["checkpoint_300"]),
                                        "baseline": inverse.digest(arrays_b["checkpoint_300"])},
            "call_reports": [], "source_truth_metrics": {str(t): {"rms_m_s": float(np.sqrt(np.mean(v*v))),
                "max_abs_m_s": float(np.max(np.abs(v))), "array_sha256": truth_hashes[str(t)]}
                for t, v in source["future"].items()}})
        save(report_path, report)
        endpoint_predictions = {"analysis": {}, "baseline": {}}
        for branch, arrays0 in (("analysis", arrays_a), ("baseline", arrays_b)):
            state = arrays0["checkpoint_300"].copy()
            for segment, absolute_start, absolute_end in ((1, 300, 600), (2, 600, 900)):
                label = f"{branch}_segment{segment}_t{absolute_start}_to_{absolute_end}"
                statefile, output = out / f"{label}.initial_state.txt", out / f"{label}.csv"
                driver.write_vector(statefile, state)
                if not np.array_equal(split.read_literal_vector(statefile), state): raise ValueError(f"{label} literal checkpoint text did not roundtrip exactly")
                command = [str(exe), *map(str, GRID), str(output), "--physical-wave-inverse",
                    str(statefile), str(inputs_a / "observations.txt"), str(STEPS), str(DT),
                    "--newton-tol", "1e-14", "--krylov-tol", "1e-12", "--bounded-tape-forward-only"]
                usage = out / f"{label}.time.txt"
                timed = ["/usr/bin/time", "-v", "-o", str(usage), *command]
                call = {"label": label, "branch": branch, "absolute_start_s": absolute_start,
                        "absolute_end_s": absolute_end, "state_file_sha256": sha(statefile),
                        "state_array_sha256": inverse.digest(state), "command": command,
                        "timed_command": timed, "output_csv": str(output), "return_code": None}
                report.update(active_call=call, status="running", native_calls_started=True); save(report_path, report)
                start = time.monotonic(); result = subprocess.run(timed, capture_output=True, text=True)
                call["seconds"] = time.monotonic() - start
                (out / f"{label}.stdout.log").write_text(result.stdout); (out / f"{label}.stderr.log").write_text(result.stderr)
                call["return_code"] = result.returncode
                time_text = usage.read_text() if usage.exists() else ""
                call.update(process_elapsed_wall=driver.parse_time_value(time_text, "Elapsed (wall clock) time (h:mm:ss or m:ss)"),
                            peak_rss_kib=driver.parse_time_value(time_text, "Maximum resident set size (kbytes)"))
                call["output_sha256"] = sha(output) if output.is_file() else None
                report["call_reports"].append(call); report["active_call"] = None; save(report_path, report)
                if result.returncode:
                    raise RuntimeError(f"{label} failed with return code {result.returncode}")
                meta, arrays, scalars, text = inverse.read_payload(output)
                driver.validate_trial(meta, arrays, scalars, text, expected_initial=state, center_arrays=None, gradient=False)
                for key in (*CONTEXT, "observed_150", "observed_300"):
                    if not np.array_equal(arrays[key], arrays_a[key]): raise ValueError(f"{label} changed {key}")
                for t in (150, 300):
                    bracket = arrays[f"bracket_index_{t}"]
                    if bracket.shape != (420,) or not np.isfinite(bracket).all() or np.any(bracket != np.floor(bracket)) or np.any((bracket < 0) | (bracket >= GRID[2])): raise ValueError(f"{label} has invalid brackets at t={t}")
                    call[f"bracket_{t}_sha256"] = inverse.digest(bracket)
                endpoint = arrays["predicted_300"]; endpoint_predictions[branch][absolute_end] = endpoint.copy()
                call["endpoint_300_array_sha256"] = inverse.digest(arrays["checkpoint_300"])
                call["endpoint_prediction_sha256"] = inverse.digest(endpoint)
                call["validated_forward_metadata"] = True
                state = arrays["checkpoint_300"].copy()
                save(report_path, report)
        report["forecast_metrics"] = {branch: {str(t): error_metrics(pred, source["future"][t])
            for t, pred in endpoint_predictions[branch].items()} for branch in endpoint_predictions}
        report["baseline_vs_analysis"] = {str(t): error_metrics(endpoint_predictions["analysis"][t], endpoint_predictions["baseline"][t]) for t in (600, 900)}
        report.update(status="completed", restart_scope="state-only cold restarts; predictor caches are not serialized; not bitwise uninterrupted warm-trajectory continuation")
        save(report_path, report)
        return 0
    except Exception as error:
        report.update(status="failed", failure_type=type(error).__name__, failure=str(error))
        save(report_path, report)
        raise

if __name__ == "__main__":
    raise SystemExit(main())
