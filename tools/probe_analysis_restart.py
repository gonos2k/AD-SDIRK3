#!/usr/bin/env python3
"""Compare 16 h=.3125 steps with live, restored, and cold Newton carried state."""
from __future__ import annotations
import argparse, hashlib, json, subprocess, sys, time
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import test_fine_wave_inverse as inverse  # noqa: E402
import probe_fine_split_pullbacks as split  # noqa: E402
import probe_small_step_inverse as driver  # noqa: E402

ACCEPTED_SHA = "8dbe3f9d20ac2d5591cab455c67086978bd51676ed7b2acb8c167d370372bcbd"
GRID, STEPS, DT = (16, 12, 8), 960, 0.3125
COMPONENTS = ("u", "v", "w", "ph", "theta", "mu")
BRANCHES = ("warm_live", "snapshot_restored", "cold_restart")
ALLOW_TEST_CPP = "external/libtorch_wrf/sdirk3/tests/test_native_wave_refinement.cpp"
FORECAST_RMS = {600: 2.9791482084007838e-6, 900: 7.710417499126856e-6}

def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()

def save(path: Path, value: dict) -> None:
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n")

def invoke(label: str, exe: Path, args: list[str], out: Path, report: dict) -> tuple[dict,dict,dict,dict]:
    csv = out / f"{label}.csv"
    command = [str(exe), *map(str, GRID), str(csv), *args]
    usage = out / f"{label}.time.txt"
    timed = ["/usr/bin/time", "-v", "-o", str(usage), *command]
    call = {"label": label, "command": command, "timed_command": timed,
            "output_csv": str(csv), "return_code": None}
    report.update(status="running", native_calls_started=True, active_call=call)
    save(out / "report.json", report)
    start = time.monotonic()
    result = subprocess.run(timed, capture_output=True, text=True)
    call["seconds"] = time.monotonic() - start
    (out / f"{label}.stdout.log").write_text(result.stdout)
    (out / f"{label}.stderr.log").write_text(result.stderr)
    call["return_code"] = result.returncode
    time_text = usage.read_text() if usage.exists() else ""
    call["process_elapsed_wall"] = driver.parse_time_value(
        time_text, "Elapsed (wall clock) time (h:mm:ss or m:ss)")
    call["peak_rss_kib"] = driver.parse_time_value(
        time_text, "Maximum resident set size (kbytes)")
    call["output_sha256"] = sha(csv) if csv.is_file() else None
    report.setdefault("native_calls", []).append(call)
    report["active_call"] = None
    save(out / "report.json", report)
    if result.returncode:
        raise RuntimeError(f"{label} returned {result.returncode}: {result.stderr[-2000:]}")
    return inverse.read_payload(csv)

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--accepted-zip", type=Path, required=True)
    parser.add_argument("--accepted-sha256", required=True)
    parser.add_argument("--outdir", type=Path, required=True)
    args = parser.parse_args()
    out = args.outdir.resolve()
    out.mkdir(parents=True, exist_ok=False)
    report = {"schema": "pr286-analysis-restart-smoke-v1", "status": "preflight",
              "native_calls_started": False, "optimizer_run": False,
              "observation_file_changed": False,
              "protocol": "reproduce literal 0->300 analysis, then compare three 300->305 state branches",
              "pullback_requested": False}
    save(out / "report.json", report)
    try:
        exe = args.exe.resolve()
        if not exe.is_absolute() or not exe.is_file():
            raise ValueError("--exe must be an existing absolute executable")
        if args.accepted_sha256 != ACCEPTED_SHA or sha(args.accepted_zip) != ACCEPTED_SHA:
            raise ValueError("accepted ZIP is not the SHA-pinned delta10 terminal artifact")
        accepted, bundle, report_sha = driver.copy_resume_bundle(
            args.accepted_zip, ACCEPTED_SHA, out / "accepted")
        if (accepted.get("status"), accepted.get("steps"), accepted.get("dt"), accepted.get("grid")) != (
                "completed_bounded_continuation", STEPS, DT, list(GRID)):
            raise ValueError("accepted artifact profile does not match 960 x .3125 on 16x12x8")
        if accepted["fixture_sha256"] != driver.FIXTURE_SHA or accepted["fd_zip_sha256"] != driver.FD_ZIP_SHA:
            raise ValueError("accepted artifact fixture/FD source pins differ")
        current = driver.source_fingerprints()
        archived = accepted["source_sha256"]
        if set(current) != set(archived):
            raise ValueError("source fingerprint inventory differs from accepted artifact")
        changed = [name for name in current if current[name] != archived[name]]
        if changed != [ALLOW_TEST_CPP]:
            raise ValueError(f"only the test CPP may differ from production-qualified sources: {changed}")
        inputs = bundle / "inputs"
        input_names = ("initial_state.txt", "observations.txt", "descriptor_coarse.csv", "descriptor_fine.csv")
        input_hashes = {name: sha(inputs / name) for name in input_names}
        if input_hashes["initial_state.txt"] != accepted["input_sha256"]["initial_state"] or \
                input_hashes["observations.txt"] != accepted["input_sha256"]["observations"]:
            raise ValueError("accepted literal state or observations differ from their pinned input hashes")
        call_label = accepted["terminal_gradient"]["call_label"]
        terminal_call = next(c for c in accepted["calls"] if c["label"] == call_label)
        terminal_csv = bundle / terminal_call["bundle_csv"]
        terminal_state_file = bundle / terminal_call["bundle_initial_state"]
        if sha(terminal_csv) != terminal_call["output_sha256"]:
            raise ValueError("accepted terminal gradient CSV receipt mismatch")
        accepted_meta, accepted_arrays, accepted_scalars, accepted_text = inverse.read_payload(terminal_csv)
        initial = split.read_literal_vector(terminal_state_file)
        if inverse.digest(initial) != terminal_call["state_sha256"] or \
                not np.array_equal(initial, accepted_arrays["initial_state"]):
            raise ValueError("accepted terminal literal initial/checkpoint is malformed")
        _, descriptor, _, _ = inverse.read_payload(inputs / "descriptor_fine.csv")
        for key in split.CONTEXT_ARRAYS:
            if not np.array_equal(accepted_arrays[key], descriptor[key]):
                raise ValueError(f"accepted terminal context differs from fixed descriptor: {key}")
        observations = np.loadtxt(inputs / "observations.txt", skiprows=2)
        if observations.shape != (105, 5) or not np.isfinite(observations).all():
            raise ValueError("pinned observations must contain 105 finite points")
        report.update({"source_git_head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
            "accepted_zip_sha256": ACCEPTED_SHA, "accepted_report_sha256": report_sha,
            "accepted_report_schema": accepted["schema"], "accepted_terminal_call": call_label,
            "accepted_terminal_delta": accepted["terminal_gradient"]["delta"],
            "accepted_terminal_initial_sha256": inverse.digest(initial),
            "accepted_checkpoint_300_sha256": inverse.digest(accepted_arrays["checkpoint_300"]),
            "accepted_terminal_csv_sha256": sha(terminal_csv), "input_sha256": input_hashes,
            "source_sha256": current, "changed_source_paths": changed,
            "production_source_hashes_unchanged": True, "executable_sha256": sha(exe),
            "grid": list(GRID), "steps": STEPS, "dt": DT, "observation_count": 105,
            "restart_scale_diagnostic_original_forecast_rms_m_s": FORECAST_RMS,
            "native_calls": []})
        save(out / "report.json", report)
        initial_file = out / "accepted_terminal.initial_state.txt"
        driver.write_vector(initial_file, initial)
        if not np.array_equal(split.read_literal_vector(initial_file), initial):
            raise ValueError("literal accepted state file failed exact roundtrip")
        common = ["--newton-tol", "1e-14", "--krylov-tol", "1e-12"]
        meta, arrays, scalars, text = invoke("replay_gate_same_exe", exe,
            ["--physical-wave-inverse", str(initial_file), str(inputs / "observations.txt"),
             "16", str(DT), *common, "--replay-smoke"], out, report)
        gate = out / "replay_gate_same_exe.csv"
        gate_result = driver.validate_replay_gate(gate, sha(exe), exe, initial, descriptor)
        report["same_executable_replay_gate"] = gate_result
        save(out / "report.json", report)
        meta, arrays, scalars, text = invoke("analysis_restart_smoke", exe,
            ["--physical-wave-inverse", str(initial_file), str(inputs / "observations.txt"),
             str(STEPS), str(DT), *common, "--bounded-tape-forward-only", "--restart-smoke"], out, report)
        driver.validate_trial(meta, arrays, scalars, text, expected_initial=initial,
                               center_arrays=accepted_arrays, gradient=False)
        if not np.array_equal(arrays["checkpoint_300"], accepted_arrays["checkpoint_300"]):
            raise ValueError("reproduced 0->300 FP64 endpoint differs from accepted terminal checkpoint")
        if arrays["restart_warm_live_endpoint_305"].size != initial.size:
            raise ValueError("restart branch endpoint has unexpected state size")
        if meta.get("physical_wave_restart_smoke") != 1 or text.get("restart_snapshot_semantics") != \
                "listed_carried_state_only_not_preconditioner_cache":
            raise ValueError("restart-smoke metadata/coverage declaration mismatch")
        if meta.get("restart_initial_state_shared_bitwise") != 1 or \
                meta.get("restart_fresh_constructor_captured_before_context_initializer") != 1:
            raise ValueError("restart-smoke shared-state or cold-snapshot guard failed")
        if "restart_warm_live_endpoint_305" not in arrays:
            raise ValueError("missing warm-live endpoint array")
        branch_metrics = {}
        for branch in BRANCHES:
            endpoint = arrays[f"restart_{branch}_endpoint_305"]
            prediction = arrays[f"restart_{branch}_predicted_w_305"]
            if endpoint.shape != initial.shape or prediction.shape != (105,) or \
                    not np.isfinite(endpoint).all() or not np.isfinite(prediction).all():
                raise ValueError(f"invalid restart branch arrays: {branch}")
            if meta.get(f"restart_{branch}_restore_match") != 1 or \
                    meta.get(f"restart_{branch}_admissibility_checks") != 17:
                raise ValueError(f"restart branch restore/admissibility receipt failed: {branch}")
            expected_predictors = 0 if branch == "cold_restart" else 1
            if any(meta.get(f"restart_{branch}_{stage}_predictor_defined") != expected_predictors
                   for stage in ("stage2", "stage3")):
                raise ValueError(f"restart branch predictor profile is incorrect: {branch}")
            if meta.get(f"restart_{branch}_host_step_first") != 961 or \
                    meta.get(f"restart_{branch}_host_step_last") != 976 or \
                    meta.get(f"restart_{branch}_steps") != 16 or \
                    meta.get(f"restart_{branch}_dt_fp32") != DT:
                raise ValueError(f"restart branch host-step/dt profile is incorrect: {branch}")
            branch_metrics[branch] = {"endpoint_sha256": inverse.digest(endpoint),
                "predicted_w_105_sha256": inverse.digest(prediction),
                "stage2_predictor_defined": meta.get(f"restart_{branch}_stage2_predictor_defined"),
                "stage3_predictor_defined": meta.get(f"restart_{branch}_stage3_predictor_defined")}
        diff = {}
        warm_endpoint = arrays["restart_warm_live_endpoint_305"]
        warm_w = arrays["restart_warm_live_predicted_w_305"]
        # State block sizes are the native fixture layout: U,V,W,PH,theta,MU.
        ny, nz, nx = int(meta["ny"]), int(meta["nz"]), int(meta["nx"])
        block_sizes = (ny*nz*(nx+1), (ny+1)*nz*nx, ny*(nz+1)*nx,
                       ny*(nz+1)*nx, ny*nz*nx, ny*nx)
        begin = 0
        for name, count in zip(COMPONENTS, block_sizes):
            end = begin + count
            for branch in BRANCHES[1:]:
                delta = arrays[f"restart_{branch}_endpoint_305"][begin:end] - warm_endpoint[begin:end]
                maximum, rms = float(np.max(np.abs(delta))), float(np.sqrt(np.mean(delta*delta)))
                diff[f"{branch}_vs_warm_live_{name}"] = {"max_abs": maximum, "rms": rms}
                for suffix, value in (("max_abs", maximum), ("rms", rms)):
                    key = f"restart_{branch}_vs_warm_live_{suffix}_{name}"
                    if key not in scalars or not np.isfinite(scalars[key]) or \
                            abs(scalars[key] - value) > 1e-12 * max(1.0, abs(value)):
                        raise ValueError(f"native component difference receipt disagrees with arrays: {key}")
            begin = end
        if begin != warm_endpoint.size: raise ValueError("native endpoint component sizes do not sum to state size")
        for branch in BRANCHES[1:]:
            delta = arrays[f"restart_{branch}_predicted_w_305"] - warm_w
            rms = float(np.sqrt(np.mean(delta*delta)))
            diff[f"{branch}_vs_warm_live_W105"] = {"max_abs_m_s": float(np.max(np.abs(delta)),),
                "rms_m_s": rms, "ratio_to_original_600s_forecast_rms": rms / FORECAST_RMS[600],
                "ratio_to_original_900s_forecast_rms": rms / FORECAST_RMS[900]}
            for suffix, value in (("max_abs_m_s", diff[f"{branch}_vs_warm_live_W105"]["max_abs_m_s"]),
                                  ("rms_m_s", rms)):
                key = f"restart_{branch}_vs_warm_live_W105_{suffix}"
                if key not in scalars or not np.isfinite(scalars[key]) or \
                        abs(scalars[key] - value) > 1e-12 * max(1.0, abs(value)):
                    raise ValueError(f"native W105 difference receipt disagrees with arrays: {key}")
        call = report["native_calls"][-1]
        call["validated_forward_metadata"] = True
        call["native_checkpoint_300_sha256"] = inverse.digest(arrays["checkpoint_300"])
        call["checkpoint_300_bitwise_matches_accepted_terminal"] = True
        call["branch_metrics"] = branch_metrics
        call["branch_differences_vs_warm_live"] = diff
        report.update(status="completed", active_call=None, restart_comparison="diagnostic_only",
            state_coverage="listed Newton CarriedState including stage predictors; preconditioner caches and whole solver state are not serialized",
            restart_protocol="live warm path versus restored snapshot and clean-constructor cold state, all from identical terminal checkpoint at t=300 through t=305",
            branch_metrics=branch_metrics, branch_differences_vs_warm_live=diff)
        restored_endpoint_equal = np.array_equal(
            arrays["restart_snapshot_restored_endpoint_305"],
            arrays["restart_warm_live_endpoint_305"])
        restored_w_equal = np.array_equal(
            arrays["restart_snapshot_restored_predicted_w_305"],
            arrays["restart_warm_live_predicted_w_305"])
        if (meta.get("restart_snapshot_restored_endpoint_bitwise_equal_warm_live") != 1 or
                meta.get("restart_snapshot_restored_W105_bitwise_equal_warm_live") != 1 or
                not restored_endpoint_equal or not restored_w_equal):
            report.update(status="open_snapshot_parity_finding",
                unresolved_finding="restored final CarriedState did not reproduce live warm endpoint and W105 arrays bitwise",
                restored_snapshot_endpoint_bitwise_equal=restored_endpoint_equal,
                restored_snapshot_W105_bitwise_equal=restored_w_equal)
            save(out / "report.json", report)
            return 2
        report["restored_snapshot_endpoint_bitwise_equal"] = True
        report["restored_snapshot_W105_bitwise_equal"] = True
        save(out / "report.json", report)
        return 0
    except Exception as error:
        report.update(status="failed", failure_type=type(error).__name__, failure=str(error))
        save(out / "report.json", report)
        raise

if __name__ == "__main__":
    raise SystemExit(main())
