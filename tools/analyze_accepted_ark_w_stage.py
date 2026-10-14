#!/usr/bin/env python3
"""Analyze one accepted native ARK step from existing stage-observer CSVs.

The input is diagnostic output only: this script does not call the model or
evaluate an RHS. Solver/tableau residuals are reported as measured defects,
not state-error bounds. CSV rows use the existing A/M/T protocol reader.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
from typing import Any

import numpy as np

try:  # Direct execution from tools/ and import as tools.* both work.
    from test_native_wave_refinement import read_payload
except ImportError:  # pragma: no cover - package import path
    from tools.test_native_wave_refinement import read_payload


def _sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def _need(mapping: dict, key: str, label: str) -> Any:
    if key not in mapping:
        raise ValueError(f"{label}: required field {key!r} is absent")
    return mapping[key]


def _truth(m: dict[str, float], key: str, label: str) -> bool:
    value = float(_need(m, key, label))
    if value not in (0.0, 1.0):
        raise ValueError(f"{label}: {key} must be encoded as 0 or 1, got {value}")
    return value == 1.0


def _finite(name: str, a: np.ndarray) -> np.ndarray:
    x = np.asarray(a, dtype=np.float64)
    if not np.isfinite(x).all():
        raise ValueError(f"{name}: non-finite values")
    return x


def _stats(values: np.ndarray) -> dict[str, float | int]:
    x = _finite("stats", np.asarray(values).reshape(-1))
    if x.size == 0:
        raise ValueError("cannot summarize an empty owner set")
    return {"count": int(x.size), "rms": float(np.sqrt(np.mean(x * x))),
            "max_abs": float(np.max(np.abs(x))), "mean": float(np.mean(x))}


def _blocks(nx: int, ny: int, nz: int) -> dict[str, tuple[int, tuple[int, ...]]]:
    nu, nv, nw = nx + 1, ny + 1, nz + 1
    sizes = {"u": (ny * nz * nu, (ny, nz, nu)),
             "v": (nv * nz * nx, (nv, nz, nx)),
             "w": (ny * nw * nx, (ny, nw, nx)),
             "ph": (ny * nw * nx, (ny, nw, nx)),
             "t": (ny * nz * nx, (ny, nz, nx)),
             "mu": (ny * nx, (ny, nx))}
    offsets: dict[str, tuple[int, tuple[int, ...]]] = {}
    start = 0
    for name, (size, shape) in sizes.items():
        offsets[name] = (start, shape)
        start += size
    offsets["_total"] = (start, ())
    return offsets


def _state_block(a: np.ndarray, name: str, offsets: dict) -> np.ndarray:
    start, shape = offsets[name]
    size = math.prod(shape)
    return a[start:start + size].reshape(shape)


def _row_stats(a: np.ndarray, mask: np.ndarray) -> dict[str, float | int]:
    return _stats(np.asarray(a)[mask])


def _periodic_pad_2d(a: np.ndarray, ny: int, nx: int) -> np.ndarray:
    """Pad physical source cells with the stored periodic closing row/column."""
    x = np.asarray(a, dtype=np.float64)
    if x.shape != (ny - 1, nx - 1):
        raise ValueError(f"physical field shape {x.shape}, expected {(ny - 1, nx - 1)}")
    out = np.empty((ny, nx), dtype=np.float64)
    out[:-1, :-1] = x
    out[:-1, -1] = x[:, 0]
    out[-1, :-1] = x[-1, :]
    out[-1, -1] = x[-1, 0]
    return out


def _source_dynamic_top_w_acceleration(
    p_prime_pa: np.ndarray, mu_prime_pa: np.ndarray, mub_pa: np.ndarray,
    c1f: np.ndarray, c2f_pa: np.ndarray, rdnw_signed: np.ndarray,
    gravity_m_s2: float,
) -> dict[str, np.ndarray]:
    """Derive the separate WRF dynamic-top closure from accepted-Y operands.

    Interior W terms are measured from the same-call capture. The top closure
    follows the independently reviewed E307 helper snapshot at SHA256
    1e516540a9320d60bbe081cf3466b00bd9228b0a478af7d257b89930035acb65.
    The pinned dry case has exactly-zero QVAPOR, so cqw=0.
    """
    p = _finite("p_pert", p_prime_pa)
    mu = _finite("MU", mu_prime_pa)
    mub = _finite("MUB", mub_pa)
    c1f = _finite("C1F", c1f).reshape(-1)
    c2f = _finite("C2F", c2f_pa).reshape(-1)
    rdnw = _finite("RDNW", rdnw_signed).reshape(-1)
    ny, nz, nx = p.shape
    if mu.shape != (ny, nx) or mub.shape != (ny, nx):
        raise ValueError("top closure p/MU/MUB dimensions do not match")
    if c1f.size != nz + 1 or c2f.size != nz + 1 or rdnw.size < nz:
        raise ValueError("top closure source coefficient dimensions do not match")
    lf = c1f[nz] * (mub + mu) + c2f[nz]
    if np.any(~np.isfinite(lf)) or np.any(lf <= 0.0):
        raise ValueError("top closure hybrid pressure is not positive")
    # Fortran top closure: 2*RDNW* (p'_top=0 minus p' at last mass level).
    pressure_bracket = 2.0 * rdnw[nz - 1] * (-p[:, nz - 1, :])
    mu_bracket = -c1f[nz] * mu
    scale = float(gravity_m_s2) / lf
    return {"pressure": scale * pressure_bracket,
            "mu_buoyancy": scale * mu_bracket,
            "moisture_buoyancy": np.zeros_like(mu)}


def _load_input(path: Path, expected_sha: str | None) -> dict[str, Any]:
    actual_sha = _sha256(path)
    if expected_sha and actual_sha.lower() != expected_sha.lower():
        raise ValueError(f"input SHA256 mismatch: expected {expected_sha}, got {actual_sha}")
    try:
        from netCDF4 import Dataset
    except ImportError as exc:  # pragma: no cover - depends on validation env
        raise RuntimeError("netCDF4 is required when --input is supplied") from exc
    with Dataset(path) as ds:
        def var(name: str) -> np.ndarray:
            if name not in ds.variables:
                raise ValueError(f"input NetCDF lacks {name}")
            value = np.asarray(ds.variables[name][:], dtype=np.float64)
            while value.ndim > 1 and value.shape[0] == 1:
                value = value[0]
            return _finite(name, value)

        fields = {name: var(name) for name in
                  ("MUB", "C1H", "C2H", "C1F", "C2F", "DNW", "RDN", "RDNW",
                   "MAPFAC_MX", "MAPFAC_MY")}
        attrs = {name: float(getattr(ds, name)) for name in ("DX", "DY")
                 if hasattr(ds, name)}
        if "DX" not in attrs or "DY" not in attrs:
            raise ValueError("input NetCDF must provide DX and DY global attributes")
        if "QVAPOR" in ds.variables:
            fields["QVAPOR"] = var("QVAPOR")
    return {"sha256": actual_sha, "fields": fields, **attrs}


def _check_receipt_input(receipt_path: Path | None, input_path: Path | None) -> str | None:
    if receipt_path is None:
        return None
    receipt = json.loads(receipt_path.read_text())
    if input_path is None:
        # Receipts in this workflow may bind the input either as a direct path
        # or in an inputs map. Requiring explicit --input keeps the join clear.
        raise ValueError("--run-receipt requires --input so the actual input bytes can be checked")
    expected: list[str] = []
    def walk(node: Any) -> None:
        if isinstance(node, dict):
            for k, v in node.items():
                if isinstance(v, str) and "sha256" in k.lower() and (
                    "input" in k.lower() or "wrfinput" in k.lower()
                ):
                    expected.append(v)
                else:
                    walk(v)
        elif isinstance(node, list):
            for v in node:
                walk(v)
    walk(receipt)
    actual = _sha256(input_path)
    if not expected:
        raise ValueError("run receipt has no discoverable input SHA256 binding")
    if actual not in expected:
        raise ValueError(f"run receipt does not bind --input SHA256 {actual}")
    return actual


def analyze(stage_paths: list[Path], out_path: Path, input_path: Path | None = None,
            receipt_path: Path | None = None) -> dict[str, Any]:
    if len(stage_paths) != 1:
        raise ValueError("provide exactly four stage CSVs from one accepted macro step")
    if input_path is None:
        raise ValueError("--input is required to derive physical W owners and source static mass/grid fields")
    csv_path = stage_paths[0]
    meta, arrays, _scalars, text = read_payload(csv_path)
    label = str(csv_path)
    schema = text.get("schema")
    if schema not in ("accepted_ark_w_stage_v1", "accepted_ark_w_stage_v2"):
        raise ValueError(f"{label}: unsupported schema {text.get('schema')!r}")
    if text.get("packed_order") != "U_V_W_PH_T_MU":
        raise ValueError(f"{label}: unexpected packed_order {text.get('packed_order')!r}")
    nx, ny, nz = (int(_need(meta, k, label)) for k in ("nx", "ny", "nz"))
    if min(nx, ny, nz) <= 0:
        raise ValueError(f"{label}: invalid dimensions {(nx, ny, nz)}")
    dt = float(_need(meta, "dt_seconds", label))
    if not np.isfinite(dt) or dt <= 0:
        raise ValueError(f"{label}: dt_seconds must be positive")
    if not _truth(meta, "accepted", label) or int(_need(meta, "accepted_outcome_code", label)) != 0:
        raise ValueError(f"{label}: step is not an accepted advance")
    if not _truth(meta, "physics_exact_zero_check_required", label):
        raise ValueError(f"{label}: zero-physics case gate was not required")
    if int(_need(meta, "mass_coordinate_mode", label)) != 1:
        raise ValueError(f"{label}: expected WRF-parity mass-coordinate mode 1")
    if not _truth(meta, "effective_wrf_omega_ww_cp", label):
        raise ValueError(f"{label}: effective WRF omega/ww path is not enabled")
    if float(_need(meta, "omega_w_blend", label)) != 1.0:
        raise ValueError(f"{label}: expected current-state omega blend 1")
    if not _truth(meta, "buoyancy_use_current_w", label):
        raise ValueError(f"{label}: expected buoyancy to use current W")
    input_record = _check_receipt_input(receipt_path, input_path)
    if input_record is None:
        input_record = _sha256(input_path)
    static = _load_input(input_path, input_record)

    off = _blocks(nx, ny, nz)
    total = off["_total"][0]
    sizes = {k: math.prod(v[1]) for k, v in off.items() if k != "_total"}
    if total != sum(sizes.values()):
        raise AssertionError("packed size accounting failed")
    def state(name: str) -> np.ndarray:
        a = _finite(name, np.asarray(_need(arrays, name, label)))
        if a.size != total:
            raise ValueError(f"{label}: {name} has {a.size}, expected packed state size {total}")
        return a
    initial = state("input")
    physics = state("physics")
    if np.any(physics != 0.0):
        raise ValueError(f"{label}: physics vector is not exactly zero")
    raw_final, projected_final = state("raw_final"), state("projected_final")
    if not all(int(_need(meta, k, label)) == expected for k, expected in
               (("nx_u", nx + 1), ("ny_v", ny + 1), ("nz_w", nz + 1))):
        raise ValueError(f"{label}: staggered dims do not match cell dims")

    w_start, w_shape = off["w"]
    w_size = sizes["w"]
    mub_shape = static["fields"]["MUB"].shape
    if schema == "accepted_ark_w_stage_v2" and not (
        "physical_owners_nx" in meta and "physical_owners_ny" in meta
    ):
        raise ValueError(f"{label}: v2 trace must declare physical owner dimensions")
    physical_nx = int(meta.get("physical_owners_nx", mub_shape[1]))
    physical_ny = int(meta.get("physical_owners_ny", mub_shape[0]))
    if (physical_ny, physical_nx) != mub_shape:
        raise ValueError(f"{label}: physical owner dims {(physical_ny, physical_nx)} "
                         f"do not match input MUB {mub_shape}")
    if (physical_ny, physical_nx) != (ny - 1, nx - 1):
        raise ValueError(f"{label}: input physical dims {(physical_ny, physical_nx)} "
                         f"must match packed periodic dimensions {(ny - 1, nx - 1)}")
    # WRF input holds physical mass points; the trace adds the periodic closing
    # row/column at the final indices. Keep those aliases out of the owner set.
    owner = np.zeros((ny, nx), dtype=bool)
    owner[:mub_shape[0], :mub_shape[1]] = True
    fixed_bottom = np.zeros((ny, nz + 1, nx), dtype=bool)
    fixed_bottom[:, 0, :] = True
    top_lid = bool(_truth(meta, "split_explicit_top_lid", label))
    w_owner = np.broadcast_to(owner[:, None, :], w_shape).copy()
    w_owner[:, 0, :] = False
    if top_lid:
        w_owner[:, nz, :] = False
    w_interior_owner = w_owner.copy()
    w_interior_owner[:, nz, :] = False
    w_top_owner = np.zeros_like(w_owner)
    if not top_lid:
        w_top_owner[:, nz, :] = owner
    expected_w_owners = int(owner.sum()) * (nz - (1 if top_lid else 0))
    if int(w_owner.sum()) != expected_w_owners:
        raise AssertionError("W owner mask accounting failed")
    static_mub_padded = _periodic_pad_2d(static["fields"]["MUB"], ny, nx)
    static_msftx_padded = _periodic_pad_2d(static["fields"]["MAPFAC_MX"], ny, nx)
    static_msfty_padded = _periodic_pad_2d(static["fields"]["MAPFAC_MY"], ny, nx)

    ae = np.stack([_finite(f"tableau_AE_s{i}", _need(arrays, f"tableau_AE_s{i}", label))
                   for i in range(1, 5)])
    ai = np.stack([_finite(f"tableau_AI_s{i}", _need(arrays, f"tableau_AI_s{i}", label))
                   for i in range(1, 5)])
    be = _finite("tableau_bE", _need(arrays, "tableau_bE", label)).reshape(-1)
    bi = _finite("tableau_bI", _need(arrays, "tableau_bI", label)).reshape(-1)
    if ae.shape != (4, 4) or ai.shape != (4, 4) or be.size != 4 or bi.size != 4:
        raise ValueError(f"{label}: malformed source-exported ARK tableau")
    if not np.array_equal(be, bi):
        raise ValueError("this source-order full-RHS replay requires bE == bI")

    stages: list[dict[str, Any]] = []
    slow_w, fast_w, full_w, ys_w = [], [], [], []
    stage_states = {name: [] for name in ("slow", "fast", "full")}
    w_capture_complete = True
    diagnostic_arrays: dict[str, np.ndarray] = {}
    for i in range(4):
        n = i + 1
        if int(_need(meta, f"s{n}_stage_id", label)) != n:
            raise ValueError(f"{label}: stage id mismatch at stage {n}")
        gates = ["gate_accepted"]
        if n > 1:
            gates.append("solver_converged")
        for gate in gates:
            if not _truth(meta, f"s{n}_{gate}", label):
                raise ValueError(f"{label}: stage {n} failed {gate}")
        explicit = _truth(meta, f"s{n}_explicit_stage", label)
        postprocess_unchanged = _truth(meta, f"s{n}_postprocess_unchanged", label)
        postprocess_equality_observed = meta.get(f"s{n}_postprocess_equality_observed")
        postprocess_equality_status = text.get(f"s{n}_postprocess_equality_status")
        if explicit != (n == 1):
            raise ValueError(f"{label}: stage {n} explicit/Newton classification is inconsistent")
        if _truth(meta, f"s{n}_fast_guard_applied", label):
            raise ValueError(f"{label}: stage {n} fast guard altered accepted K; attribution is ambiguous")
        base, y = state(f"base_s{n}"), state(f"y_s{n}")
        fast, slow, full = (state(f"k_fast_s{n}"), state(f"k_slow_s{n}"), state(f"k_full_s{n}"))
        full_split = full - fast - slow
        stage_states["fast"].append(fast)
        stage_states["slow"].append(slow)
        stage_states["full"].append(full)
        yi_w = _state_block(y, "w", off)
        ys_w.append(yi_w)

        # Rebuild B_i in the exact production source order from prior accepted
        # slow and fast terms; this is a replay check, not an alternate solver.
        base_replay = initial.copy()
        for j in range(i):
            base_replay = (base_replay + dt * ae[i, j] * stage_states["slow"][j]
                           + dt * ai[i, j] * stage_states["fast"][j])
        history_name = f"history_base_s{n}"
        history_base = state(history_name) if history_name in arrays else base
        history_replay_delta = history_base - base_replay
        accepted_base_delta = base - history_base
        diagnostic_arrays[f"history_base_replay_delta_w_s{n}"] = _state_block(
            history_replay_delta, "w", off)
        diagnostic_arrays[f"accepted_base_sanitization_delta_w_s{n}"] = _state_block(
            accepted_base_delta, "w", off)
        diagnostic_arrays[f"y_w_s{n}"] = yi_w
        diagnostic_arrays[f"k_fast_w_s{n}"] = _state_block(fast, "w", off)
        diagnostic_arrays[f"k_slow_w_s{n}"] = _state_block(slow, "w", off)
        diagnostic_arrays[f"k_full_w_s{n}"] = _state_block(full, "w", off)

        residual: dict[str, Any] = {"available": False}
        fi_name, g_name = f"fi_checked_s{n}", f"g_checked_s{n}"
        fi_available = _truth(meta, f"s{n}_fi_available", label)
        fi_observed = _truth(meta, f"s{n}_fi_evaluation_observed", label)
        checked_matches_y = _truth(meta, f"s{n}_checked_state_matches_y", label)
        fi_vector: np.ndarray | None = None
        raw_fi_at_y = False
        method_k_minus_fi: dict[str, Any] | None = None
        reference_flags = {
            "fi_ref_matches_base": _truth(meta, f"s{n}_fi_ref_matches_base", label),
            "fi_ref_matches_checked_state": _truth(
                meta, f"s{n}_fi_ref_matches_checked_state", label),
            "full_ref_matches_base": _truth(meta, f"s{n}_full_ref_matches_base", label),
            "full_ref_matches_checked_state": _truth(
                meta, f"s{n}_full_ref_matches_checked_state", label),
            "slow_ref_matches_base": _truth(meta, f"s{n}_slow_ref_matches_base", label),
            "slow_ref_matches_y": _truth(meta, f"s{n}_slow_ref_matches_y", label),
        }
        if n > 1:
            if not reference_flags["fi_ref_matches_base"]:
                raise ValueError(f"{label}: stage {n} FI reference is not the recorded tableau base")
            if not reference_flags["full_ref_matches_base"]:
                raise ValueError(f"{label}: stage {n} Full reference is not the recorded tableau base")
            if not reference_flags["slow_ref_matches_y"]:
                raise ValueError(f"{label}: stage {n} slow RHS reference is not accepted Y")
        checked_state_name = f"checked_state_s{n}"
        checked_array_matches_y = (checked_state_name in arrays and
                                   np.array_equal(state(checked_state_name), y))
        raw_fi_at_y = fi_observed and checked_matches_y and checked_array_matches_y and fi_name in arrays
        if fi_available and not raw_fi_at_y:
            raise ValueError(f"{label}: stage {n} FI-availability flag lacks same-Y observation gates")
        if raw_fi_at_y:
            fi_vector = state(fi_name)
            method_k_minus_fi = {
                "w_owner_difference_m_s2": _row_stats(
                    _state_block(fast - fi_vector, "w", off), w_owner),
                "scope": "accepted method K minus directly observed same-Y raw FI RHS",
                "accepted_fi_composite_gate": fi_available,
                "postprocess_unchanged": postprocess_unchanged}
            diagnostic_arrays[f"method_K_minus_FI_RK_w_s{n}"] = _state_block(
                fast - fi_vector, "w", off)
        if n == 1:
            residual["unavailable_reason"] = "stage 1 is explicit; no nonlinear solver defect is defined"
        elif raw_fi_at_y:
            aii = float(_need(meta, f"s{n}_effective_a_ii", label))
            calculated = y - base - dt * aii * fi_vector
            diagnostic_arrays[f"G_residual_w_s{n}"] = _state_block(calculated, "w", off)
            residual = {"available": True, "units": "W component m/s",
                        "w_owner_stats": _row_stats(_state_block(calculated, "w", off), w_owner),
                        "scope": "measured G=Y-B-h*a_ii*FI(Y), not a state-error bound"}
            if g_name in arrays:
                g = state(g_name)
                diff = calculated - g
                residual["versus_captured_G"] = {
                    "w_owner_difference": _row_stats(_state_block(diff, "w", off), w_owner)}
        else:
            residual["unavailable_reason"] = "same-point FI evaluation was unavailable"
        residual["raw_FI_observed_at_accepted_Y"] = raw_fi_at_y
        residual["composite_fi_available_flag"] = fi_available
        if method_k_minus_fi is not None:
            residual["method_K_minus_FI_RK"] = method_k_minus_fi
        returned_name, final_k_name = f"newton_returned_K_s{n}", f"newton_final_K_s{n}"
        if final_k_name in arrays:
            final_k = state(final_k_name)
            residual["newton_final_K_vs_method_K"] = {
                "w_owner_difference_m_s2": _row_stats(
                    _state_block(final_k - fast, "w", off), w_owner)}
            diagnostic_arrays[f"newton_final_K_minus_method_K_w_s{n}"] = _state_block(
                final_k - fast, "w", off)
        if returned_name in arrays and final_k_name in arrays:
            returned_k = state(returned_name)
            final_k = state(final_k_name)
            residual["newton_postprocess_returned_vs_final"] = {
                "w_owner_difference_m_s2": _row_stats(
                    _state_block(final_k - returned_k, "w", off), w_owner),
                "comparison_available": True}
        else:
            residual["newton_postprocess_returned_vs_final"] = {
                "comparison_available": False,
                "returned_K_present": returned_name in arrays,
                "final_K_present": final_k_name in arrays,
                "scope": "postprocess_unchanged=false is not evidence of K change without both snapshots"}
        full_array_present = f"full_actual_s{n}" in arrays
        full_available = _truth(meta, f"s{n}_full_available", label)
        full_observed = _truth(meta, f"s{n}_full_evaluation_observed", label)
        if full_array_present and full_available and full_observed and raw_fi_at_y:
            if not checked_array_matches_y:
                raise ValueError(f"{label}: stage {n} exact checked-state array/Y join is unavailable")
            full_actual = state(f"full_actual_s{n}")
            method_remainder = full_actual - fast - slow
            rhs_remainder = full_actual - fi_vector - slow
            diagnostic_arrays[f"full_minus_method_K_FE_w_s{n}"] = _state_block(
                method_remainder, "w", off)
            diagnostic_arrays[f"full_minus_FI_FE_w_s{n}"] = _state_block(
                rhs_remainder, "w", off)
            residual["full_remainders"] = {
                "method_K_plus_FE_remainder_w_owner_m_s2": _row_stats(
                    _state_block(method_remainder, "w", off), w_owner),
                "actual_FI_plus_FE_remainder_w_owner_m_s2": _row_stats(
                    _state_block(rhs_remainder, "w", off), w_owner),
                "scope": "same checked-Y RHS; first uses method K, second uses raw FI(Y)"}
        elif full_array_present or full_available or full_observed:
            residual["full_rhs_comparison_unavailable"] = {
                "array_present": full_array_present,
                "metadata_available": full_available,
                "evaluation_observed": full_observed,
                "reason": "same-Y FI/Full provenance gates did not all pass"}

        cap = _truth(meta, f"s{n}_w_capture_available", label)
        pressure_at_y = _truth(meta, f"s{n}_w_pressure_at_accepted_y", label)
        capture: dict[str, Any] = {"raw_capture_available": cap,
                                   "available_at_accepted_y": cap and pressure_at_y,
                                   "pressure_at_accepted_y": pressure_at_y,
                                   "postprocess_unchanged": postprocess_unchanged}
        if cap:
            for key in ("w_capture_identity_valid", "w_conversion_observed"):
                if not _truth(meta, f"s{n}_{key}", label):
                    raise ValueError(f"{label}: stage {n} W capture failed {key}")
            if int(_need(meta, f"s{n}_w_capture_rhs_mode", label)) != 1:
                raise ValueError(f"{label}: stage {n} W capture is not the expected FI mode")
            fields = {}
            for term in ("pg", "buoy_mu1", "buoy_mu2", "w_pgf_buoy_all",
                         "velocity_mass_w", "mu_tend_at_w", "conversion_rate",
                         "w_tend_after_conversion"):
                a = _finite(f"{term}_s{n}", _need(arrays, f"{term}_s{n}", label))
                if a.size != w_size:
                    raise ValueError(f"{label}: {term}_s{n} size {a.size} != W size {w_size}")
                fields[term] = a.reshape(w_shape)
                diagnostic_arrays[f"{term}_w_s{n}"] = fields[term]
            den = fields["velocity_mass_w"]
            if np.any(den[w_owner] <= 0.0):
                raise ValueError(f"{label}: non-positive W velocity mass on owned faces")
            # C++ source convention is pg - buoy_mu1 - buoy_mu2.
            converted = (fields["pg"] - fields["buoy_mu1"] - fields["buoy_mu2"])
            capture["pg_minus_buoyancy_vs_all"] = _row_stats(
                (converted - fields["w_pgf_buoy_all"]), w_owner)
            capture["term_capture_vertical_extent"] = (
                "measured interior W faces only; dynamic-top closure is source-derived separately")
            capture["captured_interior_pressure_acceleration_m_s2"] = _row_stats(
                (fields["pg"] / den)[w_interior_owner],
                np.ones(int(w_interior_owner.sum()), bool))
            capture["captured_interior_mu_buoyancy_acceleration_m_s2"] = _row_stats(
                (-fields["buoy_mu1"] / den)[w_interior_owner],
                np.ones(int(w_interior_owner.sum()), bool))
            capture["captured_interior_moisture_buoyancy_acceleration_m_s2"] = _row_stats(
                (-fields["buoy_mu2"] / den)[w_interior_owner],
                np.ones(int(w_interior_owner.sum()), bool))
            capture["captured_top_pressure_acceleration_m_s2"] = _row_stats(
                (fields["pg"] / den)[w_top_owner], np.ones(int(w_top_owner.sum()), bool))
            capture["w_owner_count"] = expected_w_owners
            p_meta = _truth(meta, f"s{n}_p_pert_at_accepted_y", label)
            p_name = f"p_pert_checked_s{n}"
            if p_meta and p_name in arrays and not top_lid:
                p_prime = _finite(p_name, arrays[p_name]).reshape(ny, nz, nx)
                mu_state = _state_block(y, "mu", off)
                top_terms = _source_dynamic_top_w_acceleration(
                    p_prime, mu_state, static_mub_padded, static["fields"]["C1F"],
                    static["fields"]["C2F"], static["fields"]["RDNW"],
                    float(_need(meta, "gravity_rhs_value", label)))
                capture["dynamic_top_source_derived_from_accepted_Y_p_pert"] = {
                    f"{term}_acceleration_m_s2": _row_stats(value[owner],
                        np.ones(int(owner.sum()), bool))
                    for term, value in top_terms.items()}
                capture["dynamic_top_closure_source"] = "same-point p_pert, MU, MUB, C1F/C2F/RDNW; cqw=0 from input"
                diagnostic_arrays[f"derived_top_pressure_accel_s{n}"] = top_terms["pressure"]
                diagnostic_arrays[f"derived_top_mu_buoyancy_accel_s{n}"] = top_terms["mu_buoyancy"]
                diagnostic_arrays[f"derived_top_moisture_buoyancy_accel_s{n}"] = top_terms["moisture_buoyancy"]
            elif not top_lid:
                capture["dynamic_top_source_derived_from_accepted_Y_p_pert"] = None
                capture["dynamic_top_derivation_unavailable"] = (
                    "same-Y p_pert array/flag unavailable")
                w_capture_complete = False
            if not pressure_at_y:
                w_capture_complete = False
                capture["accepted_y_attribution_unavailable"] = (
                    "same-call terms were not gated to accepted Y")
        else:
            w_capture_complete = False
            capture["reason"] = "same-call W pressure/conversion terms not captured"

        stages.append({"stage": n, "explicit_stage": explicit,
                       "postprocess_unchanged": postprocess_unchanged,
                       "postprocess_equality_observed": postprocess_equality_observed,
                       "postprocess_equality_status": postprocess_equality_status,
                       "retry_used": _truth(meta, f"s{n}_retry_used", label),
                       "retry_base_sanitized": _truth(meta, f"s{n}_retry_base_sanitized", label),
                       "base_differs_from_history": _truth(meta, f"s{n}_base_differs_from_history", label),
                       "rhs_reference_flags": reference_flags,
                       "tableau_a_ii": float(_need(meta, f"s{n}_tableau_a_ii", label)),
                       "effective_a_ii": float(_need(meta, f"s{n}_effective_a_ii", label)),
                       "stage_base_source_order_replay": {
                           "history_Bi_minus_source_order_replay": {
                               "w_owner_difference": _row_stats(
                                   _state_block(history_replay_delta, "w", off), w_owner)},
                           "accepted_Bi_minus_history_Bi": {
                               "w_owner_difference": _row_stats(
                                   _state_block(accepted_base_delta, "w", off), w_owner)}},
                       "stored_full_equals_fast_plus_slow": {
                           "w_owner_difference": _row_stats(_state_block(full_split, "w", off), w_owner)},
                       "G_residual": residual, "same_call_W_terms": capture})
        slow_w.append(_state_block(slow, "w", off))
        fast_w.append(_state_block(fast, "w", off))
        full_w.append(_state_block(full, "w", off))

    # Replay the accepted endpoint using source-order FP64 scalar multiply/add.
    de = np.zeros(total, dtype=np.float64)
    di = np.zeros(total, dtype=np.float64)
    replay = initial.copy()
    for i in range(4):
        de += dt * be[i] * stage_states["slow"][i]
        di += dt * bi[i] * stage_states["fast"][i]
        replay = replay + dt * be[i] * stage_states["full"][i]
    endpoint_delta = raw_final - initial
    raw_replay_diff = replay - raw_final
    pressure_result: dict[str, Any] = {
        "available": False,
        "reason": "--input not supplied; same-call measured pg work requires source static mass/grid fields",
    }
    qvapor = static["fields"].get("QVAPOR")
    dry_input = qvapor is not None and bool(np.all(qvapor == 0.0))
    if dry_input:
            # Input arrays are authoritative static WRF fields; stage MU is the
            # accepted Yi field. Work uses measured same-call PG/buoyancy terms.
            mub = static_mub_padded
            msftx, msfty = static_msftx_padded, static_msfty_padded
            c1h, c2h, dnw = static["fields"]["C1H"], static["fields"]["C2H"], static["fields"]["DNW"]
            gravity = float(_need(meta, "gravity_rhs_value", label))
            component_work: dict[str, dict[int, float]] = {
                "pressure": {}, "mu_buoyancy": {}, "moisture_buoyancy": {},
                "pressure_plus_buoyancy": {}}
            top_component_work: dict[str, dict[int, float]] = {
                "pressure": {}, "mu_buoyancy": {}, "moisture_buoyancy": {},
                "pressure_plus_buoyancy": {}}
            # Work terms are evaluated at each accepted Yi, using measured
            # accelerations from same-call pg and source buoyancy terms.
            for n, (y_w, wcap) in enumerate(zip(ys_w, stages), 1):
                if not wcap["same_call_W_terms"]["available_at_accepted_y"]:
                    continue
                fields = {term: np.asarray(arrays[f"{term}_s{n}"]).reshape(w_shape)
                          for term in ("pg", "buoy_mu1", "buoy_mu2", "velocity_mass_w")}
                den = fields["velocity_mass_w"]
                pg_acc = fields["pg"] / den
                mu_acc = -fields["buoy_mu1"] / den
                moist_acc = -fields["buoy_mu2"] / den
                top_only = {name: np.zeros(w_shape, dtype=np.float64)
                            for name in ("pressure", "mu_buoyancy", "moisture_buoyancy")}
                p_prime = _finite(f"p_pert_checked_s{n}",
                                  _need(arrays, f"p_pert_checked_s{n}", label)).reshape(ny, nz, nx)
                top_terms = _source_dynamic_top_w_acceleration(
                    p_prime, _state_block(state(f"y_s{n}"), "mu", off), mub,
                    static["fields"]["C1F"], static["fields"]["C2F"],
                    static["fields"]["RDNW"], gravity)
                for term, value in top_terms.items():
                    top_only[term][:, nz, :] = value
                # Captured C++ component tensors omit the separately added
                # source top closure. Keep their interior values and replace
                # only the dynamic top from accepted-Y p'/MU and native metrics.
                pg_acc[:, nz, :] = top_terms["pressure"]
                mu_acc[:, nz, :] = top_terms["mu_buoyancy"]
                moist_acc[:, nz, :] = top_terms["moisture_buoyancy"]
                # stage W velocity and MU are sampled from accepted Yi.
                w_state = y_w
                mu_state = _state_block(state(f"y_s{n}"), "mu", off)
                # Inline the source-reviewed helper math to keep this analysis
                # script portable and single-authoritative within the repo.
                work = _physical_w_work(
                    w_state, pg_acc, mu_acc, moist_acc, mu_state, mub, c1h, c2h,
                    dnw, msftx, msfty, static["DX"], static["DY"], gravity,
                    owner, top_lid)
                top_work = _physical_w_work(
                    w_state, top_only["pressure"], top_only["mu_buoyancy"],
                    top_only["moisture_buoyancy"], mu_state, mub, c1h, c2h,
                    dnw, msftx, msfty, static["DX"], static["DY"], gravity,
                    owner, top_lid)
                for term in component_work:
                    if term == "pressure_plus_buoyancy":
                        component_work[term][n] = (
                            work["pressure"] + work["mu_buoyancy"] + work["moisture_buoyancy"])
                        top_component_work[term][n] = (
                            top_work["pressure"] + top_work["mu_buoyancy"] +
                            top_work["moisture_buoyancy"])
                    else:
                        component_work[term][n] = work[term]
                        top_component_work[term][n] = top_work[term]
            pressure_result = {
                "available": w_capture_complete,
                "static_input_sha256": static["sha256"],
                "units": "physical domain power W, with source dry-column dual mass",
                "stage_power_w": component_work,
                "dynamic_top_source_derived_work_w": top_component_work,
                "eligible_stages": sorted(component_work["pressure"]),
                "note": "measured pg/buoyancy divided by same-call velocity_mass_w; pressure-coordinate proxy is not physical work"}
            if w_capture_complete:
                pressure_result["tableau_weighted_power_time_integral_j"] = {
                    term: float(sum(dt * bi[i] * component_work[term][i + 1]
                                    for i in range(4)))
                    for term in component_work}
                pressure_result["dynamic_top_tableau_weighted_work_integral_j"] = {
                    term: float(sum(dt * bi[i] * top_component_work[term][i + 1]
                                    for i in range(4)))
                    for term in component_work}
            else:
                eligible = component_work["pressure"].keys()
                pressure_result["partial_tableau_weighted_power_time_integral_j"] = {
                    term: float(sum(dt * bi[n - 1] * component_work[term][n]
                                    for n in eligible))
                    for term in component_work}
                pressure_result["partial_dynamic_top_tableau_work_integral_j"] = {
                    term: float(sum(dt * bi[n - 1] * top_component_work[term][n]
                                    for n in eligible))
                    for term in component_work}
                pressure_result["reason"] = "at least one stage lacks accepted-Y pressure capture; quadrature withheld"
    else:
        pressure_result["reason"] = (
            "same-call W term capture missing or not accepted-Y on at least one stage"
            if not w_capture_complete else
            "source dry-column work quadrature requires input QVAPOR to be exactly zero")

    owner_stats = {
        "accepted_rhs_increment_E_m_s": _row_stats(sum(dt * be[i] * slow_w[i]
                                                         for i in range(4)), w_owner),
        "accepted_rhs_increment_I_m_s": _row_stats(sum(dt * bi[i] * fast_w[i]
                                                         for i in range(4)), w_owner),
        "E_plus_I_endpoint_difference_m_s": _row_stats(
            sum(dt * be[i] * slow_w[i] + dt * bi[i] * fast_w[i] for i in range(4))
            - _state_block(endpoint_delta, "w", off), w_owner),
        "raw_endpoint_delta_m_s": _row_stats(_state_block(endpoint_delta, "w", off), w_owner),
        "projected_endpoint_delta_m_s": _row_stats(
            _state_block(projected_final - initial, "w", off), w_owner),
        "full_rhs_tableau_replay_minus_raw_endpoint_m_s": _row_stats(
            _state_block(raw_replay_diff, "w", off), w_owner),
        "units": "W state increments and endpoint deltas: m/s; RHS terms: m/s^2",
    }
    result: dict[str, Any] = {
        "schema": "accepted_ark_w_stage_analysis_v2",
        "input_trace_schema": schema,
        "scope": "one accepted step; solver defects and replay differences are diagnostics, not state-error bounds",
        "source_csv": str(csv_path), "source_csv_sha256": _sha256(csv_path),
        "analyzer_sha256": _sha256(Path(__file__)),
        "run_receipt": str(receipt_path) if receipt_path else None,
        "run_receipt_sha256": _sha256(receipt_path) if receipt_path else None,
        "input_path": str(input_path) if input_path else None,
        "input_sha256": input_record,
        "dimensions": {"nx": nx, "ny": ny, "nz": nz, "dt_seconds": dt,
                       "physical_owner_nx": physical_nx, "physical_owner_ny": physical_ny,
                       "physical_owner_dims_source": "trace metadata crosschecked to NetCDF MUB" if
                       "physical_owners_nx" in meta and "physical_owners_ny" in meta else
                       "NetCDF MUB dimensions (v1 trace lacks explicit owner dimensions)",
                       "W_owner_count": int(w_owner.sum()), "fixed_bottom_excluded": True,
                       "dynamic_top_included": not top_lid},
        "runtime": {k: meta[k] for k in (
                    "kdamp_config", "kdamp_grid", "gravity_grid", "gravity_fallback",
                    "gravity_rhs_value", "mass_coordinate_mode", "raw_wrf_omega_ww_cp",
                    "effective_wrf_omega_ww_cp", "omega_w_blend", "buoyancy_use_current_w",
                    "split_explicit_top_lid", "implicit_divergence", "wrf_w_damping",
                    "wrf_damp_opt", "diffusion_option", "khdif", "kvdif",
                    "newton_tol_config", "newton_rtol_config", "krylov_tol_config",
                    "effective_ewt_rtol_config", "gate_metric_mode", "stage_metric",
                    "stage_gate_rel_threshold", "stage3_gate_rel_threshold") if k in meta},
        "newton_tolerance_source_binding": text.get("newton_tol_source_binding"),
        "stages": stages,
        "accepted_update": owner_stats,
        "physical_pressure_work": pressure_result,
    }
    out_path.parent.mkdir(parents=True, exist_ok=True)
    diagnostic_arrays.update({
        "owner_w": w_owner, "fixed_bottom_w": fixed_bottom,
        "delta_w_E_m_s": sum(dt * be[i] * slow_w[i] for i in range(4)),
        "delta_w_I_m_s": sum(dt * bi[i] * fast_w[i] for i in range(4)),
        "delta_w_raw_m_s": _state_block(endpoint_delta, "w", off),
        "delta_w_projected_m_s": _state_block(projected_final - initial, "w", off),
        "delta_w_full_replay_m_s": _state_block(replay - initial, "w", off),
    })
    npz_path = out_path.with_suffix(".npz")
    np.savez_compressed(npz_path, **diagnostic_arrays)
    result["npz_path"] = str(npz_path)
    result["npz_sha256"] = _sha256(npz_path)
    out_path.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    return result


def _physical_w_work(w: np.ndarray, pressure_accel: np.ndarray, mu_accel: np.ndarray,
                     moisture_accel: np.ndarray, mu: np.ndarray, mub: np.ndarray,
                     c1h: np.ndarray, c2h: np.ndarray, dnw: np.ndarray,
                     msftx: np.ndarray, msfty: np.ndarray, dx: float, dy: float,
                     gravity: float, owner_xy: np.ndarray, top_lid: bool) -> dict[str, float]:
    """Source dry-column dual-mass work quadrature, kept here for portability.

    The reviewed reference implementation is preserved in the E307 evidence
    bundle at SHA256 1e516540a9320d60bbe081cf3466b00bd9228b0a478af7d257b89930035acb65.
    """
    layer_dp = -dnw[None, :, None] * (c1h[None, :, None] * (mub + mu)[:, None, :]
                                      + c2h[None, :, None])
    if np.any(~np.isfinite(layer_dp)) or np.any(layer_dp <= 0):
        raise ValueError("derived hybrid layer pressure thickness must be positive")
    layer_mass = layer_dp / gravity
    ny, nz, nx = layer_mass.shape
    dual_mass = np.zeros((ny, nz + 1, nx), dtype=np.float64)
    for k in range(1, nz):
        dual_mass[:, k, :] = 0.5 * (layer_mass[:, k - 1, :] + layer_mass[:, k, :])
    dual_mass[:, nz, :] = 0.5 * layer_mass[:, nz - 1, :]
    face_owner = np.broadcast_to(owner_xy[:, None, :], dual_mass.shape).copy()
    face_owner[:, 0, :] = False
    if top_lid:
        face_owner[:, nz, :] = False
    area = dx * dy / (msftx * msfty)
    def power(accel: np.ndarray) -> float:
        density = np.sum(np.where(face_owner, dual_mass * w * accel, 0.0), axis=1)
        return float(np.sum(density * area))
    return {"pressure": power(pressure_accel), "mu_buoyancy": power(mu_accel),
            "moisture_buoyancy": power(moisture_accel)}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage-csv", type=Path, action="append", required=True,
                        help="one CSV containing four stage records (repeat is rejected)")
    parser.add_argument("--run-receipt", type=Path)
    parser.add_argument("--input", type=Path, required=True,
                        help="pinned WRF input NetCDF for physical owners and work quadrature")
    parser.add_argument("--out", type=Path, required=True, help="JSON output; NPZ is written beside it")
    args = parser.parse_args()
    result = analyze(args.stage_csv, args.out, args.input, args.run_receipt)
    print(json.dumps({"out": str(args.out), "npz": str(args.out.with_suffix('.npz')),
                      "stages": len(result["stages"]),
                      "w_owners": result["dimensions"]["W_owner_count"],
                      "pressure_work_available": result["physical_pressure_work"]["available"]},
                     sort_keys=True))


if __name__ == "__main__":
    main()
