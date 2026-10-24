#!/usr/bin/env python3
"""Compare archived dry em_b_wave WRF outputs and run-cost receipts.

The JSON plan supplies run directories, timestep/method labels, optional wall
and RSS receipts, and optional input/executable/namelist SHA-256 values. This
is an analysis-only whole-path comparison; it does not isolate integrator
cost unless the plan proves the surrounding setup and receipts are matched.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
from datetime import datetime, timedelta
from pathlib import Path

import numpy as np
from netCDF4 import Dataset

ENDPOINT_SECONDS = 240
THETA_BASE_K = 300.0
GRAVITY_M_S2 = 9.81
DEFAULT_GRID = {"nx": 16, "ny": 16, "nz": 16}
VARIABLE_UNITS = {
    "U": "m s-1", "V": "m s-1", "W": "m s-1",
    "PH": "m2 s-2", "T": "K", "MU": "Pa",
}
PRESSURE_UNITS = {"P_total": "Pa"}
OWNED_COUNTS = {"U": 16 * 16 * 16, "V": 16 * 17 * 16,
                "W": 16 * 16 * 16, "PH": 16 * 16 * 16,
                "T": 16 * 16 * 16, "MU": 16 * 16}
BASE_STATE_FIELDS = ("U", "V", "W", "PH", "PHB", "T", "MU", "MUB", "P", "PB")
AVAILABLE_BASE_FIELDS = (
    "QVAPOR", "DNW", "DN", "ZNW", "ZNU", "MAPFAC_M", "MAPFAC_U", "MAPFAC_V",
    "MAPFAC_MX", "MAPFAC_MY", "F",
)
AVAILABLE_BASE_ATTRIBUTES = ("MAP_PROJ", "CEN_LAT", "CEN_LON", "TRUELAT1", "TRUELAT2", "STAND_LON")
EXPECTED_DIMS = {
    "U": ("Time", "bottom_top", "south_north", "west_east_stag"),
    "V": ("Time", "bottom_top", "south_north_stag", "west_east"),
    "W": ("Time", "bottom_top_stag", "south_north", "west_east"),
    "PH": ("Time", "bottom_top_stag", "south_north", "west_east"),
    "PHB": ("Time", "bottom_top_stag", "south_north", "west_east"),
    "T": ("Time", "bottom_top", "south_north", "west_east"),
    "MU": ("Time", "south_north", "west_east"),
    "MUB": ("Time", "south_north", "west_east"),
    "P": ("Time", "bottom_top", "south_north", "west_east"),
    "PB": ("Time", "bottom_top", "south_north", "west_east"),
    "QVAPOR": ("Time", "bottom_top", "south_north", "west_east"),
    "DNW": ("Time", "bottom_top"),
    "DN": ("Time", "bottom_top"),
    "ZNW": ("Time", "bottom_top_stag"),
    "ZNU": ("Time", "bottom_top"),
    "MAPFAC_M": ("Time", "south_north", "west_east"),
    "MAPFAC_U": ("Time", "south_north", "west_east_stag"),
    "MAPFAC_V": ("Time", "south_north_stag", "west_east"),
    "MAPFAC_MX": ("Time", "south_north", "west_east"),
    "MAPFAC_MY": ("Time", "south_north", "west_east"),
    "F": ("Time", "south_north", "west_east"),
}


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


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


def _owned(fields: dict[str, np.ndarray], grid: dict[str, int] | None = None) -> dict[str, np.ndarray]:
    """Apply the native WRF owned-point policy to (z,y,x)-ordered arrays."""
    grid = DEFAULT_GRID if grid is None else grid
    nx, ny, nz = grid["nx"], grid["ny"], grid["nz"]
    expected = {
        "U": nz * ny * nx, "V": nz * (ny + 1) * nx,
        "W": nz * ny * nx, "PH": nz * ny * nx,
        "T": nz * ny * nx, "MU": ny * nx,
    }
    result = {
        "U": fields["U"][:, :, :-1],       # omit periodic duplicate x face
        "V": fields["V"],                  # retain both physical y walls
        "W": fields["W"][1:, :, :],       # omit fixed lower z face
        "PH": fields["PH"][1:, :, :],     # omit fixed lower z face
        "T": fields["T"],
        "MU": fields["MU"],
    }
    for name, array in result.items():
        if array.size != expected[name]:
            raise ValueError(f"{name} owned crop has {array.size} cells, expected {expected[name]}")
        if not np.isfinite(array).all():
            raise ValueError(f"{name} owned crop contains NaN or Inf")
    return result


def _numeric_array(variable, time_index: int = 0) -> np.ndarray:
    value = np.ma.asarray(variable[time_index] if "Time" in variable.dimensions else variable[:], dtype=np.float64)
    if np.ma.isMaskedArray(value):
        value = value.filled(np.nan)
    return np.asarray(value, dtype=np.float64)


def _exact_numeric_equal(left: np.ndarray, right: np.ndarray) -> bool:
    """Numerical exact equality treats +0 and -0 as equal and rejects NaN/Inf."""
    a, b = np.asarray(left), np.asarray(right)
    return a.shape == b.shape and np.isfinite(a).all() and np.isfinite(b).all() and np.array_equal(a, b)


def _exact_value_equal(left, right) -> bool:
    a, b = np.asarray(left), np.asarray(right)
    if a.dtype.kind in "biufc" and b.dtype.kind in "biufc":
        return _exact_numeric_equal(a, b)
    return a.shape == b.shape and np.array_equal(a, b)


def _expected_grid(plan: dict) -> dict[str, int]:
    value = plan.get("expected_grid", DEFAULT_GRID)
    if not isinstance(value, dict) or not {"nx", "ny", "nz"} <= value.keys():
        raise ValueError("expected_grid must be an object with nx, ny, and nz")
    if any(type(value[name]) is not int or value[name] <= 0 for name in ("nx", "ny", "nz")):
        raise ValueError("expected_grid dimensions must be positive integers")
    grid = {name: value[name] for name in ("nx", "ny", "nz")}
    return grid


def _load_run(run: dict, plan_dir: Path, plan: dict | None = None) -> dict:
    plan = plan or {}
    expected_grid = _expected_grid(plan)
    duration_seconds = plan.get("duration_seconds", ENDPOINT_SECONDS)
    if type(duration_seconds) is not int or duration_seconds <= 0:
        raise ValueError("duration_seconds must be a positive integer")
    strict_initial = bool(plan.get("require_shared_initial_state", False))
    include_pressure = bool(plan.get("include_total_pressure", False))
    dry_no_qv = bool(plan.get("dry_case_no_qv", False))
    use_theta_m = plan.get("use_theta_m")
    if dry_no_qv and (type(use_theta_m) is not int or use_theta_m not in (0, 1)):
        raise ValueError("dry_case_no_qv requires explicit use_theta_m=0 or 1")
    include_budgets = bool(plan.get("include_mass_theta_budgets", False))
    wave_plan = plan.get("wave_mode1", False)
    wave_fields = tuple(wave_plan.get("fields", ("T", "W", "P"))) if wave_plan else ()
    if any(name not in ("T", "W", "P") for name in wave_fields):
        raise ValueError("wave_mode1 fields may contain only T, W, and P perturbation")
    required_base = (tuple(dict.fromkeys((*BASE_STATE_FIELDS,
                                          *plan.get("required_shared_state_fields", ()))))
                     if strict_initial else ())
    if strict_initial and dry_no_qv:
        required_base = tuple(dict.fromkeys((*required_base, "QVAPOR")))
    optional_base = tuple(plan.get("compare_available_base_state_fields", AVAILABLE_BASE_FIELDS)) if strict_initial else ()
    required_attrs = tuple(plan.get("required_shared_global_attributes", ("DX", "DY"))) if strict_initial else ()
    optional_attrs = tuple(plan.get("compare_available_global_attributes", AVAILABLE_BASE_ATTRIBUTES)) if strict_initial else ()
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

    # Read only Times metadata first so strict initial-state capture always uses t=0.
    time_inventory = []
    for file_path in files:
        with Dataset(file_path, "r") as dataset:
            if "Times" not in dataset.variables:
                raise ValueError(f"{label}: {file_path.name} missing Times")
            time_inventory.extend(_time_text(row) for row in dataset.variables["Times"][:])
    if len(time_inventory) != len(set(time_inventory)):
        raise ValueError(f"{label}: duplicate exact timestamp across output files")
    ordered_times = sorted(time_inventory, key=lambda value: datetime.strptime(value, "%Y-%m-%d_%H:%M:%S"))
    initial_time = datetime.strptime(ordered_times[0], "%Y-%m-%d_%H:%M:%S")
    endpoint = (initial_time + timedelta(seconds=duration_seconds)).isoformat(sep="_", timespec="seconds")
    if endpoint not in set(ordered_times):
        raise ValueError(f"{label}: exact start and {duration_seconds}s endpoint frames are required")
    interval = plan.get("output_interval_seconds")
    if interval is not None:
        if type(interval) is not int or interval <= 0 or duration_seconds % interval:
            raise ValueError("output_interval_seconds must divide duration_seconds")
        expected_times = [
            (initial_time + timedelta(seconds=t)).isoformat(sep="_", timespec="seconds")
            for t in range(0, duration_seconds + 1, interval)
        ]
        if ordered_times != expected_times:
            raise ValueError(f"{label}: output timestamps do not match the requested {interval}s cadence")
    expected_start = plan.get("start_timestamp")
    if expected_start is not None and ordered_times[0] != expected_start:
        raise ValueError(f"{label}: start timestamp {ordered_times[0]} != plan {expected_start}")

    frames: dict[str, dict[str, np.ndarray]] = {}
    dimensions = None
    grid_attrs = None
    output_files = []
    minimum_total_theta = math.inf
    minimum_total_mu = math.inf
    minimum_layer_depth = math.inf
    minimum_total_pressure = math.inf
    base_state = None
    base_state_fields = None
    base_attributes = None
    base_optional_available = None
    base_attribute_availability = None
    strict_attributes_in_files = None
    budgets = {}
    qvapor_max_abs = 0.0
    required = ["U", "V", "W", "PH", "PHB", "T", "MU", "MUB"]
    if include_pressure:
        required.extend(("P", "PB"))
    if "P" in wave_fields:
        required.append("P")
    if dry_no_qv:
        required.append("QVAPOR")
    for file_path in files:
        with Dataset(file_path, "r") as dataset:
            dimension_fields = list(dict.fromkeys(required))
            if strict_initial:
                dimension_fields.extend(name for name in required_base if name not in dimension_fields)
            if include_budgets:
                dimension_fields.extend(("DNW", "MAPFAC_M"))
            for name in dict.fromkeys(dimension_fields):
                if name not in dataset.variables:
                    raise ValueError(f"{label}: {file_path.name} missing required {name}")
                if name in EXPECTED_DIMS and tuple(dataset.variables[name].dimensions) != EXPECTED_DIMS[name]:
                    actual_dims = tuple(dataset.variables[name].dimensions)
                    expected_dims = EXPECTED_DIMS[name]
                    raise ValueError(f"{label}: {name} dimensions {actual_dims} != {expected_dims}")
            current_grid = (dataset.dimensions["west_east"].size,
                            dataset.dimensions["south_north"].size,
                            dataset.dimensions["bottom_top"].size)
            current_attrs = (float(dataset.getncattr("DX")), float(dataset.getncattr("DY")))
            if current_grid != tuple(expected_grid[k] for k in ("nx", "ny", "nz")):
                raise ValueError(f"{label}: grid {current_grid} != expected {expected_grid}")
            if dimensions is None:
                dimensions, grid_attrs = current_grid, current_attrs
            elif current_grid != dimensions or current_attrs != grid_attrs:
                raise ValueError(f"{label}: grid/spacing changed in {file_path.name}")
            if strict_initial:
                missing_attrs = [name for name in required_attrs if name not in dataset.ncattrs()]
                if missing_attrs:
                    raise ValueError(f"{label}: missing required shared global attributes {missing_attrs}")
                attrs_here = tuple(name for name in optional_attrs if name in dataset.ncattrs())
                if base_attribute_availability is None:
                    base_attribute_availability = attrs_here
                elif attrs_here != base_attribute_availability:
                    raise ValueError(f"{label}: shared global-attribute availability changed in {file_path.name}")
                names_here = tuple(dict.fromkeys((*required_attrs, *attrs_here)))
                values_here = {name: dataset.getncattr(name) for name in names_here}
                if strict_attributes_in_files is None:
                    strict_attributes_in_files = values_here
                elif any(not _exact_value_equal(values_here[name], strict_attributes_in_files[name])
                         for name in names_here):
                    raise ValueError(f"{label}: shared global attributes changed in {file_path.name}")
                optional_fields_here = tuple(name for name in optional_base if name in dataset.variables)
                if base_optional_available is None:
                    base_optional_available = optional_fields_here
                elif optional_fields_here != base_optional_available:
                    raise ValueError(f"{label}: shared base-field availability changed in {file_path.name}")
                for name in optional_fields_here:
                    if (name in EXPECTED_DIMS and
                            tuple(dataset.variables[name].dimensions) != EXPECTED_DIMS[name]):
                        raise ValueError(f"{label}: {name} dimensions changed/invalid in {file_path.name}")
            times = [_time_text(row) for row in dataset.variables["Times"][:]]
            output_files.append({"path": str(file_path), "sha256": _sha256(file_path),
                                 "timestamps": times})
            for index, timestamp in enumerate(times):
                if timestamp in frames:
                    raise ValueError(f"{label}: duplicate exact timestamp {timestamp}")
                fields = {name: _float_array(dataset.variables[name], index) for name in required}
                if any(not np.isfinite(array).all() for array in fields.values()):
                    raise ValueError(f"{label}: raw field contains NaN or Inf at {timestamp}")
                if strict_initial and timestamp == ordered_times[0]:
                    missing = [name for name in required_base if name not in dataset.variables]
                    if missing:
                        raise ValueError(f"{label}: strict initial state missing required fields {missing}")
                    available_optional = list(base_optional_available or ())
                    base_names = tuple(dict.fromkeys((*required_base, *available_optional)))
                    for name in base_names:
                        if name not in dataset.variables:
                            raise ValueError(f"{label}: strict initial state missing required field {name}")
                    base_state = {
                        name: (fields[name] if name in fields else _numeric_array(dataset.variables[name], index))
                        for name in base_names
                    }
                    base_state_fields = list(base_names)
                    available_attrs = list(base_attribute_availability or ())
                    attr_names = tuple(dict.fromkeys((*required_attrs, *available_attrs)))
                    base_attributes = {name: dataset.getncattr(name) for name in attr_names}
                if dry_no_qv:
                    qvapor = _float_array(dataset.variables["QVAPOR"], index)
                    qvapor_max_abs = max(qvapor_max_abs, float(np.max(np.abs(qvapor))))
                    if np.any(qvapor != 0.0):
                        raise ValueError(f"{label}: dry_case_no_qv requested but QVAPOR is nonzero at {timestamp}")
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
                owned = _owned(fields, expected_grid)
                if include_pressure:
                    total_pressure = fields["P"] + fields["PB"]
                    if not np.isfinite(total_pressure).all() or np.any(total_pressure <= 0.0):
                        raise ValueError(f"{label}: total pressure P+PB is non-positive/non-finite at {timestamp}")
                    minimum_total_pressure = min(minimum_total_pressure, float(np.min(total_pressure)))
                    owned["P_total"] = total_pressure
                if "P" in wave_fields:
                    owned["P_perturbation"] = fields["P"]
                frames[timestamp] = owned
                if include_budgets:
                    dnw = _numeric_array(dataset.variables["DNW"], index)
                    mapfac = _numeric_array(dataset.variables["MAPFAC_M"], index)
                    if (not np.isfinite(dnw).all() or not np.isfinite(mapfac).all()
                            or np.any(mapfac <= 0.0)):
                        raise ValueError(f"{label}: invalid DNW or MAPFAC_M for dry budget at {timestamp}")
                    area = (current_attrs[0] * current_attrs[1]) / np.square(mapfac)
                    layer_mass = (total_mu[None, :, :] * np.abs(dnw)[:, None, None]
                                  / GRAVITY_M_S2 * area[None, :, :])
                    mass_kg = float(np.sum(layer_mass))
                    budgets[timestamp] = {
                        "dry_mass_kg": float(np.sum(total_mu / GRAVITY_M_S2 * area)),
                        "mass_weighted_theta_K": (float(np.sum(layer_mass * total_theta) / mass_kg)
                                                   if mass_kg > 0.0 else None),
                    }

    if strict_initial and (base_state is None or base_state_fields is None):
        raise ValueError(f"{label}: strict initial state fields were not captured at t=0")
    if dimensions != tuple(expected_grid[k] for k in ("nx", "ny", "nz")):
        raise ValueError(f"{label}: expected grid {expected_grid}, got {dimensions}")
    if include_budgets and budgets:
        first_mass = budgets[ordered_times[0]]["dry_mass_kg"]
        first_theta = budgets[ordered_times[0]]["mass_weighted_theta_K"]
        for value in budgets.values():
            value["dry_mass_relative_change"] = ((value["dry_mass_kg"] - first_mass) / first_mass
                                                 if first_mass > 0.0 else None)
            value["mass_weighted_theta_change_K"] = (value["mass_weighted_theta_K"] - first_theta
                                                      if first_theta is not None and value["mass_weighted_theta_K"] is not None
                                                      else None)

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
        "base_state": base_state,
        "base_state_fields": base_state_fields,
        "base_attributes": base_attributes,
        "qvapor_max_abs": qvapor_max_abs if dry_no_qv else None,
        "use_theta_m": use_theta_m if dry_no_qv else None,
        "theta_interpretation": ("dry potential temperature perturbation plus 300 K; not air temperature"
                                 if dry_no_qv else None),
        "physical_minimum_total_pressure_Pa": minimum_total_pressure if include_pressure else None,
        "physical_minima_over_all_frames": {
            "total_theta_K": minimum_total_theta,
            "total_mu_Pa": minimum_total_mu,
            "layer_depth_m_from_PH_plus_PHB_over_g": minimum_layer_depth,
            "g_m_s2": GRAVITY_M_S2},
        "timestamps": ordered_times,
        "frames": frames,
        "budgets": budgets if include_budgets else None,
        "step_logs": _read_step_logs(run, run_dir),
    }


def _validate_shared_initial_state(runs: dict[str, dict], plan: dict) -> dict:
    """Certify a shared initial condition before any field metrics are computed."""
    if not plan.get("require_shared_initial_state", False):
        return {"status": "not_requested", "comparison_certified": False}
    rows = list(runs.values())
    if not rows:
        raise ValueError("shared-initial-state gate requires at least one run")
    reference = rows[0]
    reference_input = reference.get("input_sha256")
    if not reference_input or not reference_input.get("sha256"):
        raise ValueError(f"{reference['label']}: strict comparison requires a verified input_sha256")
    reference_hash = reference_input["sha256"]
    reference_start = reference["timestamps"][0]
    reference_times = reference["timestamps"]
    reference_fields = set(reference.get("base_state_fields") or ())
    required_fields = set(BASE_STATE_FIELDS) | set(plan.get("required_shared_state_fields", ()))
    if plan.get("dry_case_no_qv", False):
        required_fields.add("QVAPOR")
    if not required_fields <= reference_fields:
        raise ValueError(f"{reference['label']}: missing strict base fields {sorted(required_fields - reference_fields)}")
    if reference.get("base_state") is None:
        raise ValueError(f"{reference['label']}: strict base state was not captured")

    for candidate in rows[1:]:
        if candidate["grid"] != reference["grid"]:
            raise ValueError(f"{candidate['label']}: strict initial-state grid differs from {reference['label']}")
        if candidate["timestamps"][0] != reference_start:
            raise ValueError(f"{candidate['label']}: strict initial-state start differs from {reference['label']}")
        if candidate["timestamps"] != reference_times:
            raise ValueError(f"{candidate['label']}: strict output timestamps differ from {reference['label']}")
        candidate_input = candidate.get("input_sha256")
        if not candidate_input or candidate_input.get("sha256") != reference_hash:
            raise ValueError(f"{candidate['label']}: strict verified wrfinput SHA-256 differs from {reference['label']}")
        fields = set(candidate.get("base_state_fields") or ())
        if fields != reference_fields:
            raise ValueError(f"{candidate['label']}: strict base-field availability differs from {reference['label']}")
        for name in sorted(required_fields):
            left, right = reference["base_state"][name], candidate["base_state"][name]
            if not _exact_numeric_equal(left, right):
                raise ValueError(f"{candidate['label']}: strict t=0 field {name} differs from {reference['label']}")
        for name in sorted(reference_fields - required_fields):
            if not _exact_numeric_equal(reference["base_state"][name], candidate["base_state"][name]):
                raise ValueError(f"{candidate['label']}: available t=0 coordinate/base field {name} differs")
        attrs = reference.get("base_attributes") or {}
        candidate_attrs = candidate.get("base_attributes") or {}
        if set(attrs) != set(candidate_attrs):
            raise ValueError(f"{candidate['label']}: strict shared global-attribute availability differs")
        if any(not _exact_value_equal(attrs[name], candidate_attrs[name]) for name in attrs):
            raise ValueError(f"{candidate['label']}: strict shared global attributes differ from {reference['label']}")
    expected_start = plan.get("start_timestamp")
    if expected_start is not None and reference_start != expected_start:
        raise ValueError(f"strict start timestamp {reference_start} != plan {expected_start}")
    return {
        "status": "passed",
        "comparison_certified": True,
        "source_kind": "wrfout_t0",
        "input_sha256": reference_hash,
        "start_timestamp": reference_start,
        "grid": reference["grid"],
        "base_fields_checked": sorted(reference_fields),
        "global_attributes_checked": sorted(reference.get("base_attributes") or {}),
        "signed_zero_equivalent": True,
        "comparison_method": "numerically exact array equality; byte/log text equality not required",
    }


def load_initial_state(run_dir: Path, expected_grid: dict[str, int] = DEFAULT_GRID,
                       fields: tuple[str, ...] | None = None,
                       dry_case_no_qv: bool = False,
                       use_theta_m: int | None = None,
                       source: str = "auto") -> dict:
    """Load saved t=0 from wrfinput, wrfout, or auto; never runs WRF."""
    run_dir = Path(run_dir).resolve()
    grid = _expected_grid({"expected_grid": expected_grid})
    if dry_case_no_qv and (type(use_theta_m) is not int or use_theta_m not in (0, 1)):
        raise ValueError("dry_case_no_qv initial-state check requires explicit use_theta_m=0 or 1")
    input_path = run_dir / "wrfinput_d01"
    if source not in ("auto", "wrfinput", "wrfout"):
        raise ValueError("source must be auto, wrfinput, or wrfout")
    if source == "wrfinput" or (source == "auto" and input_path.is_file()):
        if not input_path.is_file():
            raise ValueError(f"source=wrfinput requested but {input_path} is absent")
        files = [input_path]
        source_kind = "wrfinput"
    else:
        files = sorted(p for p in run_dir.glob("wrfout_d01*") if p.is_file())
        source_kind = "wrfout"
    if not files:
        raise ValueError(f"no {source} t=0 source files in {run_dir}")
    time_records = []
    for path in files:
        with Dataset(path, "r") as dataset:
            if "Times" not in dataset.variables:
                raise ValueError(f"{path.name} missing Times")
            time_records.extend((time_text, path, index) for index, time_text in enumerate(
                _time_text(row) for row in dataset.variables["Times"][:]))
    timestamps = [row[0] for row in time_records]
    if len(timestamps) != len(set(timestamps)):
        raise ValueError(f"duplicate timestamps under {run_dir}")
    start = min(timestamps, key=lambda value: datetime.strptime(value, "%Y-%m-%d_%H:%M:%S"))
    _, path, time_index = next(record for record in time_records if record[0] == start)
    with Dataset(path, "r") as dataset:
        actual_grid = (dataset.dimensions["west_east"].size,
                       dataset.dimensions["south_north"].size,
                       dataset.dimensions["bottom_top"].size)
        if actual_grid != tuple(grid[k] for k in ("nx", "ny", "nz")):
            raise ValueError(f"initial state grid {actual_grid} != expected {grid}")
        base_fields = list(fields or BASE_STATE_FIELDS)
        if dry_case_no_qv:
            base_fields.append("QVAPOR")
        if fields is None:
            base_fields.extend(name for name in AVAILABLE_BASE_FIELDS
                               if name in dataset.variables and name not in base_fields)
        values = {}
        for name in base_fields:
            if name not in dataset.variables:
                raise ValueError(f"initial state missing required {name}")
            if name in EXPECTED_DIMS and tuple(dataset.variables[name].dimensions) != EXPECTED_DIMS[name]:
                raise ValueError(f"initial state {name} has unexpected dimensions")
            values[name] = _numeric_array(dataset.variables[name], time_index)
            if not np.isfinite(values[name]).all():
                raise ValueError(f"initial state {name} contains NaN or Inf")
        qvapor_max = float(np.max(np.abs(values["QVAPOR"]))) if "QVAPOR" in values else None
        if dry_case_no_qv and qvapor_max != 0.0:
            raise ValueError("dry_case_no_qv requested but t=0 QVAPOR is nonzero")
        attrs = {name: dataset.getncattr(name) for name in
                 ("DX", "DY", *AVAILABLE_BASE_ATTRIBUTES) if name in dataset.ncattrs()}
    return {"path": str(run_dir), "timestamp": start,
            "grid": {"nx": actual_grid[0], "ny": actual_grid[1], "nz": actual_grid[2],
                     "dx_m": float(attrs["DX"]), "dy_m": float(attrs["DY"])},
            "input_sha256": _sha256(input_path) if input_path.is_file() else None,
            "source_kind": source_kind,
            "fields": values, "attributes": attrs, "qvapor_max_abs": qvapor_max,
            "use_theta_m": use_theta_m if dry_case_no_qv else None,
            "theta_interpretation": ("T+300 K is dry potential temperature; not air temperature"
                                     if dry_case_no_qv else None)}


def compare_initial_states(states: dict[str, dict], require_input_hash: bool = True) -> dict:
    """Numerically compare saved initial states; signed zeros compare equal."""
    if len(states) < 2:
        raise ValueError("at least two initial states are required")
    labels = list(states)
    reference_label, reference = labels[0], states[labels[0]]
    if require_input_hash and not reference.get("input_sha256"):
        raise ValueError(f"{reference_label}: wrfinput_d01 hash is required")
    for label in labels[1:]:
        candidate = states[label]
        if candidate.get("source_kind") != reference.get("source_kind"):
            raise ValueError(f"{label}: initial-state source differs from {reference_label}")
        if candidate["grid"] != reference["grid"] or candidate["timestamp"] != reference["timestamp"]:
            raise ValueError(f"{label}: initial grid/start differs from {reference_label}")
        if require_input_hash and candidate.get("input_sha256") != reference.get("input_sha256"):
            raise ValueError(f"{label}: wrfinput_d01 hash differs from {reference_label}")
        if set(candidate["fields"]) != set(reference["fields"]):
            raise ValueError(f"{label}: initial field manifest differs from {reference_label}")
        for name in reference["fields"]:
            if not _exact_numeric_equal(reference["fields"][name], candidate["fields"][name]):
                raise ValueError(f"{label}: t=0 field {name} differs from {reference_label}")
        if set(candidate["attributes"]) != set(reference["attributes"]):
            raise ValueError(f"{label}: initial global-attribute manifest differs from {reference_label}")
        if any(not _exact_value_equal(reference["attributes"][name], candidate["attributes"][name])
               for name in reference["attributes"]):
            raise ValueError(f"{label}: initial global attributes differ from {reference_label}")
    return {"status": "passed", "reference_label": reference_label,
            "labels": labels, "timestamp": reference["timestamp"],
            "source_kind": reference.get("source_kind"),
            "gate_scope": ("post-initialization first history frame" if reference.get("source_kind") == "wrfout"
                           else "input-state preflight; not post-initialization evidence"),
            "grid": reference["grid"], "input_sha256": reference.get("input_sha256"),
            "fields_checked": sorted(reference["fields"]),
            "signed_zero_equivalent": True}


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


def _metrics(candidate: np.ndarray, reference: np.ndarray, signal: np.ndarray,
             extra_metrics: tuple[str, ...] = ()) -> dict:
    difference = np.asarray(candidate, dtype=np.float64) - np.asarray(reference, dtype=np.float64)
    ref_signal = np.asarray(signal, dtype=np.float64)
    rms = float(np.sqrt(np.mean(difference * difference)))
    signal_rms = float(np.sqrt(np.mean(ref_signal * ref_signal)))
    result = {
        "rms": rms,
        "max_abs": float(np.max(np.abs(difference))),
        "reference_signal_rms_from_initial": signal_rms,
        "relative_rms_to_reference_signal": rms / signal_rms if signal_rms > 0.0 else None,
        "relative_rms_status": "defined" if signal_rms > 0.0 else "undefined_zero_reference_signal",
    }
    if "mean_bias" in extra_metrics:
        result["mean_bias"] = float(np.mean(difference))
    if "mean_absolute_error" in extra_metrics:
        result["mean_absolute_error"] = float(np.mean(np.abs(difference)))
    if "max_location" in extra_metrics:
        result["max_abs_index"] = [int(x) for x in np.unravel_index(np.argmax(np.abs(difference)), difference.shape)]
        result["max_abs_index_axes"] = ["z", "y", "x"] if difference.ndim == 3 else ["y", "x"]
    if "per_z" in extra_metrics:
        result["per_z"] = ([{
            "z_index": z,
            "mean_bias": float(np.mean(layer)),
            "mean_absolute_error": float(np.mean(np.abs(layer))),
            "rms": float(np.sqrt(np.mean(layer * layer))),
            "max_abs": float(np.max(np.abs(layer))),
        } for z, layer in enumerate(difference)] if difference.ndim == 3 else None)
    if "boundary_error_fraction" in extra_metrics:
        if difference.ndim == 3:
            boundary = np.zeros(difference.shape, dtype=bool)
            boundary[0, :, :] = True
            boundary[-1, :, :] = True
            boundary[:, 0, :] = True
            boundary[:, -1, :] = True
        elif difference.ndim == 2:
            boundary = np.zeros(difference.shape, dtype=bool)
            boundary[0, :] = True
            boundary[-1, :] = True
        else:
            boundary = np.zeros(difference.shape, dtype=bool)
        total = float(np.sum(np.abs(difference)))
        result["physical_boundary_mean_abs_error_fraction"] = (
            float(np.sum(np.abs(difference)[boundary])) / total if total > 0.0 else None
        )
    return result


def _wave_mode1(field: np.ndarray) -> np.ndarray:
    """Return complex zonal Fourier mode 1 coefficients on native (z,y) slices."""
    value = np.asarray(field, dtype=np.float64)
    if value.ndim != 3 or value.shape[-1] < 3:
        raise ValueError("mode-1 wave diagnostics require a (z,y,x) array with at least three x points")
    return np.fft.fft(value, axis=-1)[..., 1] / value.shape[-1]


def _wave_summary(coefficients: np.ndarray) -> dict:
    amp = float(np.sqrt(np.mean(np.abs(coefficients) ** 2)))
    return {"mode": 1, "coefficient_rms_amplitude": amp,
            "phase_rad": None,
            "phase_status": "undefined_zero_mode" if amp == 0.0 else "reported_pairwise_as_cross_phase"}


def _wave_pair(left: np.ndarray, right: np.ndarray) -> dict:
    left_power = float(np.sum(np.abs(left) ** 2))
    right_power = float(np.sum(np.abs(right) ** 2))
    left_amp = float(np.sqrt(left_power / left.size))
    right_amp = float(np.sqrt(right_power / right.size))
    cross = np.sum(right * np.conjugate(left))
    coherence = float(np.abs(cross) / np.sqrt(left_power * right_power)) if left_power > 0.0 and right_power > 0.0 else None
    phase = float(np.angle(cross)) if coherence is not None and cross != 0 else None
    return {"mode": 1, "left_coefficient_rms_amplitude": left_amp,
            "right_coefficient_rms_amplitude": right_amp,
            "right_to_left_amplitude_ratio": right_amp / left_amp if left_amp > 0.0 else None,
            "cross_phase_right_minus_left_rad": phase, "coherence": coherence,
            "phase_convention": "arg(sum(C_right * conjugate(C_left))) with no spatial alignment",
            "phase_status": "defined" if phase is not None else "undefined_zero_amplitude_or_cross_coherence"}


def _pairwise_sensitivities(runs: dict[str, dict], plan: dict, fields: tuple[str, ...],
                            extra_metrics: tuple[str, ...]) -> dict:
    specs = plan.get("comparison_pairs", ())
    if specs and not plan.get("require_shared_initial_state", False):
        raise ValueError("comparison_pairs require require_shared_initial_state=true")
    result, names = {}, set()
    for spec in specs:
        if not isinstance(spec, dict):
            raise ValueError("each comparison_pair must be an object")
        name, left, right = spec.get("name"), spec.get("left"), spec.get("right")
        if not all(isinstance(v, str) and v for v in (name, left, right)) or name in names:
            raise ValueError("comparison_pairs need unique name and left/right run labels")
        if left not in runs or right not in runs or left == right:
            raise ValueError(f"{name}: comparison_pair refers to unknown or identical run labels")
        a, b = runs[left], runs[right]
        if a["method"] != b["method"] or a["h_seconds"] == b["h_seconds"]:
            raise ValueError(f"{name}: timestep sensitivity requires same method and different timesteps")
        if a["grid"] != b["grid"] or a["timestamps"] != b["timestamps"]:
            raise ValueError(f"{name}: paired runs need identical grid and timestamps")
        if not a.get("base_state_fields") or not b.get("base_state_fields"):
            raise ValueError(f"{name}: strict shared initial state was not captured")
        base = a["frames"][a["timestamps"][0]]
        result[name] = {
            "left_label": left, "right_label": right, "method": a["method"],
            "h_seconds": {left: a["h_seconds"], right: b["h_seconds"]},
            "interpretation": "within-method timestep sensitivity; neither run is exact truth",
            "times": {timestamp: {
                field: _metrics(b["frames"][timestamp][field], a["frames"][timestamp][field],
                                a["frames"][timestamp][field] - base[field], extra_metrics)
                for field in fields} for timestamp in a["timestamps"]},
        }
        names.add(name)
    return result


def _wave_diagnostics(runs: dict[str, dict], plan: dict, comparison_pairs: list[dict]) -> tuple[dict, dict]:
    spec = plan.get("wave_mode1")
    if not spec:
        return {}, {}
    if spec is True:
        spec = {}
    fields = tuple(spec.get("fields", ("T", "W", "P")))
    field_key = {"P": "P_perturbation"}
    diagnostics = {}
    coeffs: dict[str, dict[str, dict[str, np.ndarray]]] = {}
    nx = next(iter(runs.values()))["grid"]["nx"]
    for label, run in runs.items():
        start = run["timestamps"][0]
        coeffs[label] = {}
        diagnostics[label] = {}
        for timestamp in run["timestamps"]:
            diagnostics[label][timestamp] = {}
            coeffs[label][timestamp] = {}
            for name in fields:
                key = field_key.get(name, name)
                if key not in run["frames"][timestamp] or key not in run["frames"][start]:
                    raise ValueError(f"{label}: wave mode-1 requires field {name}")
                coefficient = _wave_mode1(run["frames"][timestamp][key])
                coeffs[label][timestamp][name] = coefficient
                diagnostics[label][timestamp][name] = _wave_summary(coefficient)
    pairwise = {}
    for pair in comparison_pairs:
        left, right = pair["left"], pair["right"]
        pairwise[pair["name"]] = {}
        for timestamp in runs[left]["timestamps"]:
            pairwise[pair["name"]][timestamp] = {
                name: _wave_pair(coeffs[left][timestamp][name], coeffs[right][timestamp][name])
                for name in fields
            }
    for label in diagnostics:
        for time_record in diagnostics[label].values():
            for field_name, field_record in time_record.items():
                field_record["wavelength_m"] = nx * runs[label]["grid"]["dx_m"]
                field_record["coefficient_convention"] = "FFT(field, zonal axis, mode=1) / nx; no alignment"
                field_record["native_vertical_indexing"] = (
                    "bottom_top_stag; fixed lower interface excluded and top retained"
                    if field_name == "W" else "bottom_top mass levels")
    return diagnostics, pairwise


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
    grid = _expected_grid(plan)
    duration_seconds = plan.get("duration_seconds", ENDPOINT_SECONDS)
    if type(duration_seconds) is not int or duration_seconds <= 0:
        raise ValueError("duration_seconds must be a positive integer")
    include_pressure = bool(plan.get("include_total_pressure", False))
    fields = tuple(plan.get("comparison_fields", (*VARIABLE_UNITS, *(PRESSURE_UNITS if include_pressure else ()))))
    units = {**VARIABLE_UNITS, **(PRESSURE_UNITS if include_pressure else {})}
    unknown_fields = [name for name in fields if name not in units]
    if unknown_fields:
        raise ValueError(f"comparison_fields request unavailable fields: {unknown_fields}")
    if "P_total" in fields and not include_pressure:
        raise ValueError("P_total comparison requires include_total_pressure=true")
    extra_metrics = tuple(plan.get("extra_metrics", ()))
    allowed_metrics = {"mean_bias", "mean_absolute_error", "per_z", "max_location", "boundary_error_fraction"}
    if not set(extra_metrics) <= allowed_metrics:
        raise ValueError(f"unknown extra_metrics: {sorted(set(extra_metrics) - allowed_metrics)}")
    plan_dir = plan_path.parent
    runs = {run["label"]: _load_run(run, plan_dir, plan) for run in rows}
    shared_gate = _validate_shared_initial_state(runs, plan)
    reference_run = runs[reference_label]
    reference_start = reference_run["timestamps"][0]
    expected_endpoint = (datetime.strptime(reference_start, "%Y-%m-%d_%H:%M:%S") +
                         timedelta(seconds=duration_seconds)).isoformat(sep="_", timespec="seconds")
    initial = reference_run["frames"][reference_start]
    result = {
        "schema": "wrf-dry-accuracy-cost-analysis-v1",
        "comparison_scope": ("whole-run WRF result; method and cost ratio are not integrator-only "
                             "unless source/build receipts prove that scope"),
        "model": plan.get("model", "em_b_wave"),
        "endpoint_seconds": duration_seconds,
        "expected_grid": grid,
        "reference_label": reference_label,
        "reference_interpretation": "comparator, not exact truth",
        "reference_check_label": reference_check_label,
        "reference_resolutions": {
            "primary": {"label": reference_run["label"], "h_seconds": reference_run["h_seconds"],
                        "method": reference_run["method"]},
            "second": ({"label": runs[reference_check_label]["label"],
                        "h_seconds": runs[reference_check_label]["h_seconds"],
                        "method": runs[reference_check_label]["method"]}
                       if reference_check_label is not None else None),
        },
        "reference_start_timestamp": reference_start,
        "reference_endpoint_timestamp": expected_endpoint,
        "shared_initial_state_gate": shared_gate,
        "owned_points": {
            "U": "exclude periodic duplicate x face",
            "V": "include both physical y walls",
            "W": "exclude fixed lower z face; include top",
            "PH": "exclude fixed lower z face; include top",
            "T": "perturbation potential temperature; physical theta is T+300 K",
            "MU": "perturbation dry column mass; physical dry column mass is MU+MUB",
            "PH+PHB": "positive layer depth from diff(PH+PHB)/9.81",
            "P_total": "optional total pressure P+PB; not air temperature",
        },
        "owned_point_counts_for_16_cubed_case": dict(OWNED_COUNTS) if grid == DEFAULT_GRID else None,
        "owned_point_counts": {"U": grid["nz"] * grid["ny"] * grid["nx"],
                               "V": grid["nz"] * (grid["ny"] + 1) * grid["nx"],
                               "W": grid["nz"] * grid["ny"] * grid["nx"],
                               "PH": grid["nz"] * grid["ny"] * grid["nx"],
                               "T": grid["nz"] * grid["ny"] * grid["nx"],
                               "MU": grid["ny"] * grid["nx"]},
        "units": units,
        "runs": [],
        "comparisons": {},
        "reference_step_sensitivity": None,
        "comparison_pair_sensitivities": {},
        "wave_mode1_diagnostics": {},
        "wave_pairwise_diagnostics": {},
        "cost": {},
    }
    if plan.get("dry_case_no_qv", False):
        result["theta_interpretation"] = (
            f"T+300 K is potential temperature with use_theta_m={plan['use_theta_m']}; "
            "QVAPOR was verified exactly zero. This is not air temperature."
        )
    if plan.get("include_mass_theta_budgets", False):
        result["budget_interpretation"] = ("Dry mass and mass-weighted theta diagnostics only; neither is a "
                                           "pass/fail conservation gate or an energy claim.")
        result["budgets"] = {label: run["budgets"] for label, run in runs.items()}
    internal = {"frames", "timestamps", "base_state", "base_state_fields", "base_attributes", "budgets"}
    for run in runs.values():
        summary = {key: value for key, value in run.items() if key not in internal and value is not None}
        result["runs"].append(summary)
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
        if run["grid"] != reference_run["grid"]:
            raise ValueError(f"{label}: candidate/reference grid or spacing differs")
        result["comparisons"][label] = {}
        for timestamp in common:
            current = run["frames"][timestamp]
            reference = reference_run["frames"][timestamp]
            signal = {name: reference[name] - initial[name] for name in fields}
            result["comparisons"][label][timestamp] = {
                name: _metrics(current[name], reference[name], signal[name], extra_metrics)
                for name in fields
            }
    if reference_check_label is not None:
        fine = runs[reference_check_label]
        if fine["grid"] != reference_run["grid"]:
            raise ValueError("reference and reference-check grids differ")
        common = sorted(set(reference_run["frames"]) & set(fine["frames"]))
        if reference_start not in common or expected_endpoint not in common:
            raise ValueError("reference-check lacks exact shared initial/endpoint timestamps")
        result["reference_step_sensitivity"] = {
            "primary_label": reference_label, "second_label": reference_check_label,
            "interpretation": "difference between two archived reference resolutions; not exact truth or a temporal-order claim",
            "primary_h_seconds": reference_run["h_seconds"], "second_h_seconds": fine["h_seconds"],
            "times": {timestamp: {
                name: _metrics(fine["frames"][timestamp][name], reference_run["frames"][timestamp][name],
                               reference_run["frames"][timestamp][name] - initial[name], extra_metrics)
                for name in fields} for timestamp in common},
        }
    comparison_pairs = plan.get("comparison_pairs", ())
    result["comparison_pair_sensitivities"] = _pairwise_sensitivities(runs, plan, fields, extra_metrics)
    wave_diagnostics, wave_pairs = _wave_diagnostics(runs, plan, comparison_pairs)
    result["wave_mode1_diagnostics"] = wave_diagnostics
    result["wave_pairwise_diagnostics"] = wave_pairs
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
            "whole-path run wall from supplied plan receipts; not an integrator-only ratio")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(result, indent=2) + "\n")
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runs-json", type=Path, required=True,
                        help="structured run plan with labels, directories, h_seconds, methods, and optional receipts")
    parser.add_argument("--reference", required=True,
                        help="label of the archived dry RK3 reference")
    parser.add_argument("--reference-check",
                        help="optional second RK3 resolution for reference-step sensitivity (not truth/order)")
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
