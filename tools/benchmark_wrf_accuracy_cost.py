#!/usr/bin/env python3
"""Compare archived dry em_b_wave WRF outputs and run-cost receipts.

The JSON plan supplies run directories, timestep/method labels, optional wall
and RSS receipts, and optional input/executable/namelist SHA-256 values. This
is an analysis-only whole-path comparison; it does not isolate integrator
cost unless the plan proves the surrounding setup and receipts are matched.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timedelta
import hashlib
import json
import math
from pathlib import Path
import re

import numpy as np
from netCDF4 import Dataset


ENDPOINT_SECONDS = 240
THETA_BASE_K = 300.0
GRAVITY_M_S2 = 9.81
VARIABLE_UNITS = {
    "U": "m s-1", "V": "m s-1", "W": "m s-1",
    "PH": "m2 s-2", "T": "K", "MU": "Pa",
}
OWNED_COUNTS = {"U": 16 * 16 * 16, "V": 16 * 17 * 16,
                "W": 16 * 16 * 16, "PH": 16 * 16 * 16,
                "T": 16 * 16 * 16, "MU": 16 * 16}
EXPECTED_DIMS = {
    "U": ("Time", "bottom_top", "south_north", "west_east_stag"),
    "V": ("Time", "bottom_top", "south_north_stag", "west_east"),
    "W": ("Time", "bottom_top_stag", "south_north", "west_east"),
    "PH": ("Time", "bottom_top_stag", "south_north", "west_east"),
    "PHB": ("Time", "bottom_top_stag", "south_north", "west_east"),
    "T": ("Time", "bottom_top", "south_north", "west_east"),
    "MU": ("Time", "south_north", "west_east"),
    "MUB": ("Time", "south_north", "west_east"),
}


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _resolve(base: Path, value: str) -> Path:
    path = Path(value).expanduser()
    return path.resolve() if path.is_absolute() else (base / path).resolve()


def _time_text(row: np.ndarray) -> str:
    value = np.asarray(row)
    if value.dtype.kind == "S":
        return b"".join(value.tolist()).decode("ascii").rstrip("\x00 ")
    if value.dtype.kind == "U":
        return "".join(value.tolist()).rstrip("\x00 ")
    raise ValueError(f"unsupported WRF Times dtype: {value.dtype}")


def _float_array(variable, time_index: int) -> np.ndarray:
    value = np.ma.asarray(variable[time_index], dtype=np.float64)
    if np.ma.isMaskedArray(value):
        return np.asarray(value.filled(np.nan), dtype=np.float64)
    return np.asarray(value, dtype=np.float64)


def _owned(fields: dict[str, np.ndarray]) -> dict[str, np.ndarray]:
    """Apply the native WRF owned-point policy to (z,y,x)-ordered arrays."""
    result = {
        "U": fields["U"][:, :, :-1],       # omit periodic duplicate x face
        "V": fields["V"],                  # retain both physical y walls
        "W": fields["W"][1:, :, :],       # omit fixed lower z face
        "PH": fields["PH"][1:, :, :],     # omit fixed lower z face
        "T": fields["T"],
        "MU": fields["MU"],
    }
    for name, array in result.items():
        if array.size != OWNED_COUNTS[name]:
            raise ValueError(f"{name} owned crop has {array.size} cells, expected {OWNED_COUNTS[name]}")
        if not np.isfinite(array).all():
            raise ValueError(f"{name} owned crop contains NaN or Inf")
    return result


def _load_run(run: dict, plan_dir: Path) -> dict:
    label = run.get("label")
    path_value = run.get("path")
    if not isinstance(label, str) or not label or not isinstance(path_value, str):
        raise ValueError("each run needs non-empty label and path fields")
    run_dir = _resolve(plan_dir, path_value)
    if not run_dir.is_dir():
        raise ValueError(f"run directory does not exist: {run_dir}")
    h_seconds = float(run.get("h_seconds", 0.0))
    method = run.get("method")
    if not math.isfinite(h_seconds) or h_seconds <= 0 or not isinstance(method, str) or not method:
        raise ValueError(f"{label}: h_seconds must be positive and method must be supplied")
    for key in ("wall_seconds", "peak_rss_bytes"):
        value = run.get(key)
        if value is not None and (not math.isfinite(float(value)) or float(value) <= 0.0):
            raise ValueError(f"{label}: {key} must be a positive finite receipt")

    checked_hashes = {}
    for key, default_name in (("input_sha256", "wrfinput_d01"),
                              ("namelist_sha256", "namelist.input"),
                              ("executable_sha256", "wrf.exe")):
        expected = run.get(key)
        if expected is None:
            continue
        path_key = key.removesuffix("_sha256") + "_path"
        receipt_path = _resolve(plan_dir, run[path_key]) if run.get(path_key) else run_dir / default_name
        if not receipt_path.is_file():
            raise ValueError(f"{label}: {key} supplied but receipt file is missing: {receipt_path}")
        actual = _sha256(receipt_path)
        if actual != expected:
            raise ValueError(f"{label}: {key} mismatch for {receipt_path}: {actual} != {expected}")
        checked_hashes[key] = {"path": str(receipt_path), "sha256": actual}

    files = sorted(p for p in run_dir.glob("wrfout_d01*") if p.is_file())
    if not files:
        raise ValueError(f"{label}: no wrfout_d01 files in {run_dir}")
    frames: dict[str, dict[str, np.ndarray]] = {}
    dimensions = None
    grid_attrs = None
    output_files = []
    minimum_total_theta = math.inf
    minimum_total_mu = math.inf
    minimum_layer_depth = math.inf
    for file_path in files:
        with Dataset(file_path, "r") as dataset:
            for name, expected_dims in EXPECTED_DIMS.items():
                if name not in dataset.variables:
                    raise ValueError(f"{label}: {file_path.name} missing required {name}")
                actual_dims = tuple(dataset.variables[name].dimensions)
                if actual_dims != expected_dims:
                    raise ValueError(f"{label}: {name} dimensions {actual_dims} != {expected_dims}")
            if "Times" not in dataset.variables:
                raise ValueError(f"{label}: {file_path.name} missing Times")
            current_grid = (dataset.dimensions["west_east"].size,
                            dataset.dimensions["south_north"].size,
                            dataset.dimensions["bottom_top"].size)
            current_attrs = (float(dataset.getncattr("DX")), float(dataset.getncattr("DY")))
            if dimensions is None:
                dimensions, grid_attrs = current_grid, current_attrs
            elif current_grid != dimensions or current_attrs != grid_attrs:
                raise ValueError(f"{label}: grid/spacing changed in {file_path.name}")
            times = [_time_text(row) for row in dataset.variables["Times"][:]]
            output_files.append({"path": str(file_path), "sha256": _sha256(file_path),
                                 "timestamps": times})
            required = ("U", "V", "W", "PH", "PHB", "T", "MU", "MUB")
            for index, timestamp in enumerate(times):
                if timestamp in frames:
                    raise ValueError(f"{label}: duplicate exact timestamp {timestamp}")
                fields = {name: _float_array(dataset.variables[name], index) for name in required}
                if any(not np.isfinite(array).all() for array in fields.values()):
                    raise ValueError(f"{label}: raw field contains NaN or Inf at {timestamp}")
                total_theta = fields["T"] + THETA_BASE_K
                total_mu = fields["MU"] + fields["MUB"]
                full_ph = fields["PH"] + fields["PHB"]
                layer_depth = np.diff(full_ph, axis=0) / GRAVITY_M_S2
                if not np.isfinite(total_theta).all() or np.any(total_theta <= 0.0):
                    raise ValueError(f"{label}: non-positive/non-finite total theta at {timestamp}")
                if not np.isfinite(total_mu).all() or np.any(total_mu <= 0.0):
                    raise ValueError(f"{label}: non-positive/non-finite total mu at {timestamp}")
                if not np.isfinite(layer_depth).all() or np.any(layer_depth <= 0.0):
                    raise ValueError(f"{label}: non-positive/non-finite PH+PHB layer depth at {timestamp}")
                minimum_total_theta = min(minimum_total_theta, float(np.min(total_theta)))
                minimum_total_mu = min(minimum_total_mu, float(np.min(total_mu)))
                minimum_layer_depth = min(minimum_layer_depth, float(np.min(layer_depth)))
                frames[timestamp] = _owned(fields)

    ordered_times = sorted(frames, key=lambda value: datetime.strptime(value, "%Y-%m-%d_%H:%M:%S"))
    initial_time = datetime.strptime(ordered_times[0], "%Y-%m-%d_%H:%M:%S")
    endpoint = (initial_time + timedelta(seconds=ENDPOINT_SECONDS)).isoformat(sep="_", timespec="seconds")
    if ordered_times[0] not in frames or endpoint not in frames:
        raise ValueError(f"{label}: exact start and {ENDPOINT_SECONDS}s endpoint frames are required")
    if dimensions != (16, 16, 16):
        raise ValueError(f"{label}: expected the archived 16x16x16 em_b_wave case, got {dimensions}")

    return {
        "label": label, "path": str(run_dir), "h_seconds": h_seconds, "method": method,
        "wall_seconds": run.get("wall_seconds"),
        "wall_receipt_source": run.get("wall_receipt_source"),
        "peak_rss_bytes": run.get("peak_rss_bytes"),
        "source_revision": run.get("source_revision"),
        "source_sha256": run.get("source_sha256"),
        "build_receipt": run.get("build_receipt"),
        "input_sha256": checked_hashes.get("input_sha256"),
        "namelist_sha256": checked_hashes.get("namelist_sha256"),
        "executable_sha256": checked_hashes.get("executable_sha256"),
        "wrfout_files": output_files,
        "grid": {"nx": dimensions[0], "ny": dimensions[1], "nz": dimensions[2],
                 "dx_m": grid_attrs[0], "dy_m": grid_attrs[1]},
        "physical_minima_over_all_frames": {
            "total_theta_K": minimum_total_theta,
            "total_mu_Pa": minimum_total_mu,
            "layer_depth_m_from_PH_plus_PHB_over_g": minimum_layer_depth,
            "g_m_s2": GRAVITY_M_S2},
        "timestamps": ordered_times,
        "frames": frames,
        "step_logs": _read_step_logs(run, run_dir),
    }


def _read_step_logs(run: dict, run_dir: Path) -> dict:
    """Report RSL step timing and Newton counters without calling them wall time."""
    rsl_out_path = _resolve(run_dir, run["rsl_out"]) if run.get("rsl_out") else run_dir / "rsl.out.0000"
    rsl_error_path = _resolve(run_dir, run["rsl_error"]) if run.get("rsl_error") else run_dir / "rsl.error.0000"
    timings = []
    if rsl_out_path.is_file():
        text = rsl_out_path.read_text(errors="replace")
        timings = [float(value) for value in re.findall(
            r"Timing for main:.*?:\s*([0-9.]+) elapsed seconds", text)]
    converged = []
    if rsl_error_path.is_file():
        text = rsl_error_path.read_text(errors="replace")
        converged = [(int(stage), int(iterations)) for stage, iterations in re.findall(
            r"\[Newton\]\s+CONVERGED stage=(\d+) iters=(\d+)", text)]
    return {
        "rsl_main_step_count": len(timings),
        "rsl_main_step_sum_seconds": float(sum(timings)) if timings else None,
        "rsl_main_step_seconds": timings,
        "converged_newton_stage_count": len(converged),
        "newton_iterations_sum": sum(iterations for _, iterations in converged) if converged else None,
        "rsl_timing_files": {"rsl_out": str(rsl_out_path) if rsl_out_path.is_file() else None,
                             "rsl_error": str(rsl_error_path) if rsl_error_path.is_file() else None},
        "qualification": "RSL step/main-section and Newton counters; not end-to-end wall time",
    }


def _metrics(candidate: np.ndarray, reference: np.ndarray, signal: np.ndarray) -> dict:
    difference = np.asarray(candidate, dtype=np.float64) - np.asarray(reference, dtype=np.float64)
    ref_signal = np.asarray(signal, dtype=np.float64)
    rms = float(np.sqrt(np.mean(difference * difference)))
    signal_rms = float(np.sqrt(np.mean(ref_signal * ref_signal)))
    return {
        "rms": rms,
        "max_abs": float(np.max(np.abs(difference))),
        "reference_signal_rms_from_initial": signal_rms,
        "relative_rms_to_reference_signal": rms / signal_rms if signal_rms > 0.0 else None,
        "relative_rms_status": "defined" if signal_rms > 0.0 else "undefined_zero_reference_signal",
    }


def analyze(plan_path: Path, reference_label: str, out_path: Path,
            reference_check_label: str | None = None) -> dict:
    plan = json.loads(plan_path.read_text())
    rows = plan.get("runs")
    if not isinstance(rows, list) or not rows:
        raise ValueError("runs JSON needs a non-empty runs list")
    labels = [row.get("label") for row in rows]
    if len(labels) != len(set(labels)):
        raise ValueError("run labels must be unique")
    if reference_label not in labels:
        raise ValueError(f"unknown reference label: {reference_label}")
    if reference_check_label is not None and reference_check_label not in labels:
        raise ValueError(f"unknown reference-check label: {reference_check_label}")
    if reference_check_label == reference_label:
        raise ValueError("reference-check must be a separate run label")
    plan_dir = plan_path.parent
    runs = {run["label"]: _load_run(run, plan_dir) for run in rows}
    reference_run = runs[reference_label]
    reference_start = reference_run["timestamps"][0]
    expected_endpoint = (datetime.strptime(reference_start, "%Y-%m-%d_%H:%M:%S") +
                         timedelta(seconds=ENDPOINT_SECONDS)).isoformat(sep="_", timespec="seconds")
    initial = reference_run["frames"][reference_start]
    result = {
        "schema": "wrf-dry-accuracy-cost-analysis-v1",
        "comparison_scope": "whole-run WRF result; method and cost ratio are not integrator-only unless source/build receipts prove that scope",
        "model": plan.get("model", "em_b_wave"),
        "endpoint_seconds": ENDPOINT_SECONDS,
        "reference_label": reference_label,
        "reference_check_label": reference_check_label,
        "reference_resolutions": {
            "primary": {"label": reference_run["label"],
                        "h_seconds": reference_run["h_seconds"],
                        "method": reference_run["method"]},
            "second": ({"label": runs[reference_check_label]["label"],
                        "h_seconds": runs[reference_check_label]["h_seconds"],
                        "method": runs[reference_check_label]["method"]}
                       if reference_check_label is not None else None),
        },
        "reference_start_timestamp": reference_start,
        "reference_endpoint_timestamp": expected_endpoint,
        "owned_points": {
            "U": "exclude periodic duplicate x face",
            "V": "include both physical y walls",
            "W": "exclude fixed lower z face; include top",
            "PH": "exclude fixed lower z face; include top",
            "T": "all mass points; physical theta is T+300 K",
            "MU": "all mass points; physical dry column mass is MU+MUB",
            "PH+PHB": "positive layer depth from diff(PH+PHB)/9.81",
        },
        "owned_point_counts_for_16_cubed_case": dict(OWNED_COUNTS),
        "units": VARIABLE_UNITS,
        "runs": [],
        "comparisons": {},
        "reference_step_sensitivity": None,
        "cost": {},
    }
    for run in runs.values():
        result["runs"].append({key: value for key, value in run.items() if key not in ("frames", "timestamps")})
        result["cost"][run["label"]] = {
            "wall_seconds_receipt": run["wall_seconds"],
            "wall_receipt_source": run["wall_receipt_source"],
            "peak_rss_bytes_receipt": run["peak_rss_bytes"],
            "rsl_step_logs": run["step_logs"],
        }
    for label, run in runs.items():
        if label == reference_label:
            continue
        common = sorted(set(reference_run["frames"]) & set(run["frames"]))
        if reference_start not in common or expected_endpoint not in common:
            raise ValueError(f"{label}: no exact shared initial and endpoint timestamps with reference")
        # All cases are the same owned mesh; refuse silent interpolation/regridding.
        if run["grid"] != reference_run["grid"]:
            raise ValueError(f"{label}: candidate/reference grid or spacing differs")
        result["comparisons"][label] = {}
        for timestamp in common:
            current = run["frames"][timestamp]
            reference = reference_run["frames"][timestamp]
            signal = {name: reference[name] - initial[name] for name in VARIABLE_UNITS}
            result["comparisons"][label][timestamp] = {
                name: _metrics(current[name], reference[name], signal[name])
                for name in VARIABLE_UNITS
            }
    if reference_check_label is not None:
        fine = runs[reference_check_label]
        if fine["grid"] != reference_run["grid"]:
            raise ValueError("reference and reference-check grids differ")
        common = sorted(set(reference_run["frames"]) & set(fine["frames"]))
        if reference_start not in common or expected_endpoint not in common:
            raise ValueError("reference-check lacks exact shared initial/endpoint timestamps")
        result["reference_step_sensitivity"] = {
            "primary_label": reference_label,
            "second_label": reference_check_label,
            "interpretation": "difference between two archived reference resolutions; not exact truth or a temporal-order claim",
            "primary_h_seconds": reference_run["h_seconds"],
            "second_h_seconds": fine["h_seconds"],
            "times": {
                timestamp: {
                    name: _metrics(fine["frames"][timestamp][name],
                                   reference_run["frames"][timestamp][name],
                                   reference_run["frames"][timestamp][name] - initial[name])
                    for name in VARIABLE_UNITS
                }
                for timestamp in common
            },
        }
    ref_wall = reference_run["wall_seconds"]
    for label, run in runs.items():
        wall = run["wall_seconds"]
        result["cost"][label]["whole_path_wall_ratio_to_reference"] = (
            float(wall) / float(ref_wall)
            if label != reference_label and wall is not None and ref_wall is not None and ref_wall > 0
            else None)
        result["cost"][label]["wall_ratio_status"] = (
            "whole_path_receipts_available_not_integrator_isolated"
            if result["cost"][label]["whole_path_wall_ratio_to_reference"] is not None
            else "unavailable_without_both_wall_receipts")
        result["cost"][label]["wall_ratio_scope"] = (
            "whole-path run wall from the supplied plan receipts; not an integrator-only ratio")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(result, indent=2) + "\n")
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runs-json", type=Path, required=True,
                        help="structured run plan with labels, directories, h_seconds, methods, and optional receipts")
    parser.add_argument("--reference", required=True,
                        help="label of the archived dry RK3 reference")
    parser.add_argument("--reference-check", help="optional second RK3 resolution for reference-step sensitivity (not truth/order)")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    result = analyze(args.runs_json.resolve(), args.reference, args.out.resolve(), args.reference_check)
    print(json.dumps({"status": "written", "out": str(args.out.resolve()),
                      "reference": result["reference_label"],
                      "candidate_count": len(result["comparisons"]),
                      "times_compared": sorted(next(iter(result["comparisons"].values())))
                      if result["comparisons"] else []}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
