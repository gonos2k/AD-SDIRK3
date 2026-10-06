#!/usr/bin/env python3
"""Compare native wave quadratic responses with the independent source reference.

This is an integration runner for the ``test_native_wave_refinement`` fixture.
It uses the native imported background, serializes source-mode directions in the
native packed layout, and compares projected m=2 responses against the
source-transcribed coefficient and its DOP853 propagation.  It makes no claim
about a general tile Hessian.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import platform
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
import scipy


ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))
import test_native_wave_refinement as native  # noqa: E402
import wave_quadratic_reference as reference  # noqa: E402


GRIDS = ((8, 6, 4), (16, 12, 8))
TIMES = (10.0, 20.0, 30.0)
CONTROL_DIRECTION = np.array([0.15, -0.12, 0.09, 0.17], dtype=np.float64)
ACTIVE_RELATIVE_BUDGET = 1.0e-3
ADJOINT_RELATIVE_BUDGET = 2.0e-3


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def packed_offsets(nx: int, ny: int, nz: int) -> tuple[int, ...]:
    sizes = (ny*nz*(nx+1), (ny+1)*nz*nx, ny*(nz+1)*nx,
             ny*(nz+1)*nx, ny*nz*nx, ny*nx)
    return tuple(int(x) for x in np.r_[0, np.cumsum(sizes[:-1])])


def pack_mode(q: np.ndarray, nx: int, ny: int, nz: int,
              offsets: tuple[int, ...], lx: float = 40000.0) -> np.ndarray:
    """Encode source order [U,W,PH,THETA,MU] into native packed state order."""
    q = np.asarray(q, dtype=np.complex128)
    if q.shape != (4*nz+1,):
        raise ValueError(f"source mode has shape {q.shape}, expected {(4*nz+1,)}")
    result = np.zeros(offsets[-1] + ny*nx, dtype=np.float64)
    o_u, o_v, o_w, o_ph, o_t, o_mu = offsets
    center_phase = np.exp(2j*np.pi*(np.arange(nx)+0.5)/nx)
    face_phase = np.exp(2j*np.pi*np.arange(nx)/nx)
    u = result[o_u:o_v].reshape(ny, nz, nx+1)
    w = result[o_w:o_ph].reshape(ny, nz+1, nx)
    ph = result[o_ph:o_t].reshape(ny, nz+1, nx)
    theta = result[o_t:o_mu].reshape(ny, nz, nx)
    mu = result[o_mu:].reshape(ny, nx)
    for k in range(nz):
        u[:, k, :nx] = np.real(q[k]*face_phase)[None, :]
        # Periodic endpoint is a duplicate of face zero; retain it for the
        # native packed contract while excluding it from modal projection.
        u[:, k, nx] = u[:, k, 0]
        w[:, k+1, :] = np.real(q[nz+k]*center_phase)[None, :]
        ph[:, k+1, :] = np.real(q[2*nz+k]*center_phase)[None, :]
        theta[:, k, :] = np.real(q[3*nz+k]*center_phase)[None, :]
    mu[:, :] = np.real(q[-1]*center_phase)[None, :]
    return result


def mode_pair_controls(c: dict, controls: np.ndarray) -> np.ndarray:
    modes = [reference.mode_pair(c, i) for i in range(2)]
    return sum((controls[2*i]*modes[i][1] + controls[2*i+1]*modes[i][2]
                for i in range(2)), np.zeros(4*int(c["nz"])+1, dtype=np.complex128))


def project_m2(z: np.ndarray, nx: int, ny: int, nz: int,
               offsets: tuple[int, ...], lx: float = 40000.0) -> np.ndarray:
    """Project m=2 at every y/z location, excluding the duplicate U endpoint."""
    if not np.isfinite(z).all():
        raise AssertionError("native payload contains NaN or Inf")
    o_u, o_v, o_w, o_ph, o_t, o_mu = offsets
    u = z[o_u:o_v].reshape(ny, nz, nx+1)[:, :, :nx]
    w = z[o_w:o_ph].reshape(ny, nz+1, nx)[:, 1:nz+1]
    ph = z[o_ph:o_t].reshape(ny, nz+1, nx)[:, 1:nz+1]
    theta = z[o_t:o_mu].reshape(ny, nz, nx)
    mu = z[o_mu:].reshape(ny, nx)
    wave = 4.0*np.pi/lx
    xu = np.arange(nx, dtype=np.float64)*(lx/nx)
    xc = (np.arange(nx, dtype=np.float64)+0.5)*(lx/nx)
    def project(a: np.ndarray, x: np.ndarray) -> np.ndarray:
        return 2.0*np.mean(a*np.exp(-1j*wave*x), axis=-1)
    return np.concatenate((project(u, xu).ravel(), project(w, xc).ravel(),
                           project(ph, xc).ravel(), project(theta, xc).ravel(),
                           project(mu, xc).ravel()))


def project_v_m2(z: np.ndarray, nx: int, ny: int, nz: int,
                 offsets: tuple[int, ...], lx: float = 40000.0) -> np.ndarray:
    """Project all meridional V faces; the uniform-y x-z fixture predicts zero."""
    o_v = offsets[1]
    v = z[o_v:o_v+(ny+1)*nz*nx].reshape(ny+1, nz, nx)
    phase = np.exp(-4j*np.pi*(np.arange(nx, dtype=np.float64)+0.5)/nx)
    return (2.0*np.mean(v*phase, axis=-1)).ravel()


def tile_source_mode(q: np.ndarray, ny: int, nz: int) -> np.ndarray:
    """Repeat the uniform-y source coefficient at every corresponding cell."""
    return np.concatenate((np.tile(q[:nz], ny), np.tile(q[nz:2*nz], ny),
                           np.tile(q[2*nz:3*nz], ny), np.tile(q[3*nz:4*nz], ny),
                           np.full(ny, q[-1], dtype=np.complex128)))


def complex_pairs(values: list[list[float]]) -> np.ndarray:
    return np.asarray([complex(float(real), float(imag)) for real, imag in values],
                      dtype=np.complex128)


def centered_coefficient(plus: np.ndarray, minus: np.ndarray, base: np.ndarray,
                         amplitude: float) -> np.ndarray:
    if amplitude <= 0:
        raise ValueError("probe amplitude must be positive")
    return (plus + minus - 2.0*base)/(2.0*amplitude*amplitude)


def branch_metadata(meta: dict[str, float], label: str) -> dict[str, float]:
    required = ("kdamp_config", "implicit_divergence", "sign_smooth_delta_config",
                "omega_w_blend_config", "do_curvature_config",
                "effective_wrf_omega_ww_cp", "advection_order_config",
                "non_hydrostatic_config", "map_input_max_deviation")
    missing = [key for key in required if key not in meta]
    if missing:
        raise AssertionError(f"{label}: native payload omits branch metadata: {missing}")
    values = {key: float(meta[key]) for key in required}
    expected = {"kdamp_config": 0.0, "implicit_divergence": 0.0,
                "omega_w_blend_config": 1.0, "do_curvature_config": 1.0,
                "effective_wrf_omega_ww_cp": 1.0, "advection_order_config": 2.0,
                "non_hydrostatic_config": 1.0, "map_input_max_deviation": 0.0}
    failures = {key: (values[key], target) for key, target in expected.items()
                if values[key] != target}
    if values["sign_smooth_delta_config"] <= 0.0:
        failures["sign_smooth_delta_config"] = (values["sign_smooth_delta_config"], "positive")
    if failures:
        raise AssertionError(f"{label}: native fixture branch metadata mismatch: {failures}")
    return values


def richardson_quadratic(payload: dict[str, np.ndarray], project,
                         prefix: str = "rhs") -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Return wide/narrow Richardson estimates and their signed side defect."""
    base_key = f"{prefix}_center" if prefix in {"rhs_E", "rhs_I"} else f"{prefix}_base"
    base = project(payload[base_key])
    centered = []
    for level, amp in enumerate((1.0, 0.5, 0.25)):
        centered.append(centered_coefficient(project(payload[f"{prefix}_plus_{level}"]),
                                             project(payload[f"{prefix}_minus_{level}"]),
                                             base, amp))
    wide = (4.0*centered[1]-centered[0])/3.0
    narrow = (4.0*centered[2]-centered[1])/3.0
    return wide, narrow, wide-narrow


def run_native(exe: Path, nx: int, ny: int, nz: int, out: Path,
               extra: tuple[str, ...] = ()) -> tuple[dict, dict, dict, dict]:
    command = [str(exe), str(nx), str(ny), str(nz), str(out), *extra]
    done = subprocess.run(command, capture_output=True, text=True, check=False)
    if done.returncode:
        tail = "\n".join((done.stdout+done.stderr).splitlines()[-80:])
        raise RuntimeError(f"native command failed ({done.returncode}): {' '.join(command)}\n{tail}")
    parsed = native.read_payload(out)
    meta = parsed[0]
    if (int(meta.get("nx", -1)), int(meta.get("ny", -1)), int(meta.get("nz", -1))) != (nx, ny, nz):
        raise AssertionError(f"native payload grid mismatch for {nx}x{ny}x{nz}")
    branch_metadata(meta, f"{out.name}")
    return parsed


def objective_value(scalars: dict[str, float], arrays: dict[str, np.ndarray]) -> float:
    """Read the m=2 objective field, with payload projection as a compatibility fallback."""
    if "objective_w_m2_projection" in scalars:
        return float(scalars["objective_w_m2_projection"])
    if "final" in arrays and "terminal_cotangent" in arrays:
        return float(np.dot(arrays["final"], arrays["terminal_cotangent"]))
    raise AssertionError("native trajectory payload has no terminal W projection objective")


def write_direction(path: Path, direction: np.ndarray) -> None:
    with path.open("w") as stream:
        stream.write(f"{direction.size}\n")
        stream.write(" ".join(format(float(v), ".17g") for v in direction))
        stream.write("\n")


def source_direction(c: dict, nx: int, ny: int, nz: int,
                     offsets: tuple[int, ...], controls: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    q = mode_pair_controls(c, controls)
    return q, pack_mode(q, nx, ny, nz, offsets)


def component_layout(ny: int, nz: int) -> tuple[tuple[str, int], ...]:
    return (("U", ny*nz), ("W", ny*nz), ("PH", ny*nz),
            ("THETA", ny*nz), ("MU", ny))


def eos_pressure_floors(c: dict, nx: int, ny: int, nz: int,
                        duration: float = 1.0) -> np.ndarray:
    """Per-cell FP64 floor mapped from Pa roundoff through source EOS scales."""
    eps = np.finfo(np.float64).eps
    pa_floor = 128.0*eps*float(np.max(np.abs(c["p_bar"])))
    k2 = 4.0*np.pi/float(c["lx"])
    alpha = float(np.max(np.abs(c["alpha_bar"])))
    mbar = float(np.sum(c["eta_delta"]*c["layer_mass"]))
    freq = float(np.max(np.abs(np.linalg.eigvals(c["A"]))))
    floors = {
        "U": pa_floor*alpha*k2*duration,
        "W": pa_floor*float(c["g"])*float(np.max(np.abs(c["rdnw"])))/mbar*duration,
        "PH": pa_floor*alpha*freq*duration,
        "THETA": pa_floor*float(np.max(np.abs(c["theta"]))) /
                 float(np.min(np.abs(c["p_bar"]))) * freq*duration,
        "MU": pa_floor/float(c["g"])*freq*duration,
    }
    return np.concatenate([np.full(count, floors[name], dtype=np.float64)
                           for name, count in component_layout(ny, nz)])


def componentwise_check(actual: np.ndarray, expected: np.ndarray, floors: np.ndarray,
                        ny: int, nz: int, relative_budget: float,
                        label: str) -> dict:
    if actual.shape != expected.shape or actual.shape != floors.shape:
        raise ValueError(f"{label}: incompatible projected block sizes")
    results = {}
    cursor = 0
    for name, count in component_layout(ny, nz):
        sl = slice(cursor, cursor+count)
        delta = actual[sl]-expected[sl]
        absolute = float(np.linalg.norm(delta))
        signal = float(np.linalg.norm(expected[sl]))
        floor_norm = float(np.linalg.norm(floors[sl]))
        budget = max(relative_budget*signal, floor_norm)
        if not np.isfinite(absolute) or absolute > budget:
            raise AssertionError(f"{label} {name} error {absolute:.6g} > mixed budget {budget:.6g} "
                                 f"(signal {signal:.6g}, EOS/Pa floor {floor_norm:.6g})")
        results[name] = {"absolute_error": absolute, "signal_norm": signal,
                         "eos_pressure_floor_norm": floor_norm,
                         "accepted_error_budget": budget,
                         "relative_error": absolute/max(signal, floor_norm)}
        cursor += count
    return results


def zero_mode_check(actual: np.ndarray, per_cell_floor: float, label: str) -> dict:
    absolute = float(np.linalg.norm(actual))
    floor_norm = per_cell_floor*np.sqrt(actual.size)
    if not np.isfinite(absolute) or absolute > floor_norm:
        raise AssertionError(f"{label} m=2 response {absolute:.6g} exceeds zero EOS/Pa floor {floor_norm:.6g}")
    return {"absolute_norm": absolute, "eos_pressure_floor_norm": floor_norm,
            "max_abs_complex": float(np.max(np.abs(actual))) if actual.size else 0.0}


def read_grid_descriptor(exe: Path, outdir: Path, nx: int, ny: int, nz: int) -> dict:
    csv_path = outdir/f"descriptor_{nx}x{ny}x{nz}.csv"
    meta, arrays, scalars, text = run_native(exe, nx, ny, nz, csv_path,
                                             ("--descriptor-only",))
    phb = arrays["phb"].reshape(ny, nz+1, nx)
    phb_faces = phb[0, :, 0].copy()
    if not np.array_equal(phb, np.broadcast_to(phb_faces[None, :, None], phb.shape)):
        raise AssertionError(f"{nx}x{ny}x{nz}: imported PHB is not a uniform column")
    c = native.source_column(nz, nx, phb_faces)
    reference._apply_native_base(c, csv_path)
    branch = branch_metadata(meta, f"descriptor_{nx}x{ny}x{nz}")
    if (int(meta["nx"]), int(meta["ny"]), int(meta["nz"])) != (nx, ny, nz):
        raise AssertionError("native descriptor grid does not match requested grid")
    return {"column": c, "meta": meta, "arrays": arrays, "scalars": scalars,
            "text": text, "branch_config": branch,
            "phb_sha256": hashlib.sha256(phb_faces.tobytes()).hexdigest()}


def run_probe(exe: Path, outdir: Path, grid: tuple[int, int, int], d: dict,
              q: np.ndarray, check_halfshift: bool = True) -> dict:
    outdir.mkdir(parents=True, exist_ok=True)
    nx, ny, nz = grid
    offsets = packed_offsets(nx, ny, nz)
    direction = pack_mode(q, nx, ny, nz, offsets)
    direction_path = outdir/f"direction_{nx}x{ny}x{nz}.txt"
    write_direction(direction_path, direction)
    path = outdir/f"probe_{nx}x{ny}x{nz}.csv"
    meta, arrays, scalars, text = run_native(exe, nx, ny, nz, path,
                                              ("--quadratic-probe", str(direction_path)))
    branch = branch_metadata(meta, f"probe_{nx}x{ny}x{nz}")
    projector = lambda v: project_m2(v, nx, ny, nz, offsets)
    v_projector = lambda v: project_v_m2(v, nx, ny, nz, offsets)
    expected_channels = reference.quadratic_forcing_channels(d["column"], q)
    expected = expected_channels["total"]
    expected_projected = tile_source_mode(expected, ny, nz)
    wide, narrow, scatter = richardson_quadratic(arrays, projector)
    floors = eos_pressure_floors(d["column"], nx, ny, nz)
    coefficient_errors = componentwise_check(wide, expected_projected, floors, ny, nz,
                                             ACTIVE_RELATIVE_BUDGET, "instantaneous source comparison")
    scatter_errors = componentwise_check(wide, narrow, floors, ny, nz,
                                         ACTIVE_RELATIVE_BUDGET, "amplitude Richardson scatter")
    v_base = v_projector(arrays["rhs_base"])
    v_centered = []
    for level, amplitude in enumerate((1.0, 0.5, 0.25)):
        v_centered.append(centered_coefficient(v_projector(arrays[f"rhs_plus_{level}"]),
                                               v_projector(arrays[f"rhs_minus_{level}"]),
                                               v_base, amplitude))
    v_wide = (4.0*v_centered[1]-v_centered[0])/3.0
    v_narrow = (4.0*v_centered[2]-v_centered[1])/3.0
    v_floor = float(np.max(floors[:ny*nz]))
    v_zero_check = zero_mode_check(v_wide, v_floor, f"{nx}x{ny}x{nz} probe V")
    v_scatter_check = zero_mode_check(v_wide-v_narrow, v_floor,
                                      f"{nx}x{ny}x{nz} V amplitude scatter")
    error_abs = float(np.linalg.norm(wide-expected_projected))
    error_rel = error_abs/max(float(np.linalg.norm(expected_projected)), float(np.linalg.norm(floors)))
    scatter_abs = float(np.linalg.norm(scatter))
    parity_abs = None
    parity_errors = {}
    if check_halfshift:
        centered_half_shift = -direction
        shift_path = outdir/f"direction_halfshift_{nx}x{ny}x{nz}.txt"
        write_direction(shift_path, centered_half_shift)
        shifted_path = outdir/f"probe_halfshift_{nx}x{ny}x{nz}.csv"
        _, shifted_arrays, _, _ = run_native(exe, nx, ny, nz, shifted_path,
                                              ("--quadratic-probe", str(shift_path)))
        shifted, _, shifted_scatter = richardson_quadratic(shifted_arrays, projector)
        shifted_vbase = v_projector(shifted_arrays["rhs_base"])
        shifted_vcentered = []
        for level, amplitude in enumerate((1.0, 0.5, 0.25)):
            shifted_vcentered.append(centered_coefficient(
                v_projector(shifted_arrays[f"rhs_plus_{level}"]),
                v_projector(shifted_arrays[f"rhs_minus_{level}"]), shifted_vbase, amplitude))
        shifted_vwide = (4.0*shifted_vcentered[1]-shifted_vcentered[0])/3.0
        parity_abs = float(np.linalg.norm(shifted-wide))
        parity_errors = componentwise_check(shifted, wide, floors, ny, nz,
                                            ACTIVE_RELATIVE_BUDGET, "half-domain phase parity")
        v_zero_check["halfshift_parity"] = zero_mode_check(
            shifted_vwide-v_wide, v_floor, f"{nx}x{ny}x{nz} V halfshift parity")
    # Signed complex errors retain phase and sign information per state component.
    cursor = 0
    component_errors = {}
    for label, count in component_layout(ny, nz):
        sl = slice(cursor, cursor+count)
        delta = wide[sl]-expected_projected[sl]
        component_errors[label] = {
            **coefficient_errors[label],
            "richardson_scatter": scatter_errors[label],
            "halfshift_parity": parity_errors.get(label),
            "max_abs_complex": float(np.max(np.abs(delta))),
            "signed_real": delta.real.tolist(), "signed_imag": delta.imag.tolist(),
            "native_real": wide[sl].real.tolist(), "native_imag": wide[sl].imag.tolist(),
            "source_real": expected_projected[sl].real.tolist(),
            "source_imag": expected_projected[sl].imag.tolist(),
        }
        cursor += count
    component_errors["V"] = {"source_expected_zero": True,
                              "eos_pressure_floor_per_cell": v_floor,
                              **v_zero_check, "richardson_scatter": v_scatter_check}
    split_channels = {}
    for label in ("E", "I"):
        coarse = projector(arrays[f"quadratic_{label}_positive"])
        fine = projector(arrays[f"quadratic_{label}_positive_small"])
        split_channels[label] = (4.0*fine-coarse)/3.0
    return {"grid": list(grid), "direction_norm": float(np.linalg.norm(direction)),
            "reference_channels_norm": {k: float(np.linalg.norm(v)) for k,v in expected_channels.items()},
            "native_quadratic_wide": [[float(x.real), float(x.imag)] for x in wide],
            "native_quadratic_narrow": [[float(x.real), float(x.imag)] for x in narrow],
            "reference_quadratic": [[float(x.real), float(x.imag)] for x in expected],
            "error_abs": error_abs, "error_scaled": error_rel,
            "eos_pa_roundoff_floors": floors.tolist(),
            "richardson_scatter_abs": scatter_abs,
            "halfshift_parity_abs": parity_abs,
            "halfshift_scatter_abs": (float(np.linalg.norm(shifted_scatter))
                                      if check_halfshift else None),
            "signed_component_errors": component_errors,
            "branch_config": branch,
            "probe_amplitudes": [float(meta[f"probe_amplitude_{i}"]) for i in range(3)],
            "cpp_channels": {name: [[float(x.real), float(x.imag)] for x in values]
                             for name, values in split_channels.items()}}


def sample_source_path(c: dict, initial: np.ndarray, seconds: float = 30.0) -> dict:
    prop = reference.propagate_forcing(c, initial, seconds, samples=301)
    tightening_relative = float(prop["tightening_relative"])
    if not np.isfinite(tightening_relative) or tightening_relative > 1.0e-6:
        raise AssertionError(f"DOP853 source integration tightening {tightening_relative:.6g} > 1e-6")
    return {"states": {t: prop["states"][int(round(t/seconds*300))] for t in TIMES},
            "integration": {"method": "DOP853", "tightening_absolute": float(prop["tightening_abs"]),
                            "tightening_relative": tightening_relative,
                            "tightening_relative_budget": 1.0e-6}}


def run_trajectory(exe: Path, outdir: Path, grid: tuple[int, int, int], d: dict,
                   q: np.ndarray) -> dict:
    nx, ny, nz = grid
    offsets = packed_offsets(nx, ny, nz)
    packed = pack_mode(q, nx, ny, nz, offsets)
    zero_path = outdir/f"zero_{nx}x{ny}x{nz}.txt"
    write_direction(zero_path, np.zeros_like(packed))
    source = sample_source_path(d["column"], q)
    projector = lambda v: project_m2(v, nx, ny, nz, offsets)
    v_projector = lambda v: project_v_m2(v, nx, ny, nz, offsets)
    floors_at_time = {t: eos_pressure_floors(d["column"], nx, ny, nz, duration=t)
                      for t in TIMES}
    floors_per_second = eos_pressure_floors(d["column"], nx, ny, nz)
    coefficients: dict[tuple[float, float, float], np.ndarray] = {}
    trajectory_v_checks: dict[str, dict] = {}
    run_receipts = []
    for dt in (1.0, 0.5):
        steps = int(round(30.0/dt))
        checkpoint_indices = {t: int(round(t/dt))-1 for t in TIMES}
        base_path = outdir/f"trajectory_base_{nx}_dt{dt:g}.csv"
        base_meta, base_arrays, _, _ = run_native(exe, nx, ny, nz, base_path,
            ("--quadratic-forward", str(zero_path), str(steps), str(dt)))
        run_receipts.append({"dt": dt, "amplitude": 0.0, "sign": 0,
                             "steps": int(base_meta["trajectory_steps"]),
                             "payload": base_path.name})
        signed_states_by_amp = {}
        for amp in (1.0, 0.5):
            signed_states = {}
            for sign in (-1.0, 1.0):
                direction_path = outdir/f"trajectory_direction_{nx}_dt{dt:g}_{amp:g}_{sign:+g}.txt"
                write_direction(direction_path, sign*amp*packed)
                path = outdir/f"trajectory_{nx}_dt{dt:g}_{amp:g}_{sign:+g}.csv"
                meta, arrays, _, _ = run_native(exe, nx, ny, nz, path,
                    ("--quadratic-forward", str(direction_path), str(steps), str(dt)))
                if int(meta["trajectory_steps"]) != steps:
                    raise AssertionError("native trajectory retained an unexpected number of steps")
                signed_states[sign] = arrays
                run_receipts.append({"dt": dt, "amplitude": amp, "sign": int(sign),
                                     "steps": steps, "payload": path.name})
            signed_states_by_amp[amp] = signed_states
            for t, index in checkpoint_indices.items():
                name = f"checkpoint_{index}"
                coefficients[(dt, amp, t)] = centered_coefficient(
                    projector(signed_states[1.0][name]),
                    projector(signed_states[-1.0][name]),
                    projector(base_arrays[name]), amp)
        for index in range(steps):
            name = f"checkpoint_{index}"
            elapsed = (index+1)*dt
            v_floor = float(np.max(floors_per_second[:ny*nz]))*elapsed
            v_receipt = {"amplitude_1_sign_-1": zero_mode_check(
                v_projector(signed_states_by_amp[1.0][-1.0][name]), v_floor,
                f"{nx}x{ny}x{nz} dt={dt:g} t={elapsed:g} V a=1 -"),
                "amplitude_1_sign_+1": zero_mode_check(
                v_projector(signed_states_by_amp[1.0][1.0][name]), v_floor,
                f"{nx}x{ny}x{nz} dt={dt:g} t={elapsed:g} V a=1 +"),
                "amplitude_half_sign_-1": zero_mode_check(
                v_projector(signed_states_by_amp[0.5][-1.0][name]), v_floor,
                f"{nx}x{ny}x{nz} dt={dt:g} t={elapsed:g} V a=0.5 -"),
                "amplitude_half_sign_+1": zero_mode_check(
                v_projector(signed_states_by_amp[0.5][1.0][name]), v_floor,
                f"{nx}x{ny}x{nz} dt={dt:g} t={elapsed:g} V a=0.5 +"),
                "base": zero_mode_check(v_projector(base_arrays[name]), v_floor,
                                         f"{nx}x{ny}x{nz} dt={dt:g} t={elapsed:g} V background")}
            trajectory_v_checks[f"dt{dt:g}_checkpoint_{index}"] = v_receipt
    times = {}
    for t in TIMES:
        # First Richardson extrapolate in perturbation amplitude, then use the
        # SDIRK3 dt^3 ratio to estimate the fixed-step temporal limit.
        amp_richardson = {}
        amp_uncertainty = {}
        for dt in (1.0, 0.5):
            large = coefficients[(dt, 1.0, t)]
            half = coefficients[(dt, 0.5, t)]
            amp_richardson[dt] = (4.0*half-large)/3.0
            amp_uncertainty[dt] = np.abs(large-half)/3.0
        time_extrapolated = (8.0*amp_richardson[0.5]-amp_richardson[1.0])/7.0
        time_uncertainty = np.abs(amp_richardson[1.0]-amp_richardson[0.5])/7.0
        amp_uncertainty_vector = np.maximum(amp_uncertainty[1.0], amp_uncertainty[0.5])
        src = tile_source_mode(source["states"][t], ny, nz)
        floors = floors_at_time[t]
        errors = {}
        cursor = 0
        for name, count in component_layout(ny, nz):
            sl = slice(cursor, cursor+count)
            error_norm = float(np.linalg.norm(time_extrapolated[sl]-src[sl]))
            signal_norm = float(np.linalg.norm(src[sl]))
            temporal_norm = float(np.linalg.norm(time_uncertainty[sl]))
            amplitude_norm = float(np.linalg.norm(amp_uncertainty_vector[sl]))
            floor_norm = float(np.linalg.norm(floors[sl]))
            # The observed dt and amplitude scatters are explicit truncation
            # allowances; the final term is the EOS-conditioned Pa floor.
            acceptance_budget = (ACTIVE_RELATIVE_BUDGET*signal_norm +
                                 2.0*temporal_norm + 2.0*amplitude_norm + floor_norm)
            if not np.isfinite(error_norm) or error_norm > acceptance_budget:
                raise AssertionError(f"trajectory {t:g}s {name} source error {error_norm:.6g} > "
                                     f"signal+dt+amplitude+EOS budget {acceptance_budget:.6g}")
            errors[name] = {"absolute_error": error_norm, "source_signal_norm": signal_norm,
                            "temporal_uncertainty_norm": temporal_norm,
                            "amplitude_uncertainty_norm": amplitude_norm,
                            "eos_pressure_floor_norm": floor_norm,
                            "accepted_error_budget": acceptance_budget}
            cursor += count
        times[str(int(t))] = {
            "native_amplitude_1_dt1": [[float(x.real), float(x.imag)]
                                       for x in coefficients[(1.0, 1.0, t)]],
            "native_amplitude_half_dt1": [[float(x.real), float(x.imag)]
                                           for x in coefficients[(1.0, 0.5, t)]],
            "native_amplitude_1_dt_half": [[float(x.real), float(x.imag)]
                                            for x in coefficients[(0.5, 1.0, t)]],
            "native_amplitude_half_dt_half": [[float(x.real), float(x.imag)]
                                               for x in coefficients[(0.5, 0.5, t)]],
            "amplitude_richardson_dt1": [[float(x.real), float(x.imag)]
                                          for x in amp_richardson[1.0]],
            "amplitude_richardson_dt_half": [[float(x.real), float(x.imag)]
                                              for x in amp_richardson[0.5]],
            "temporal_richardson": [[float(x.real), float(x.imag)]
                                    for x in time_extrapolated],
            "source": [[float(x.real), float(x.imag)] for x in src],
            "amplitude_scatter_norm": float(np.linalg.norm(amp_uncertainty_vector)),
            "temporal_scatter_norm": float(np.linalg.norm(time_uncertainty)),
            "source_tightening_to_native_scatter": (
                source["integration"]["tightening_absolute"] /
                max(float(np.linalg.norm(time_uncertainty)),
                    float(np.linalg.norm(amp_uncertainty_vector)),
                    float(np.linalg.norm(floors)), np.finfo(float).tiny)
                if t == TIMES[-1] else None),
            "component_acceptance": errors,
        }
    return {"grid": list(grid), "dt_values": [1.0, 0.5], "physical_duration": 30.0,
            "temporal_order_assumed": 3, "runs": run_receipts,
            "V_zero_projection_checks": trajectory_v_checks,
            "source_integration": source["integration"], "times": times}


def run_adjoint_check(exe: Path, outdir: Path, d: dict,
                      controls: np.ndarray) -> dict:
    nx, ny, nz = GRIDS[0]
    offsets = packed_offsets(nx, ny, nz)
    q0, packed0 = source_direction(d["column"], nx, ny, nz, offsets, controls)
    dq, packed_delta = source_direction(d["column"], nx, ny, nz, offsets,
                                        CONTROL_DIRECTION)
    dt, steps = 1.0, 6
    path0 = outdir/"adjoint_center.csv"
    direction0 = outdir/"adjoint_center.txt"
    write_direction(direction0, packed0)
    _, arrays0, scalars0, _ = run_native(exe, nx, ny, nz, path0,
        ("--quadratic-trajectory", str(direction0), str(steps), str(dt)))
    pullback = arrays0["initial_pullback"]
    terminal_mode = project_m2(arrays0["terminal_cotangent"], nx, ny, nz, offsets)
    w_start, w_end = ny*nz, 2*ny*nz
    terminal_w_norm = float(np.linalg.norm(terminal_mode[w_start:w_end]))
    terminal_other_norm = float(np.linalg.norm(np.r_[terminal_mode[:w_start], terminal_mode[w_end:]]))
    if terminal_w_norm < 1.0e-6 or terminal_other_norm > 1.0e-10*terminal_w_norm:
        raise AssertionError("native terminal cotangent is not an m=2 W-only projection")
    predicted = float(np.dot(pullback, packed_delta))
    objective_pairs = {}
    fd_values = {}
    for fd_eps in (0.01, 0.005):
        objectives = []
        for sign in (-1.0, 1.0):
            q = mode_pair_controls(d["column"], controls+sign*fd_eps*CONTROL_DIRECTION)
            direction = pack_mode(q, nx, ny, nz, offsets)
            path = outdir/f"adjoint_fd_eps{fd_eps:g}_{sign:+g}.csv"
            direction_path = outdir/f"adjoint_fd_eps{fd_eps:g}_{sign:+g}.txt"
            write_direction(direction_path, direction)
            _, arrays, scalars, _ = run_native(exe, nx, ny, nz, path,
                ("--quadratic-forward", str(direction_path), str(steps), str(dt)))
            objectives.append(objective_value(scalars, arrays))
        objective_pairs[str(fd_eps)] = objectives
        fd_values[fd_eps] = (objectives[1]-objectives[0])/(2.0*fd_eps)
    fd = fd_values[0.005]
    round_floor = (128.0*np.finfo(float).eps*
                   max(*(abs(x) for pair in objective_pairs.values() for x in pair),
                       abs(objective_value(scalars0, arrays0)), np.finfo(float).tiny)/0.005)
    if abs(fd) <= round_floor:
        raise AssertionError(f"m=2 adjoint FD signal {abs(fd):.6g} is at roundoff floor {round_floor:.6g}")
    fd_relative = abs(fd-predicted)/max(abs(fd), abs(predicted), round_floor)
    eps_relative = abs(fd_values[0.01]-fd_values[0.005])/max(abs(fd), round_floor)
    if not np.isfinite(fd_relative) or fd_relative > ADJOINT_RELATIVE_BUDGET:
        raise AssertionError(f"trajectory adjoint directional error {fd_relative:.6g} > "
                             f"{ADJOINT_RELATIVE_BUDGET}")
    if not np.isfinite(eps_relative) or eps_relative > ADJOINT_RELATIVE_BUDGET:
        raise AssertionError(f"adjoint finite-difference epsilon scatter {eps_relative:.6g} > "
                             f"{ADJOINT_RELATIVE_BUDGET}")
    return {"steps": steps, "dt": dt, "seconds": steps*dt,
            "controls": controls.tolist(), "control_direction": CONTROL_DIRECTION.tolist(),
            "finite_difference_epsilons": [0.01, 0.005],
            "objectives_by_epsilon_minus_plus": objective_pairs,
            "finite_difference_directional_derivatives": {
                str(k): v for k, v in fd_values.items()},
            "finite_difference_directional_derivative": fd,
            "adjoint_directional_derivative": predicted,
            "absolute_error": abs(fd-predicted), "relative_error": fd_relative,
            "epsilon_scatter_relative": eps_relative, "budget": ADJOINT_RELATIVE_BUDGET,
            "finite_difference_roundoff_floor": round_floor,
            "objective_center": objective_value(scalars0, arrays0),
            "terminal_m2_w_norm": terminal_w_norm,
            "terminal_other_blocks_norm": terminal_other_norm,
            "source_direction_norm": float(np.linalg.norm(q0)),
            "control_state_direction_norm": float(np.linalg.norm(packed_delta))}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path,
                        help="built test_native_wave_refinement executable")
    parser.add_argument("--output-dir", type=Path,
                        help="retain native CSVs and machine-readable results here")
    parser.add_argument("--skip-trajectories", action="store_true",
                        help="omit the 30-second source/native trajectory comparisons; the separate adjoint check still runs")
    parser.add_argument("--skip-adjoint", action="store_true",
                        help="omit the short 6-second trajectory adjoint directional check")
    args = parser.parse_args()
    exe = args.executable.resolve()
    if not exe.is_file():
        raise SystemExit(f"native executable does not exist: {exe}")
    owned_tmp = None
    if args.output_dir:
        outdir = args.output_dir.resolve()
        outdir.mkdir(parents=True, exist_ok=True)
    else:
        owned_tmp = tempfile.TemporaryDirectory(prefix="wave-quadratic-")
        outdir = Path(owned_tmp.name)
    controls = reference.CONTROLS.copy()
    descriptors = {grid: read_grid_descriptor(exe, outdir, *grid) for grid in GRIDS}
    coeffs = {grid: descriptors[grid]["column"] for grid in GRIDS}
    coarse_q = mode_pair_controls(coeffs[GRIDS[0]], controls)
    fine_q = np.zeros(4*GRIDS[1][2]+1, dtype=np.complex128)
    for i in range(2):
        mode = reference.mode_pair(coeffs[GRIDS[0]], i)
        fine_q += controls[2*i]*reference.remap_eta(coeffs[GRIDS[0]], mode[1], coeffs[GRIDS[1]])
        fine_q += controls[2*i+1]*reference.remap_eta(coeffs[GRIDS[0]], mode[2], coeffs[GRIDS[1]])
    coarse_modes = [reference.mode_pair(coeffs[GRIDS[0]], i) for i in range(2)]
    q0 = controls[0]*coarse_modes[0][1] + controls[1]*coarse_modes[0][2]
    q1 = controls[2]*coarse_modes[1][1] + controls[3]*coarse_modes[1][2]
    exe_hash = sha256_file(exe)
    input_receipt = {"native_executable": str(exe), "native_executable_sha256": exe_hash,
                     "controls": controls.tolist(), "grids": {}}
    for grid, q in ((GRIDS[0], coarse_q), (GRIDS[1], fine_q)):
        nx, ny, nz = grid
        offsets = packed_offsets(nx, ny, nz)
        packed_direction = pack_mode(q, nx, ny, nz, offsets)
        desc = descriptors[grid]
        c = coeffs[grid]
        c2 = dict(c)
        c2["k_physical"] = 4.0*np.pi/float(c["lx"])
        c2["kappa"] = 2.0*np.sin(2.0*np.pi*float(c["dx"])/float(c["lx"]))*float(c["rdx_fp32"])
        input_receipt["grids"]["x".join(map(str, grid))] = {
            "PHB_sha256": desc["phb_sha256"],
            "native_base_state_sha256": hashlib.sha256(
                np.asarray(desc["arrays"]["base"], dtype=np.float64).tobytes()).hexdigest(),
            "source_A1_sha256": hashlib.sha256(
                np.asarray(c["A"], dtype=np.complex128).tobytes()).hexdigest(),
            "source_A2_sha256": hashlib.sha256(
                np.asarray(reference.source_matrix(c2), dtype=np.complex128).tobytes()).hexdigest(),
            "source_initial_m1": [[float(v.real), float(v.imag)] for v in q],
            "packed_direction_sha256": hashlib.sha256(packed_direction.tobytes()).hexdigest(),
            "packed_native_initial_sha256": hashlib.sha256(
                (np.asarray(desc["arrays"]["base"], dtype=np.float64)+packed_direction).tobytes()).hexdigest(),
        }
    coarse_offsets = packed_offsets(*GRIDS[0])
    coarse_nx, coarse_ny, coarse_nz = GRIDS[0]
    for label, q in (("mode_0_self", q0), ("mode_1_self", q1),
                     ("combined_cross", coarse_q), ("half_domain_phase", -coarse_q)):
        direction = pack_mode(q, coarse_nx, coarse_ny, coarse_nz, coarse_offsets)
        input_receipt.setdefault("coarse_probe_directions", {})[label] = {
            "source_m1": [[float(v.real), float(v.imag)] for v in q],
            "packed_direction_sha256": hashlib.sha256(direction.tobytes()).hexdigest(),
        }
    input_receipt["trajectory_inputs"] = {}
    for grid, q in ((GRIDS[0], coarse_q), (GRIDS[1], fine_q)):
        nx, ny, nz = grid
        packed = pack_mode(q, nx, ny, nz, packed_offsets(nx, ny, nz))
        input_receipt["trajectory_inputs"]["x".join(map(str, grid))] = {
            "duration_seconds": 30.0,
            "dt_steps": {"1": 30, "0.5": 60},
            "zero_base_direction_sha256": hashlib.sha256(
                np.zeros_like(packed).tobytes()).hexdigest(),
            "signed_direction_sha256": {
                f"dt{dt:g}_a{amp:g}_{sign:+g}": hashlib.sha256(
                    (sign*amp*packed).tobytes()).hexdigest()
                for dt in (1.0, 0.5) for amp in (1.0, 0.5) for sign in (-1.0, 1.0)},
        }
    adjoint_offsets = packed_offsets(*GRIDS[0])
    adjoint_nx, adjoint_ny, adjoint_nz = GRIDS[0]
    _, adjoint_center = source_direction(coeffs[GRIDS[0]], adjoint_nx, adjoint_ny,
                                         adjoint_nz, adjoint_offsets, controls)
    _, adjoint_delta = source_direction(coeffs[GRIDS[0]], adjoint_nx, adjoint_ny,
                                        adjoint_nz, adjoint_offsets, CONTROL_DIRECTION)
    input_receipt["adjoint_inputs"] = {
        "steps": 6, "dt": 1.0, "finite_difference_epsilons": [0.01, 0.005],
        "center_direction_sha256": hashlib.sha256(adjoint_center.tobytes()).hexdigest(),
        "control_direction_state_sha256": hashlib.sha256(adjoint_delta.tobytes()).hexdigest(),
    }
    receipt_path = outdir/"wave_quadratic_inputs_receipt.json"
    receipt_path.write_text(json.dumps(input_receipt, indent=2, sort_keys=True)+"\n")
    result = {"runner": str(Path(__file__).resolve()),
              "runner_sha256": sha256_file(Path(__file__).resolve()),
              "native_executable": str(exe), "native_executable_sha256": exe_hash,
              "inputs_receipt": receipt_path.name,
              "repository_commit": subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT,
                                                   check=True, capture_output=True, text=True).stdout.strip(),
              "native_test_source_sha256": sha256_file(
                  ROOT/"external/libtorch_wrf/sdirk3/tests/test_native_wave_refinement.cpp"),
              "python": platform.python_version(), "numpy": np.__version__, "scipy": scipy.__version__,
              "source_reference_sha256": sha256_file(TOOLS/"wave_quadratic_reference.py"),
              "source_transport_sha256": sha256_file(TOOLS/"wave_quadratic_transport.py"),
              "source_linear_sha256": sha256_file(TOOLS/"wave_energy_spatial_reference.py"),
              "controls": controls.tolist(),
              "grids": {"8x6x4": {"PHB_sha256": descriptors[GRIDS[0]]["phb_sha256"],
                                    "branch_config": descriptors[GRIDS[0]]["branch_config"],
                                    "native_compiler": descriptors[GRIDS[0]]["text"].get("native_compiler_version"),
                                    "native_torch": descriptors[GRIDS[0]]["text"].get("native_torch_version")},
                        "16x12x8": {"PHB_sha256": descriptors[GRIDS[1]]["phb_sha256"],
                                    "branch_config": descriptors[GRIDS[1]]["branch_config"],
                                    "native_compiler": descriptors[GRIDS[1]]["text"].get("native_compiler_version"),
                                    "native_torch": descriptors[GRIDS[1]]["text"].get("native_torch_version")}},
              "coarse_common_eta_reference": "source native PHB, two selected source modes, fixed common eta remap",
              "coarse_initial_m1": [[float(x.real), float(x.imag)] for x in coarse_q],
              "fine_initial_m1": [[float(x.real), float(x.imag)] for x in fine_q],
              "instantaneous_coefficients": {}, "trajectory": {}, "scope":
              "source-transcribed fixture-scope complete Q2; AD only supplies native forward/VJP responses"}
    for grid in GRIDS:
        label = "x".join(map(str, grid))
        q_initial = coarse_q if grid == GRIDS[0] else fine_q
        result["instantaneous_coefficients"][label] = run_probe(
            exe, outdir, grid, descriptors[grid], q_initial)
    # Verify each selected mode's self term and the independently formed
    # bilinear cross term, so combined forcing cannot pass by amplitude-only
    # scaling or cancellation between the two controls.
    coarse = descriptors[GRIDS[0]]["column"]
    probe0 = run_probe(exe, outdir/"mode0_self", GRIDS[0], descriptors[GRIDS[0]], q0,
                       check_halfshift=False)
    probe1 = run_probe(exe, outdir/"mode1_self", GRIDS[0], descriptors[GRIDS[0]], q1,
                       check_halfshift=False)
    combined = result["instantaneous_coefficients"]["8x6x4"]
    native_cross = (complex_pairs(combined["native_quadratic_wide"])
                    - complex_pairs(probe0["native_quadratic_wide"])
                    - complex_pairs(probe1["native_quadratic_wide"]))
    source_cross = (reference.total_quadratic_forcing(coarse, q0+q1)
                    - reference.total_quadratic_forcing(coarse, q0)
                    - reference.total_quadratic_forcing(coarse, q1))
    expected_cross = tile_source_mode(source_cross, GRIDS[0][1], GRIDS[0][2])
    cross_floors = eos_pressure_floors(coarse, *GRIDS[0])
    cross_errors = componentwise_check(native_cross, expected_cross, cross_floors,
                                        GRIDS[0][1], GRIDS[0][2],
                                        ACTIVE_RELATIVE_BUDGET, "independent modal cross term")
    result["coarse_modal_decomposition"] = {
        "mode_0_self_probe": probe0,
        "mode_1_self_probe": probe1,
        "native_cross_term": [[float(x.real), float(x.imag)] for x in native_cross],
        "source_cross_term": [[float(x.real), float(x.imag)] for x in expected_cross],
        "component_acceptance": cross_errors,
    }
    if not args.skip_trajectories:
        for grid in GRIDS:
            label = "x".join(map(str, grid))
            result["trajectory"][label] = run_trajectory(
                exe, outdir, grid, descriptors[grid],
                coarse_q if grid == GRIDS[0] else fine_q)
    if not args.skip_adjoint:
        result["adjoint_directional_check"] = run_adjoint_check(
            exe, outdir, descriptors[GRIDS[0]], controls)
    rendered = json.dumps(result, indent=2, sort_keys=True)
    (outdir/"wave_quadratic_forcing_report.json").write_text(rendered+"\n")
    print(rendered)
    print(f"\nArtifacts: {outdir}")


if __name__ == "__main__":
    main()
