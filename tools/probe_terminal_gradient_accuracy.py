#!/usr/bin/env python3
"""Check the accepted terminal four-control gradient using fixed-residual FDs."""
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

ACCEPTED_SHA = "8dbe3f9d20ac2d5591cab455c67086978bd51676ed7b2acb8c167d370372bcbd"
GRID, STEPS, DT, SIGMA = (16, 12, 8), 960, 0.3125, 3.0e-4
TIMES, EPSILONS, NCONTROL = (150, 300), (0.005, 0.0025), 4
GRADIENT_THRESHOLD = 1.0e-5
ELAPSED_LABEL = "Elapsed (wall clock) time (h:mm:ss or m:ss)"


def formula_self_check() -> dict:
    # Asymmetric endpoints catch accidental substitution of the average
    # perturbed residual, which computes a central cost difference.
    center, observed, plus, minus, eps = 1.0, 0.5, 1.5, 0.6, 0.25
    fixed = (center - observed) * (plus - minus) / (2.0 * eps)
    average = ((plus - observed + minus - observed) / 2.0) * (plus - minus) / (2 * eps)
    if not np.isclose(fixed, .9, atol=1e-15) or not np.isclose(average, .99, atol=1e-15) or fixed == average:
        raise AssertionError("fixed-center-residual formula self-check failed")
    return {"fixed_center_residual": fixed, "average_perturbed_residual": average,
            "distinct": True}


def load_accepted(archive: Path, digest: str, outdir: Path):
    parent, bundle, report_sha = driver.copy_resume_bundle(
        archive, digest, outdir / "accepted")
    if (parent.get("schema") != "pr284-small-step-local-chart-continuation-v1" or
            parent.get("status") != "completed_bounded_continuation"):
        raise ValueError("accepted ZIP is not the pinned PR284 continuation")
    terminal = parent.get("terminal_gradient") or {}
    label = "resume_gradient_u10_terminal"
    call = next((c for c in parent.get("calls", []) if c.get("label") == label and
                 c.get("kind") == "gradient"), None)
    if call is None or terminal.get("call_label") != label:
        raise ValueError("accepted continuation lacks the expected terminal gradient")
    delta = np.asarray(terminal["delta"], dtype=np.float64)
    if (not np.array_equal(delta, np.asarray(call["delta"], dtype=np.float64)) or
            not np.array_equal(delta, np.asarray(parent["final_delta"], dtype=np.float64))):
        raise ValueError("accepted terminal delta differs from final accepted chart point")

    inputs = bundle / "inputs"
    state_path, obs_path = inputs / "initial_state.txt", inputs / "observations.txt"
    basis_path = inputs / "canonical_basis.npy"
    if (driver.sha(state_path) != parent["input_sha256"]["initial_state"] or
            driver.sha(obs_path) != parent["input_sha256"]["observations"]):
        raise ValueError("accepted initial state or observations differ from report pins")
    state = driver.split.read_literal_vector(state_path)
    basis = np.load(basis_path, allow_pickle=False)
    if (state.size != 8480 or not np.isfinite(state).all() or
            inverse.digest(state) != parent["literal_state_sha256"] or
            basis.shape != (8480, NCONTROL) or basis.dtype != np.float64 or
            not np.isfinite(basis).all() or
            inverse.digest(basis) != parent["canonical_basis_sha256"]):
        raise ValueError("accepted literal state or canonical Bcan failed its pin")

    state_file = bundle / call["bundle_initial_state"]
    csv_file = bundle / call["bundle_csv"]
    if driver.sha(csv_file) != call["output_sha256"]:
        raise ValueError("accepted terminal CSV differs from call receipt")
    center_state = state + basis @ delta
    if not np.array_equal(driver.split.read_literal_vector(state_file), center_state):
        raise ValueError("accepted state is not literal_state + Bcan @ terminal_delta")
    archived = inverse.read_payload(csv_file)
    if not np.array_equal(archived[1].get("initial_state"), center_state):
        raise ValueError("accepted terminal CSV has a different initial state")
    if not np.isclose(archived[2]["objective_physical_w"], terminal["objective"],
                      rtol=1e-12, atol=1e-10):
        raise ValueError("accepted terminal objective differs from its CSV")
    projected = basis.T @ archived[1]["initial_pullback"]
    if not np.allclose(projected, terminal["gradient"], rtol=2e-13, atol=1e-10):
        raise ValueError("accepted terminal pullback differs from projected-gradient receipt")
    return parent, bundle, report_sha, terminal, call, state, basis, delta, archived


def source_receipt(parent: dict) -> dict:
    hashes = driver.source_fingerprints()
    old = parent.get("source_sha256", {})
    if set(hashes) != set(old):
        raise ValueError("accepted source fingerprint inventory differs from current driver inventory")
    cpp_test = "external/libtorch_wrf/sdirk3/tests/test_native_wave_refinement.cpp"
    changed = {name: (old[name], hashes[name]) for name in hashes
               if name != cpp_test and old[name] != hashes[name]}
    if changed:
        raise ValueError(f"accepted non-test source fingerprints differ: {changed}")
    hashes["tools/probe_terminal_gradient_accuracy.py"] = driver.sha(Path(__file__))
    return hashes


def call_plan(exe: Path, outdir: Path, inputs: Path, delta: np.ndarray) -> list[dict]:
    calls = [{"label": "terminal_short_replay_gate", "kind": "gate", "steps": 16,
              "delta": delta.tolist(), "flag": "--replay-smoke"},
             {"label": "terminal_center_gradient", "kind": "gradient", "steps": STEPS,
              "delta": delta.tolist(), "flag": "--bounded-replay-pullback"}]
    for eps in EPSILONS:
        for j in range(NCONTROL):
            for side, sign in (("plus", 1.0), ("minus", -1.0)):
                point = delta.copy()
                point[j] += sign * eps
                calls.append({"label": f"eps{eps:g}_j{j}_{side}", "kind": "forward_trial",
                              "steps": STEPS, "delta": point.tolist(), "epsilon": eps,
                              "coordinate": j, "side": side,
                              "flag": "--bounded-tape-forward-only"})
    for call in calls:
        call["state_path"] = outdir / f"{call['label']}.initial_state.txt"
        call["csv_path"] = outdir / f"{call['label']}.csv"
        call["command"] = [str(exe), *map(str, GRID), str(call["csv_path"]),
                            "--physical-wave-inverse", str(call["state_path"]),
                            str(inputs / "observations.txt"), str(call["steps"]), str(DT),
                            "--newton-tol", "1e-14", "--krylov-tol", "1e-12", call["flag"]]
    return calls


def run_gate(call: dict, *, exe: Path, state: np.ndarray, basis: np.ndarray,
             saved_context: dict, report: dict, report_path: Path) -> dict:
    point = np.asarray(call["delta"], dtype=np.float64)
    initial = state + basis @ point
    driver.write_vector(call["state_path"], initial)
    command = call["command"]
    usage = report_path.parent / f"{call['label']}.time.txt"
    timed = ["/usr/bin/time", "-v", "-o", str(usage), *command]
    report["active_call"] = {"label": call["label"], "kind": "replay_gate",
                             "delta": point.tolist(), "command": command,
                             "timed_command": timed, "state_sha256": driver.sha(call["state_path"])}
    driver.write_json(report_path, report)
    started = time.monotonic()
    result = subprocess.run(timed, capture_output=True, text=True)
    elapsed = time.monotonic() - started
    label = call["label"]
    (report_path.parent / f"{label}.stdout.log").write_text(result.stdout)
    (report_path.parent / f"{label}.stderr.log").write_text(result.stderr)
    time_text = usage.read_text() if usage.is_file() else ""
    row = {"label": label, "kind": "replay_gate", "delta": point.tolist(),
           "command": command, "timed_command": timed, "return_code": result.returncode,
           "elapsed_seconds_monotonic": elapsed,
           "process_elapsed_wall": driver.parse_time_value(time_text, ELAPSED_LABEL),
           "peak_rss_kib": driver.parse_time_value(time_text, "Maximum resident set size (kbytes)"),
           "state_file": call["state_path"].name, "output_file": call["csv_path"].name,
           "state_sha256": driver.sha(call["state_path"]),
           "output_sha256": driver.sha(call["csv_path"]) if call["csv_path"].is_file() else None,
           "stdout_log": f"{label}.stdout.log", "stderr_log": f"{label}.stderr.log",
           "time_report": usage.name}
    report["calls"].append(row)
    report["active_call"] = None
    driver.write_json(report_path, report)
    if result.returncode:
        raise RuntimeError(f"same-executable replay gate failed: {result.returncode}")
    if not isinstance(row["process_elapsed_wall"], str) or not isinstance(row["peak_rss_kib"], int):
        raise ValueError("replay gate lacks wall-time or RSS telemetry")
    return driver.validate_replay_gate(call["csv_path"], driver.sha(exe), exe, initial, saved_context)


def check_center(current: tuple, archived: tuple, terminal: dict) -> dict:
    arrays, old = current[1], archived[1]
    current_pullback = arrays["initial_pullback"]
    accepted_pullback = old["initial_pullback"]
    if (current_pullback.shape != (8480,) or accepted_pullback.shape != (8480,) or
            not np.isfinite(current_pullback).all() or not np.isfinite(accepted_pullback).all()):
        raise ValueError("full-state initial pullback is missing, malformed, or nonfinite")
    pullback_difference = current_pullback - accepted_pullback
    equal = {}
    for t in TIMES:
        for name in (f"predicted_{t}", f"checkpoint_{t}",
                     f"bracket_index_{t}", f"observed_{t}"):
            equal[name] = bool(np.array_equal(arrays[name], old[name]))
            if not equal[name]:
                raise ValueError(f"fresh center differs from accepted endpoint: {name}")
    for key in driver.CONTEXT:
        if not np.array_equal(arrays[key], old[key]):
            raise ValueError(f"fresh center context differs from accepted endpoint: {key}")
    cost = float(current[2]["objective_physical_w"])
    if not np.isclose(cost, terminal["objective"], rtol=0.0, atol=1e-12):
        raise ValueError("fresh center objective differs from accepted endpoint")
    return {"bitwise_equal_arrays": equal, "objective_equal_atol": 1e-12,
            "full_initial_pullback_implementation_parity": {
                "bitwise_equal": bool(np.array_equal(current_pullback, accepted_pullback)),
                "difference_l2": float(np.linalg.norm(pullback_difference)),
                "difference_max_abs": float(np.max(np.abs(pullback_difference))),
                "current_raw_l2": float(np.linalg.norm(current_pullback)),
                "accepted_raw_l2": float(np.linalg.norm(accepted_pullback)),
                "claim_scope": "implementation parity diagnostic; not stationarity"}}


def analyze(center: dict, endpoints: dict, gradient: np.ndarray, coordinates: int) -> dict:
    """Analyze the four local-chart controls; per-time AD is not emitted at h=.3125."""
    predictions = {t: center[f"predicted_{t}"] for t in TIMES}
    observations = {t: center[f"observed_{t}"] for t in TIMES}
    unit = np.finfo(np.float64).eps / 2
    gamma = 105 * unit / (1 - 105 * unit)
    fd, floors, average_fd, cost_fd = {}, {}, {}, {}
    for eps in EPSILONS:
        per_time = {str(t): [] for t in TIMES}
        floor_time = {str(t): [] for t in TIMES}
        average, cost = [], []
        for j in range(coordinates):
            plus = endpoints[(eps, j, "plus")]
            minus = endpoints[(eps, j, "minus")]
            avg_total, cost_total = 0.0, 0.0
            for t in TIMES:
                p0, y = predictions[t], observations[t]
                pplus, pminus = plus[f"predicted_{t}"], minus[f"predicted_{t}"]
                r0, rp, rm = p0 - y, pplus - y, pminus - y
                dp = pplus - pminus
                response = dp / (2 * eps)
                per_time[str(t)].append(float(np.dot(r0, response) / SIGMA**2))
                # Endpoint/center operand-rounding proxy only; it excludes
                # solver, replay, and discretization errors, so it is not a bound.
                numerator_floor = unit * (
                    np.dot(np.abs(r0), np.abs(pplus) + np.abs(pminus)) +
                    np.dot(np.abs(p0) + np.abs(y), np.abs(dp))) / (2 * eps)
                numerator_floor += gamma * np.dot(np.abs(r0), np.abs(response))
                floor_time[str(t)].append(float(numerator_floor / SIGMA**2))
                avg_total += float(np.dot((rp + rm) / 2, dp) / (2 * eps * SIGMA**2))
                cost_total += float((np.dot(rp, rp) - np.dot(rm, rm)) /
                                    (4 * eps * SIGMA**2))
            average.append(avg_total)
            cost.append(cost_total)
        combined = np.asarray(per_time[str(TIMES[0])]) + np.asarray(per_time[str(TIMES[1])])
        floor_combined = np.asarray(floor_time[str(TIMES[0])]) + np.asarray(floor_time[str(TIMES[1])])
        fd[str(eps)] = {"per_time": per_time, "combined": combined.tolist()}
        floors[str(eps)] = {"per_time": floor_time, "combined": floor_combined.tolist(),
                            "scope": "float64 endpoint/center operand proxy only; not a full error bound"}
        average_fd[str(eps)], cost_fd[str(eps)] = average, cost
    coarse, fine = (np.asarray(fd[str(eps)]["combined"]) for eps in EPSILONS)
    richardson = (4 * fine - coarse) / 3
    per_time_richardson = {}
    for t in TIMES:
        coarse_t = np.asarray(fd[str(EPSILONS[0])]["per_time"][str(t)])
        fine_t = np.asarray(fd[str(EPSILONS[1])]["per_time"][str(t)])
        per_time_richardson[str(t)] = ((4 * fine_t - coarse_t) / 3).tolist()
    return {"formula": "sum_t dot(p_center-y, (p_plus-p_minus)/(2eps)) / sigma^2",
            "per_time_ad": "unavailable from 960-step bounded-replay mode",
            "combined_ad": gradient.tolist(), "fixed_center_residual_fd": fd,
            "per_time_richardson": per_time_richardson,
            "combined_richardson": richardson.tolist(),
            "combined_gradient_difference": (richardson - gradient).tolist(),
            "combined_gradient_difference_l2": float(np.linalg.norm(richardson - gradient)),
            "epsilon_scatter": {"fine_minus_coarse": (fine - coarse).tolist(),
                                "absolute": np.abs(fine - coarse).tolist()},
            "arithmetic_floor_proxy": floors,
            "average_residual_cost_fd_diagnostics": average_fd,
            "central_objective_cost_difference_diagnostics": cost_fd,
            "formal_Eg_bound": False, "full_state_stationarity_checked": False}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--accepted-zip", type=Path, required=True)
    parser.add_argument("--accepted-sha256", required=True)
    parser.add_argument("--outdir", type=Path, required=True)
    parser.add_argument("--plan-only", action="store_true",
                        help="validate pins and show the call plan without running the solver")
    args = parser.parse_args()
    exe, outdir = args.exe.resolve(), args.outdir.resolve()
    if not exe.is_file() or outdir.exists() and any(outdir.iterdir()):
        parser.error("--exe must exist and --outdir must be new or empty")
    outdir.mkdir(parents=True, exist_ok=True)
    report_path = outdir / "report.json"
    report = {"schema": "terminal-four-control-gradient-accuracy-v1", "status": "preflight",
              "epsilons": list(EPSILONS), "grid": list(GRID), "steps": STEPS,
              "dt": DT, "sigma": SIGMA, "calls": [], "optimizer_run": False,
              "observations_changed": False, "regularization_changed": False,
              "gradient_scope": "four-control projection of the full 960-step trajectory gradient",
              "full_state_stationarity": "not assessed", "formula_self_check": formula_self_check(),
              "source_revision": subprocess.check_output(
                  ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
              "executable_sha256": driver.sha(exe), "platform": platform.platform(),
              "python": platform.python_version(), "numpy": np.__version__}
    driver.write_json(report_path, report)
    try:
        accepted_zip = args.accepted_zip.resolve()
        if args.accepted_sha256 != ACCEPTED_SHA:
            raise ValueError("accepted ZIP SHA argument differs from the pinned artifact")
        (parent, bundle, parent_report_sha, terminal, terminal_call, state, basis,
         delta, archived) = load_accepted(accepted_zip, args.accepted_sha256, outdir)
        hashes = source_receipt(parent)
        inputs = bundle / "inputs"
        report.update({"status": "preflight_ready", "accepted_zip_sha256": ACCEPTED_SHA,
                       "accepted_report_sha256": parent_report_sha,
                       "accepted_terminal_call": terminal_call["label"],
                       "accepted_terminal_csv_sha256": terminal_call["output_sha256"],
                       "accepted_executable_sha256": parent.get("executable_sha256"),
                       "accepted_delta": delta.tolist(), "state_sha256": inverse.digest(state),
                       "basis_sha256": inverse.digest(basis),
                       "input_sha256": {n: driver.sha(inputs / n) for n in
                                        ("initial_state.txt", "observations.txt", "canonical_basis.npy")},
                       "accepted_source_sha256": {n: hashes[n] for n in hashes
                                                   if n != "tools/probe_terminal_gradient_accuracy.py"},
                       "current_source_sha256": hashes,
                       "accepted_ad_gradient": terminal["gradient"],
                       "accepted_ad_gradient_l2": terminal["gradient_l2"],
                       "accepted_objective": terminal["objective"]})
        context = {k: archived[1][k] for k in driver.CONTEXT}
        calls = call_plan(exe, outdir, inputs, delta)
        report["call_plan"] = [{k: v for k, v in c.items()
                                 if k not in ("state_path", "csv_path")}
                                for c in calls]
        if args.plan_only:
            report.update(status="plan_only_no_native_calls", call_count=len(calls),
                          mock_receipt={"formula_self_check": "passed", "native_calls": 0})
            driver.write_json(report_path, report)
            return 0

        gate = run_gate(calls[0], exe=exe, state=state, basis=basis,
                        saved_context=context, report=report, report_path=report_path)
        report["same_executable_replay_gate"] = gate
        driver.write_json(report_path, report)
        _, center, center_scalars, _ = driver.invoke_native(
            exe=exe, outdir=outdir, inputs=inputs, state=state, basis=basis,
            delta=delta, label=calls[1]["label"], gradient=True,
            center_arrays=None, report=report)
        current_center = (None, center, center_scalars, None)
        report["fresh_center_vs_accepted"] = check_center(current_center, archived, terminal)
        gradient = basis.T @ center["initial_pullback"]
        if not np.allclose(gradient, terminal["gradient"], rtol=2e-13, atol=1e-10):
            raise ValueError("fresh center AD gradient differs from accepted terminal receipt")
        report["fresh_center_gradient"] = gradient.tolist()
        driver.write_json(report_path, report)

        endpoints = {}
        for call in calls[2:]:
            _, arrays, _, _ = driver.invoke_native(
                exe=exe, outdir=outdir, inputs=inputs, state=state, basis=basis,
                delta=np.asarray(call["delta"]), label=call["label"], gradient=False,
                center_arrays=center, report=report)
            endpoints[(call["epsilon"], call["coordinate"], call["side"])] = arrays
        report["analysis"] = analyze(center, endpoints, gradient, NCONTROL)
        discrepancy = report["analysis"]["combined_gradient_difference_l2"]
        budget = GRADIENT_THRESHOLD - float(np.linalg.norm(gradient))
        report["gradient_budget_diagnostic"] = {
            "threshold": GRADIENT_THRESHOLD,
            "fresh_projected_gradient_l2": float(np.linalg.norm(gradient)),
            "remaining_budget": budget,
            "fd_ad_discrepancy_l2": discrepancy,
            "empirical_discrepancy_to_remaining_budget_ratio":
                discrepancy / budget if budget > 0.0 else None,
            "scope": "empirical four-control diagnostic; not an Eg bound"}
        report.update(status="completed_terminal_four_control_fd_diagnostic",
                      call_count=len(report["calls"]),
                      convergence_claim="four-control diagnostic only; no formal Eg bound")
        driver.write_json(report_path, report)
        return 0
    except Exception as error:
        report.update(status="preflight_or_call_failed", failure_type=type(error).__name__,
                      failure=str(error))
        driver.write_json(report_path, report)
        raise


if __name__ == "__main__":
    raise SystemExit(main())
