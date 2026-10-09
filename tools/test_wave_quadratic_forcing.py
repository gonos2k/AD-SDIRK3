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
import wave_quadratic_transport as transport  # noqa: E402


GRIDS = ((8, 6, 4), (16, 12, 8))
TIMES = (10.0, 20.0, 30.0)
LONG_DT_STEPS = {5.0: 180, 2.5: 360}
PACKED_BLOCK_NAMES = ("U", "V", "W", "PH", "THETA", "MU")
PACKED_BLOCK_UNITS = {"U": "m/s", "V": "m/s", "W": "m/s", "PH": "m2/s2",
                      "THETA": "K", "MU": "Pa"}
PACKED_BLOCK_STORAGE = {
    "U": "raw flattened storage includes duplicated periodic x endpoint",
    "V": "all y faces, nz levels, nx cells",
    "W": "raw flattened storage includes fixed bottom face",
    "PH": "raw flattened storage includes fixed bottom face",
    "THETA": "all mass cells",
    "MU": "all horizontal cells",
}
CONTROL_DIRECTION = np.array([0.15, -0.12, 0.09, 0.17], dtype=np.float64)
ACTIVE_RELATIVE_BUDGET = 1.0e-3
ADJOINT_RELATIVE_BUDGET = 2.0e-3
FINITE_UPWIND_FD_EPSILONS = (0.08, 0.04)


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


def packed_block_slices(nx: int, ny: int, nz: int) -> dict[str, slice]:
    """Authoritative six-block slices for the packed native state."""
    offsets = packed_offsets(nx, ny, nz)
    stops = (*offsets[1:], offsets[-1]+ny*nx)
    return {name: slice(start, stop) for name, start, stop in
            zip(PACKED_BLOCK_NAMES, offsets, stops)}


def packed_block_marker(nx: int, ny: int, nz: int) -> dict:
    """Type marker: U=0, other blocks=1, raw L2 equals sqrt(element count)."""
    slices = packed_block_slices(nx, ny, nz)
    marker = np.ones(slices["MU"].stop, dtype=np.float64)
    marker[slices["U"]] = 0.0
    measured = raw_packed_temporal_scatter(np.zeros_like(marker), marker, nx, ny, nz)
    expected_counts = {
        "U": ny*nz*(nx+1), "V": (ny+1)*nz*nx,
        "W": ny*(nz+1)*nx, "PH": ny*(nz+1)*nx,
        "THETA": ny*nz*nx, "MU": ny*nx,
    }
    if (nx, ny, nz) == (8, 6, 4):
        expected_counts = {"U": 216, "V": 224, "W": 240, "PH": 240,
                           "THETA": 192, "MU": 48}
    result = {}
    for name in PACKED_BLOCK_NAMES:
        count = expected_counts[name]
        measured_block = measured["blocks"][name]
        if measured_block["raw_storage_count"] != count:
            raise AssertionError(f"packed {name} shape count {measured_block['raw_storage_count']} != {count}")
        expected = 0.0 if name == "U" else float(np.sqrt(count))
        observed = measured_block["raw_packed_l2"]
        if abs(observed-expected) > 16.0*np.finfo(float).eps*max(expected, 1.0):
            raise AssertionError(f"packed {name} marker norm {observed} != {expected}")
        result[name] = {"elements": count, "marker_raw_l2_from_temporal_helper": observed,
                        "expected_raw_l2": expected, "unit": PACKED_BLOCK_UNITS[name],
                        "storage": PACKED_BLOCK_STORAGE[name]}
    return result


def flip_velocity_blocks(direction: np.ndarray, nx: int, ny: int, nz: int) -> np.ndarray:
    """Negate only U/V/W packed blocks; retain PH/theta/MU bits exactly."""
    flipped = np.asarray(direction, dtype=np.float64).copy()
    slices = packed_block_slices(nx, ny, nz)
    for name in ("U", "V", "W"):
        flipped[slices[name]] *= -1.0
    for name in ("PH", "THETA", "MU"):
        if not np.array_equal(flipped[slices[name]], direction[slices[name]]):
            raise AssertionError(f"velocity reversal changed {name} input bits")
    return flipped


def raw_packed_temporal_scatter(coarse: np.ndarray, fine: np.ndarray,
                                nx: int, ny: int, nz: int) -> dict[str, dict]:
    """Per-variable raw storage L2; no physical, mass, or energy weighting."""
    slices = packed_block_slices(nx, ny, nz)
    expected_size = slices["MU"].stop
    if coarse.shape != fine.shape or coarse.size != expected_size:
        raise ValueError(f"expected packed endpoint vectors of length {expected_size}")
    return {"norm_definition": "Euclidean L2 of the raw flattened native block; unweighted",
            "blocks": {name: {"raw_packed_l2": float(np.linalg.norm((fine-coarse)[block])),
                              "raw_storage_count": block.stop-block.start,
                              "unit": PACKED_BLOCK_UNITS[name],
                              "storage": PACKED_BLOCK_STORAGE[name]}
                       for name, block in slices.items()}}


def compare_matched_T300_steps(
        states_by_dt: dict[float, dict[float, np.ndarray]],
        initial_hashes_by_dt: dict[float, dict[float, str]],
        nx: int, ny: int, nz: int) -> dict:
    """Reanalyze matched T300 endpoint states without rerunning native forecasts.

    Inputs are raw packed ``checkpoint(T=300)-zero_baseline(T=300)`` vectors
    keyed by dt and amplitude, plus SHA256 hashes of each serialized initial
    state. The output gives actual per-block storage L2 scatters and ratios;
    it makes no time-order assumption.
    """
    dts = tuple(sorted(states_by_dt, reverse=True))
    if len(dts) < 2 or any(dt not in initial_hashes_by_dt for dt in dts):
        raise ValueError("matched step study needs at least two dt inputs and initial-state hashes")
    amplitudes = tuple(sorted(next(iter(states_by_dt.values()))))
    expected_size = packed_block_slices(nx, ny, nz)["MU"].stop
    for dt in dts:
        if set(states_by_dt[dt]) != set(amplitudes):
            raise ValueError("every dt must provide the same amplitudes")
        for amp in amplitudes:
            state = np.asarray(states_by_dt[dt][amp])
            if state.ndim != 1 or state.size != expected_size:
                raise ValueError(f"dt={dt:g}, amp={amp:g}: expected raw packed vector size {expected_size}")
    initial_hashes = {}
    for amp in amplitudes:
        hashes = [initial_hashes_by_dt[dt][amp] for dt in dts]
        if len(set(hashes)) != 1:
            raise AssertionError(f"T300 dt runs do not share an identical initial state at a={amp:g}")
        initial_hashes[str(amp)] = hashes[0]
    amplitudes_out = {}
    for amp in amplitudes:
        pair_scatter = {}
        for dt0, dt1 in zip(dts[:-1], dts[1:]):
            pair_scatter[f"{dt0:g}_vs_{dt1:g}"] = raw_packed_temporal_scatter(
                np.asarray(states_by_dt[dt0][amp]), np.asarray(states_by_dt[dt1][amp]),
                nx, ny, nz)
        ratios = {}
        if len(dts) >= 3:
            coarse = pair_scatter[f"{dts[0]:g}_vs_{dts[1]:g}"]["blocks"]
            fine = pair_scatter[f"{dts[1]:g}_vs_{dts[2]:g}"]["blocks"]
            for name in PACKED_BLOCK_NAMES:
                numerator = coarse[name]["raw_packed_l2"]
                denominator = fine[name]["raw_packed_l2"]
                ratios[name] = numerator/denominator if denominator > 0.0 else None
        amplitudes_out[str(amp)] = {
            "initial_state_sha256": initial_hashes[str(amp)],
            "raw_temporal_scatter_by_dt_pair": pair_scatter,
            "observed_adjacent_pair_scatter_ratio_by_raw_block": ratios,
            "raw_endpoint_packed_state_deltas_by_dt": {
                str(dt): np.asarray(states_by_dt[dt][amp]).tolist() for dt in dts},
        }
    return {"grid": [nx, ny, nz], "endpoint_seconds": 300.0,
            "dt_values_coarse_to_fine": list(dts),
            "identical_initial_state_by_amplitude": initial_hashes,
            "raw_block_marker": packed_block_marker(nx, ny, nz),
            "norm_definition": "unweighted raw flattened block L2; physical units and storage aliases/ghosts are explicit",
            "time_order_claimed": False, "amplitudes": amplitudes_out}


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


def translate_packed(z: np.ndarray, nx: int, ny: int, nz: int,
                     offsets: tuple[int, ...], cells: int) -> np.ndarray:
    """Translate f(x) to f(x+cells*dx), preserving the periodic U alias."""
    translated = np.empty_like(z)
    ends = (*offsets[1:], z.size)
    shapes = ((ny, nz, nx+1), (ny+1, nz, nx), (ny, nz+1, nx),
              (ny, nz+1, nx), (ny, nz, nx), (ny, nx))
    for block, (start, end, shape) in enumerate(zip(offsets, ends, shapes)):
        source = z[start:end].reshape(shape)
        target = translated[start:end].reshape(shape)
        if block == 0:
            target[..., :nx] = np.roll(source[..., :nx], -cells, axis=-1)
            target[..., nx] = target[..., 0]
        else:
            target[...] = np.roll(source, -cells, axis=-1)
    return translated


def physical_blocks(z: np.ndarray, nx: int, ny: int, nz: int,
                    offsets: tuple[int, ...]) -> np.ndarray:
    """Unique U faces and active W/PH planes in source block order, without FFT."""
    o_u, o_v, o_w, o_ph, o_t, o_mu = offsets
    return np.concatenate((z[o_u:o_v].reshape(ny, nz, nx+1)[..., :nx].ravel(),
                           z[o_w:o_ph].reshape(ny, nz+1, nx)[:, 1:].ravel(),
                           z[o_ph:o_t].reshape(ny, nz+1, nx)[:, 1:].ravel(),
                           z[o_t:o_mu], z[o_mu:]))


def translation_negative_control(nx: int, ny: int, nz: int,
                                 offsets: tuple[int, ...]) -> dict:
    """The same physical covariance comparison must reject a fixed-x defect."""
    q = np.zeros(4*nz+1, dtype=np.complex128)
    q[:nz] = 1.0
    direction = pack_mode(q, nx, ny, nz, offsets)
    def defective_forcing(z):
        result = np.zeros_like(z)
        u = z[:offsets[1]].reshape(ny, nz, nx+1)
        rhs_u = result[:offsets[1]].reshape(ny, nz, nx+1)
        fixed_x = 1.0 + 0.4*np.cos(4.0*np.pi*np.arange(nx)/nx)
        rhs_u[..., :nx] = fixed_x*u[..., :nx]**2
        rhs_u[..., nx] = rhs_u[..., 0]
        return result
    actual = defective_forcing(translate_packed(direction, nx, ny, nz, offsets, nx//4))
    expected = translate_packed(defective_forcing(direction), nx, ny, nz, offsets, nx//4)
    a = physical_blocks(actual, nx, ny, nz, offsets)
    e = physical_blocks(expected, nx, ny, nz, offsets)
    rejected = False
    try:
        componentwise_check(a, e, np.full_like(a, 1.0e-14), ny*nx, nz,
                            ACTIVE_RELATIVE_BUDGET, "fixed-x translation negative control")
    except AssertionError:
        rejected = True
    if not rejected:
        raise AssertionError("quarter-period translation accepted the fixed-x defect")
    return {"rejected": rejected, "field_max_abs_defect": float(np.max(np.abs(a-e))),
            "m2_defect_norm": float(np.linalg.norm(project_m2(actual-expected, nx, ny, nz, offsets)))}


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


def project_harmonic_state(z: np.ndarray, harmonic: int, nx: int, ny: int, nz: int,
                           offsets: tuple[int, ...], lx: float = 40000.0) -> np.ndarray:
    """Project represented Fourier modes in source block order, without averaging y."""
    if harmonic < 0 or harmonic > nx//2:
        raise ValueError("harmonic must be in the represented periodic-x range")
    o_u, o_v, o_w, o_ph, o_t, o_mu = offsets
    u = z[o_u:o_v].reshape(ny, nz, nx+1)[:, :, :nx]
    w = z[o_w:o_ph].reshape(ny, nz+1, nx)[:, 1:nz+1]
    ph = z[o_ph:o_t].reshape(ny, nz+1, nx)[:, 1:nz+1]
    theta = z[o_t:o_mu].reshape(ny, nz, nx)
    mu = z[o_mu:].reshape(ny, nx)
    wave = 2.0*np.pi*harmonic/lx
    xu = np.arange(nx, dtype=np.float64)*(lx/nx)
    xc = (np.arange(nx, dtype=np.float64)+0.5)*(lx/nx)
    factor = 1.0 if harmonic == 0 or (nx % 2 == 0 and harmonic == nx//2) else 2.0
    def project(a: np.ndarray, x: np.ndarray) -> np.ndarray:
        return factor*np.mean(a*np.exp(-1j*wave*x), axis=-1)
    return np.concatenate((project(u, xu).ravel(), project(w, xc).ravel(),
                           project(ph, xc).ravel(), project(theta, xc).ravel(),
                           project(mu, xc).ravel()))


def reconstruct_source_mode(q: np.ndarray, harmonic: int, nx: int, ny: int, nz: int,
                            offsets: tuple[int, ...], lx: float = 40000.0) -> np.ndarray:
    """Reconstruct one real C-grid harmonic from source order [U,W,PH,TH,MU]."""
    q = np.asarray(q, dtype=np.complex128)
    if harmonic < 0 or harmonic > nx//2 or q.shape != (4*nz+1,):
        raise ValueError("harmonic must be in the representable range with source vector length 4*nz+1")
    result = np.zeros(offsets[-1]+ny*nx, dtype=np.float64)
    o_u, o_v, o_w, o_ph, o_t, o_mu = offsets
    wave = 2.0*np.pi*harmonic/lx
    xu = np.arange(nx, dtype=np.float64)*(lx/nx)
    xc = (np.arange(nx, dtype=np.float64)+0.5)*(lx/nx)
    face_phase = np.exp(1j*wave*xu)
    center_phase = np.exp(1j*wave*xc)
    u = result[o_u:o_v].reshape(ny, nz, nx+1)
    w = result[o_w:o_ph].reshape(ny, nz+1, nx)
    ph = result[o_ph:o_t].reshape(ny, nz+1, nx)
    theta = result[o_t:o_mu].reshape(ny, nz, nx)
    mu = result[o_mu:].reshape(ny, nx)
    for k in range(nz):
        u[:, k, :nx] = np.real(q[k]*face_phase)[None, :]
        u[:, k, nx] = u[:, k, 0]
        w[:, k+1, :] = np.real(q[nz+k]*center_phase)[None, :]
        ph[:, k+1, :] = np.real(q[2*nz+k]*center_phase)[None, :]
        theta[:, k, :] = np.real(q[3*nz+k]*center_phase)[None, :]
    mu[:, :] = np.real(q[-1]*center_phase)[None, :]
    return result


def project_v_m2(z: np.ndarray, nx: int, ny: int, nz: int,
                 offsets: tuple[int, ...], lx: float = 40000.0) -> np.ndarray:
    """Project all meridional V faces; the uniform-y x-z fixture predicts zero."""
    o_v = offsets[1]
    v = z[o_v:o_v+(ny+1)*nz*nx].reshape(ny+1, nz, nx)
    phase = np.exp(-4j*np.pi*(np.arange(nx, dtype=np.float64)+0.5)/nx)
    return (2.0*np.mean(v*phase, axis=-1)).ravel()


def raw_v_block(z: np.ndarray, nx: int, ny: int, nz: int,
                offsets: tuple[int, ...]) -> np.ndarray:
    """Return every V face/layer/cell value from the native packed state."""
    start, end = offsets[1], offsets[2]
    return z[start:end].reshape(ny+1, nz, nx)


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
                "non_hydrostatic_config", "map_input_max_deviation",
                "f_input_max_abs", "e_input_max_abs", "wrf_w_damping_config",
                "wrf_damp_opt_config", "diffusion_option_config",
                "khdif_config", "kvdif_config", "rayleigh_damp_coef_config",
                "rayleigh_damp_depth_config")
    missing = [key for key in required if key not in meta]
    if missing:
        raise AssertionError(f"{label}: native payload omits branch metadata: {missing}")
    values = {key: float(meta[key]) for key in required}
    expected = {"kdamp_config": 0.0, "implicit_divergence": 0.0,
                "omega_w_blend_config": 1.0, "do_curvature_config": 1.0,
                "effective_wrf_omega_ww_cp": 1.0, "advection_order_config": 2.0,
                "non_hydrostatic_config": 1.0, "map_input_max_deviation": 0.0,
                "f_input_max_abs": 0.0, "e_input_max_abs": 0.0,
                "wrf_w_damping_config": 0.0, "wrf_damp_opt_config": 0.0,
                "diffusion_option_config": 2.0, "khdif_config": 0.0,
                "kvdif_config": 0.0}
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


def objective_m3_value(meta: dict[str, float], scalars: dict[str, float],
                       arrays: dict[str, np.ndarray]) -> float:
    if int(meta.get("objective_x_harmonic", -1)) != 3:
        raise AssertionError("native upwind trajectory did not select the m=3 objective")
    if "objective_w_m3_projection" in scalars:
        return float(scalars["objective_w_m3_projection"])
    if "final" in arrays and "terminal_cotangent" in arrays:
        return float(np.dot(arrays["final"], arrays["terminal_cotangent"]))
    raise AssertionError("native m=3 trajectory payload has no terminal W projection")


def objective_dot_fp64_terms(final_state: np.ndarray, terminal: np.ndarray) -> dict:
    """Objective cancellation diagnostics from actual dot operands, not a full solver bound."""
    products = np.asarray(final_state, dtype=np.float64)*np.asarray(terminal, dtype=np.float64)
    objective = float(np.sum(products, dtype=np.float64))
    absolute_product_sum = float(np.sum(np.abs(products), dtype=np.float64))
    floor = 128.0*np.finfo(np.float64).eps*absolute_product_sum
    return {"objective_from_operands": objective,
            "sum_abs_final_times_terminal": absolute_product_sum,
            "fp64_dot_product_floor": floor,
            "not_a_full_solver_error_bound": True,
            "cancellation_ratio": absolute_product_sum/max(abs(objective), np.finfo(float).tiny)}


def objective_floor_cancellation_falsifier() -> dict:
    """Show that |J| alone can understate a dot-product roundoff bound."""
    eps = np.finfo(np.float64).eps
    final = np.array([1.0, 1.0], dtype=np.float64)
    terminal = np.array([1.0, -(1.0-32.0*eps)], dtype=np.float64)
    measured = objective_dot_fp64_terms(final, terminal)
    old_floor = 128.0*eps*abs(measured["objective_from_operands"])
    if measured["fp64_dot_product_floor"] <= old_floor*1.0e8:
        raise AssertionError("cancellation falsifier did not distinguish operand and objective floors")
    return {**measured, "old_abs_objective_floor": old_floor,
            "old_floor_underestimates": True}


def write_direction(path: Path, direction: np.ndarray) -> None:
    with path.open("w") as stream:
        stream.write(f"{direction.size}\n")
        stream.write(" ".join(format(float(v), ".17g") for v in direction))
        stream.write("\n")


def source_direction(c: dict, nx: int, ny: int, nz: int,
                     offsets: tuple[int, ...], controls: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    q = mode_pair_controls(c, controls)
    return q, pack_mode(q, nx, ny, nz, offsets)


def sign_smoothing_regime(c: dict, q: np.ndarray, sigma: float,
                          amplitudes: tuple[float, ...] = (1.0, 0.5)) -> dict:
    """Report diagnosed |Omega|/sigma; do not infer a Taylor degree from it."""
    dx, lx = float(c["dx"]), float(c["lx"])
    k1 = float(c["k_physical"])
    rdx = float(c["rdx_fp32"])
    half_cos = float(np.cos(0.5*k1*dx))
    mass = float(np.sum(c["eta_delta"]*c["layer_mass"]))
    omega = transport._omega_modes(c, q[:int(c["nz"])], complex(q[-1]),
                                   k1, rdx, half_cos, mass)[0]
    peak = float(np.max(np.abs(omega)))
    romm_peak = float(np.max(np.abs(0.5*(omega[:-1]+omega[1:]))))
    omega_u_peak = abs(half_cos)*peak
    return {"omega1_peak_pa_per_s": peak,
            "romm1_peak_pa_per_s": romm_peak,
            "omega_u1_peak_pa_per_s": omega_u_peak,
            "sign_smooth_delta_config": float(sigma),
            "omega_to_configured_delta_ratio_by_amplitude": {
                str(a): a*peak/float(sigma) for a in amplitudes},
            "romm_to_configured_delta_ratio_by_amplitude": {
                str(a): a*romm_peak/float(sigma) for a in amplitudes},
            "configured_delta_unit_comment": "delta uses velocity units for velocity-like branches and Pa/s for canonical Omega; it is not a universal velocity interval",
            "interpretation": "branch-regime diagnostic only; finite amplitudes are not asserted cubic"}


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
        raise AssertionError(f"{label} zero response {absolute:.6g} exceeds EOS/Pa floor {floor_norm:.6g}")
    return {"absolute_norm": absolute, "eos_pressure_floor_norm": floor_norm,
            "max_abs_complex": float(np.max(np.abs(actual))) if actual.size else 0.0}


def closed_mass_mean(z: np.ndarray, nx: int, ny: int, offsets: tuple[int, ...]) -> float:
    """Domain-mean column-mass perturbation; other field means may be nonzero."""
    mu = z[offsets[-1]:].reshape(ny, nx)
    return float(np.mean(mu))


def mass_weighted_theta_change(initial: np.ndarray, state: np.ndarray,
                               mubase: np.ndarray, c: dict, nx: int, ny: int,
                               nz: int, offsets: tuple[int, ...]) -> tuple[float, float]:
    """Compute ∫(MΘ−M0Θ0) dA/g dη and its FP64 product/sum floor."""
    o_theta, o_mu = offsets[4], offsets[5]
    theta0 = (300.0+initial[o_theta:o_mu]).reshape(ny, nz, nx)
    theta = (300.0+state[o_theta:o_mu]).reshape(ny, nz, nx)
    mass0 = mubase.reshape(ny, nx)+initial[o_mu:].reshape(ny, nx)
    mass = mubase.reshape(ny, nx)+state[o_mu:].reshape(ny, nx)
    delta_theta = theta-theta0
    delta_mass = mass-mass0
    term_mass_theta = mass0[:, None, :]*delta_theta
    term_theta_mass = theta0*delta_mass[:, None, :]
    term_cross = delta_mass[:, None, :]*delta_theta
    eta_delta = np.broadcast_to(np.asarray(c["eta_delta"], dtype=np.float64), (nz,))
    eta_weight = eta_delta[None, :, None]
    cell_area_over_g = float(c["area"])/(nx*ny*float(c["g"]))
    weighted = eta_weight*(term_mass_theta+term_theta_mass+term_cross)
    value = cell_area_over_g*float(np.sum(weighted, dtype=np.float64))
    abs_products = cell_area_over_g*float(np.sum(np.abs(eta_weight)*(
        np.abs(term_mass_theta)+np.abs(term_theta_mass)+np.abs(term_cross)), dtype=np.float64))
    abs_sum = cell_area_over_g*float(np.sum(np.abs(weighted), dtype=np.float64))
    eps = np.finfo(np.float64).eps
    cells = ny*nz*nx
    gamma_cells = (cells*eps)/(1.0-cells*eps)
    gamma_products = (5.0*eps)/(1.0-5.0*eps)
    floor = gamma_cells*abs_sum+gamma_products*abs_products
    return value, floor


def mass_theta_uniform_calibration(c: dict, nx: int, ny: int, nz: int) -> dict:
    """Check that a uniform 1 K change integrates to area*M_TOTAL/g on each grid."""
    offsets = packed_offsets(nx, ny, nz)
    state_size = offsets[-1]+ny*nx
    initial = np.zeros(state_size, dtype=np.float64)
    theta_start, mu_start = offsets[4], offsets[5]
    mubase_value = np.asarray(c.get("MUB", 80000.0), dtype=np.float64)
    mubase = np.broadcast_to(mubase_value, (ny, nx)).copy()
    mu_initial = float(c["M_TOTAL"])-mubase
    initial[theta_start:mu_start] = np.broadcast_to(
        np.asarray(c["theta"], dtype=np.float64)[None, :, None]-300.0,
        (ny, nz, nx)).ravel()
    initial[mu_start:] = mu_initial.ravel()
    final = initial.copy()
    final[theta_start:mu_start] += 1.0
    actual, floor = mass_weighted_theta_change(initial, final, mubase, c,
                                               nx, ny, nz, offsets)
    expected = float(c["area"])*float(c["M_TOTAL"])/float(c["g"])
    error = abs(actual-expected)
    if error > floor:
        raise AssertionError(f"uniform 1 K mass-theta calibration {error:.6g} > FP64 floor {floor:.6g}")
    return {"temperature_increment_k": 1.0, "actual_integral_change": actual,
            "expected_domain_area_Mtotal_over_g": expected,
            "absolute_error": error, "fp64_product_sum_floor": floor,
            "cell_area": float(c["area"])/(nx*ny)}


def source_mass_weighted_theta2(c: dict, q1_initial: np.ndarray,
                                q0_mean: np.ndarray, q1: np.ndarray) -> float:
    """Second-order source change of the dry mass-weighted theta integral."""
    n = int(c["nz"])
    q0_mean = np.asarray(q0_mean, dtype=np.complex128)
    mean_imaginary_norm = float(np.linalg.norm(q0_mean.imag))
    mean_real_norm = float(np.linalg.norm(q0_mean.real))
    imaginary_floor = (128.0*np.finfo(np.float64).eps*
                       max(mean_real_norm, np.finfo(np.float64).tiny))
    if mean_imaginary_norm > imaginary_floor:
        raise AssertionError(f"m=0 source state has imaginary component {mean_imaginary_norm:.6g} > "
                             f"real-L0 FP64 floor {imaginary_floor:.6g}")
    covariance = 0.5*np.real(q1[-1]*np.conj(q1[3*n:4*n]))
    initial_covariance = 0.5*np.real(q1_initial[-1]*np.conj(q1_initial[3*n:4*n]))
    integrand = (float(c["M_TOTAL"])*q0_mean.real[3*n:4*n]
                 + np.asarray(c["theta"])*q0_mean.real[-1]
                 + covariance-initial_covariance)
    return float(c["area"])/float(c["g"])*float(np.sum(
        np.asarray(c["eta_delta"])*integrand, dtype=np.float64))


def source_mass_weighted_theta_floor(c: dict, q0_tightening_state: np.ndarray) -> float:
    """Map q0 solver tightening through the mass-weighted theta functional."""
    n = int(c["nz"])
    state_gap = np.asarray(q0_tightening_state, dtype=np.complex128)
    gap_theta = np.abs(state_gap[3*n:4*n])
    gap_mu = abs(state_gap[-1])
    integrand_bound = (float(c["M_TOTAL"])*gap_theta+
                       np.abs(np.asarray(c["theta"])) * gap_mu)
    return (float(c["area"])/float(c["g"]) *
            float(np.sum(np.abs(c["eta_delta"])*integrand_bound, dtype=np.float64)))


def native_physical_domain(state: np.ndarray, phb: np.ndarray, mubase: np.ndarray,
                           c: dict, nx: int, ny: int, nz: int,
                           offsets: tuple[int, ...]) -> dict:
    """Check finite dry EOS state without clipping or repairing invalid values."""
    o_ph, o_theta, o_mu = offsets[3], offsets[4], offsets[5]
    mass_base = np.asarray(mubase, dtype=np.float64).reshape(ny, nx)
    mass = mass_base+state[o_mu:].reshape(ny, nx)
    theta = 300.0+state[o_theta:o_mu].reshape(ny, nz, nx)
    ph = state[o_ph:o_theta].reshape(ny, nz+1, nx)
    phb3 = np.asarray(phb, dtype=np.float64).reshape(ny, nz+1, nx)
    total_dphi = np.diff(phb3+ph, axis=1)
    pert_dphi = np.diff(ph, axis=1)
    finite_input = bool(np.isfinite(state).all() and np.isfinite(phb3).all())
    with np.errstate(divide="ignore", invalid="ignore", over="ignore"):
        alpha = (np.asarray(c["alpha_base"])[None, :, None]*mass_base[:, None, :]
                 - np.asarray(c["rdnw"])[None, :, None]*pert_dphi)/mass[:, None, :]
        pressure = 100000.0*(float(c["rd"])*theta/(100000.0*alpha))**float(c["gamma"])
        density = 1.0/alpha
    finite_eos = bool(np.isfinite(alpha).all() and np.isfinite(pressure).all() and
                      np.isfinite(density).all())
    minima = {
        "full_column_mass_pa": float(np.min(mass)) if mass.size else float("nan"),
        "theta_k": float(np.min(theta)) if theta.size else float("nan"),
        "total_geopotential_thickness": float(np.min(total_dphi)) if total_dphi.size else float("nan"),
        "inverse_density_alpha": float(np.min(alpha)) if alpha.size else float("nan"),
        "pressure_pa": float(np.min(pressure)) if pressure.size else float("nan"),
        "density": float(np.min(density)) if density.size else float("nan"),
    }
    valid = (finite_input and finite_eos and all(np.isfinite(v) and v > 0.0
                                                  for v in minima.values()))
    return {"valid": valid, "finite_input": finite_input,
            "finite_dry_eos": finite_eos, "minima": minima}


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
              q: np.ndarray, check_translation: bool = True) -> dict:
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
    v_raw_centered = []
    v_raw_base = raw_v_block(arrays["rhs_base"], nx, ny, nz, offsets)
    for level, amplitude in enumerate((1.0, 0.5, 0.25)):
        v_raw_centered.append(centered_coefficient(
            raw_v_block(arrays[f"rhs_plus_{level}"], nx, ny, nz, offsets),
            raw_v_block(arrays[f"rhs_minus_{level}"], nx, ny, nz, offsets),
            v_raw_base, amplitude))
    v_raw_wide = (4.0*v_raw_centered[1]-v_raw_centered[0])/3.0
    v_raw_narrow = (4.0*v_raw_centered[2]-v_raw_centered[1])/3.0
    v_raw_zero_check = zero_mode_check(v_raw_wide, v_floor,
                                       f"{nx}x{ny}x{nz} full raw V probe")
    v_raw_scatter_check = zero_mode_check(v_raw_wide-v_raw_narrow, v_floor,
                                          f"{nx}x{ny}x{nz} full raw V scatter")
    mean_projector = lambda v: project_harmonic_state(v, 0, nx, ny, nz, offsets)
    mean_native_center = mean_projector(arrays["rhs_base"])
    mean_native_centered = []
    for level, amplitude in enumerate((1.0, 0.5, 0.25)):
        mean_native_centered.append(centered_coefficient(
            mean_projector(arrays[f"rhs_plus_{level}"]),
            mean_projector(arrays[f"rhs_minus_{level}"]), mean_native_center, amplitude))
    native_mean_wide = (4.0*mean_native_centered[1]-mean_native_centered[0])/3.0
    native_mean_narrow = (4.0*mean_native_centered[2]-mean_native_centered[1])/3.0
    source_mean = reference.total_quadratic_mean_forcing(d["column"], q)
    source_mean_rotated = reference.total_quadratic_mean_forcing(d["column"], 1j*q)
    source_mean_covariance = componentwise_check(
        tile_source_mode(source_mean_rotated, ny, nz), tile_source_mode(source_mean, ny, nz),
        floors, ny, nz, ACTIVE_RELATIVE_BUDGET, "source m=0 quarter-period invariance")
    error_abs = float(np.linalg.norm(wide-expected_projected))
    error_rel = error_abs/max(float(np.linalg.norm(expected_projected)), float(np.linalg.norm(floors)))
    scatter_abs = float(np.linalg.norm(scatter))
    parity_abs = None
    parity_errors = {}
    v_raw_covariance = None
    mean_native_covariance = None
    translation_fields = None
    translation_negative = None
    if check_translation:
        if nx % 4:
            raise ValueError("quarter-period translation requires nx divisible by four")
        shifted_direction = translate_packed(direction, nx, ny, nz, offsets, nx//4)
        phase_direction = pack_mode(1j*q, nx, ny, nz, offsets)
        input_phase_error = float(np.max(np.abs(shifted_direction-phase_direction)))
        if input_phase_error > 32*np.finfo(float).eps*max(1.0, float(np.max(np.abs(direction)))):
            raise AssertionError("physical quarter roll disagrees with staggered m=1 phase")
        shift_path = outdir/f"direction_quartershift_{nx}x{ny}x{nz}.txt"
        write_direction(shift_path, shifted_direction)
        shifted_path = outdir/f"probe_quartershift_{nx}x{ny}x{nz}.csv"
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
        shifted_v_raw_base = raw_v_block(shifted_arrays["rhs_base"], nx, ny, nz, offsets)
        shifted_v_raw_centered = []
        for level, amplitude in enumerate((1.0, 0.5, 0.25)):
            shifted_v_raw_centered.append(centered_coefficient(
                raw_v_block(shifted_arrays[f"rhs_plus_{level}"], nx, ny, nz, offsets),
                raw_v_block(shifted_arrays[f"rhs_minus_{level}"], nx, ny, nz, offsets),
                shifted_v_raw_base, amplitude))
        shifted_v_raw_wide = (4.0*shifted_v_raw_centered[1]-shifted_v_raw_centered[0])/3.0
        v_raw_covariance = zero_mode_check(
            shifted_v_raw_wide-np.roll(v_raw_wide, -nx//4, axis=-1), v_floor,
            f"{nx}x{ny}x{nz} full raw V quartershift covariance")
        shifted_mean_center = mean_projector(shifted_arrays["rhs_base"])
        shifted_mean_centered = []
        for level, amplitude in enumerate((1.0, 0.5, 0.25)):
            shifted_mean_centered.append(centered_coefficient(
                mean_projector(shifted_arrays[f"rhs_plus_{level}"]),
                mean_projector(shifted_arrays[f"rhs_minus_{level}"]),
                shifted_mean_center, amplitude))
        shifted_mean_wide = (4.0*shifted_mean_centered[1]-shifted_mean_centered[0])/3.0
        mean_native_covariance = componentwise_check(
            shifted_mean_wide, native_mean_wide, floors, ny, nz,
            ACTIVE_RELATIVE_BUDGET, "native m=0 quarter-period invariance")
        parity_abs = float(np.linalg.norm(shifted+wide))
        parity_errors = componentwise_check(shifted, -wide, floors, ny, nz,
                                            ACTIVE_RELATIVE_BUDGET, "quarter-period m=2 phase covariance")
        field_projector = lambda v: physical_blocks(v, nx, ny, nz, offsets)
        translated_projector = lambda v: field_projector(
            translate_packed(v, nx, ny, nz, offsets, nx//4))
        shifted_field, _, _ = richardson_quadratic(shifted_arrays, field_projector)
        expected_field, _, _ = richardson_quadratic(arrays, translated_projector)
        field_floors = np.repeat(floors, nx)
        translation_fields = componentwise_check(
            shifted_field, expected_field, field_floors, ny*nx, nz,
            ACTIVE_RELATIVE_BUDGET, "quarter-period physical field covariance")
        cursor = 0
        for label, count in component_layout(ny*nx, nz):
            sl = slice(cursor, cursor+count)
            maximum = float(np.max(np.abs(shifted_field[sl]-expected_field[sl])))
            budget = max(ACTIVE_RELATIVE_BUDGET*float(np.max(np.abs(expected_field[sl]))),
                         float(np.max(field_floors[sl])))
            if maximum > budget:
                raise AssertionError(f"quarter-period {label} cellwise error {maximum} > {budget}")
            translation_fields[label].update({"max_abs_error": maximum, "cellwise_budget": budget})
            cursor += count
        translation_negative = translation_negative_control(nx, ny, nz, offsets)
        v_zero_check["quartershift_parity"] = zero_mode_check(
            shifted_vwide+v_wide, v_floor, f"{nx}x{ny}x{nz} V quartershift covariance")
    # Signed complex errors retain phase and sign information per state component.
    cursor = 0
    component_errors = {}
    for label, count in component_layout(ny, nz):
        sl = slice(cursor, cursor+count)
        delta = wide[sl]-expected_projected[sl]
        component_errors[label] = {
            **coefficient_errors[label],
            "richardson_scatter": scatter_errors[label],
            "quartershift_covariance": parity_errors.get(label),
            "max_abs_complex": float(np.max(np.abs(delta))),
            "signed_real": delta.real.tolist(), "signed_imag": delta.imag.tolist(),
            "native_real": wide[sl].real.tolist(), "native_imag": wide[sl].imag.tolist(),
            "source_real": expected_projected[sl].real.tolist(),
            "source_imag": expected_projected[sl].imag.tolist(),
        }
        cursor += count
    component_errors["V"] = {"source_expected_zero": True,
                              "eos_pressure_floor_per_cell": v_floor,
                              **v_zero_check, "richardson_scatter": v_scatter_check,
                              "full_raw_zero": v_raw_zero_check,
                              "full_raw_scatter": v_raw_scatter_check,
                              "full_raw_quartershift_covariance": v_raw_covariance}
    split_channels = {}
    for label in ("E", "I"):
        coarse = projector(arrays[f"quadratic_{label}_positive"])
        fine = projector(arrays[f"quadratic_{label}_positive_small"])
        split_channels[label] = (4.0*fine-coarse)/3.0
    return {"grid": list(grid), "direction_norm": float(np.linalg.norm(direction)),
            "native_probe_payload": path.name,
            "reference_channels_norm": {k: float(np.linalg.norm(v)) for k,v in expected_channels.items()},
            "native_quadratic_wide": [[float(x.real), float(x.imag)] for x in wide],
            "native_quadratic_narrow": [[float(x.real), float(x.imag)] for x in narrow],
            "reference_quadratic": [[float(x.real), float(x.imag)] for x in expected],
            "error_abs": error_abs, "error_scaled": error_rel,
            "eos_pa_roundoff_floors": floors.tolist(),
            "richardson_scatter_abs": scatter_abs,
            "quartershift_covariance_abs": parity_abs,
            "quartershift_scatter_abs": (float(np.linalg.norm(shifted_scatter))
                                         if check_translation else None),
            "translation_field_checks": translation_fields,
            "translation_negative_control": translation_negative,
            "native_q0_mean_quartershift_covariance": mean_native_covariance,
            "source_q0_mean_quartershift_covariance": source_mean_covariance,
            "signed_component_errors": component_errors,
            "branch_config": branch,
            "probe_amplitudes": [float(meta[f"probe_amplitude_{i}"]) for i in range(3)],
            "cpp_channels": {name: [[float(x.real), float(x.imag)] for x in values]
                             for name, values in split_channels.items()}}


def run_mean_probe(exe: Path, outdir: Path, grid: tuple[int, int, int], d: dict,
                   q: np.ndarray) -> dict:
    """Check m=0 Q from a phase-mixed mode that activates its U coefficient."""
    nx, ny, nz = grid
    offsets = packed_offsets(nx, ny, nz)
    direction = pack_mode(q, nx, ny, nz, offsets)
    direction_path = outdir/f"mean_direction_{nx}x{ny}x{nz}.txt"
    write_direction(direction_path, direction)
    path = outdir/f"mean_probe_{nx}x{ny}x{nz}.csv"
    meta, arrays, _, _ = run_native(exe, nx, ny, nz, path,
                                     ("--quadratic-probe", str(direction_path)))
    reference_mean = reference.total_quadratic_mean_forcing(d["column"], q)
    expected = tile_source_mode(reference_mean, ny, nz)
    projector = lambda v: project_harmonic_state(v, 0, nx, ny, nz, offsets)
    base = projector(arrays["rhs_base"])
    estimates = []
    for level, amplitude in enumerate((1.0, 0.5, 0.25)):
        estimates.append(centered_coefficient(
            projector(arrays[f"rhs_plus_{level}"]),
            projector(arrays[f"rhs_minus_{level}"]), base, amplitude))
    wide = (4.0*estimates[1]-estimates[0])/3.0
    narrow = (4.0*estimates[2]-estimates[1])/3.0
    floors = eos_pressure_floors(d["column"], nx, ny, nz)
    errors = componentwise_check(wide, expected, floors, ny, nz,
                                 ACTIVE_RELATIVE_BUDGET, "phase-mixed m=0 source comparison")
    scatter = componentwise_check(wide, narrow, floors, ny, nz,
                                  ACTIVE_RELATIVE_BUDGET, "phase-mixed m=0 amplitude scatter")
    u_count = ny*nz
    source_u_signal = float(np.linalg.norm(expected[:u_count]))
    native_u_signal = float(np.linalg.norm(wide[:u_count]))
    u_floor_norm = float(np.linalg.norm(floors[:u_count]))
    if source_u_signal <= u_floor_norm or native_u_signal <= u_floor_norm:
        raise AssertionError("phase-mixed Q0 U-mean signal is not resolved above its EOS/Pa floor")
    return {"grid": list(grid), "source_initial_m1": [[float(x.real), float(x.imag)] for x in q],
            "probe_amplitudes": [float(meta[f"probe_amplitude_{i}"]) for i in range(3)],
            "native_mean_quadratic": [[float(x.real), float(x.imag)] for x in wide],
            "source_mean_quadratic": [[float(x.real), float(x.imag)] for x in expected],
            "source_u_mean": [[float(x.real), float(x.imag)] for x in reference_mean[:nz]],
            "source_u_mean_signal_norm": source_u_signal,
            "native_u_mean_signal_norm": native_u_signal,
            "u_mean_eos_pa_floor_norm": u_floor_norm,
            "component_acceptance": errors, "amplitude_scatter": scatter,
            "eos_pa_roundoff_floors": floors.tolist(),
            "branch_config": branch_metadata(meta, f"mean-probe-{nx}x{ny}x{nz}")}


def run_finite_upwind_probe(exe: Path, outdir: Path, grid: tuple[int, int, int],
                            d: dict, q: np.ndarray,
                            original_probe: dict) -> dict:
    """Isolate smooth vertical upwind by reversing velocity blocks only."""
    nx, ny, nz = grid
    offsets = packed_offsets(nx, ny, nz)
    original_direction = pack_mode(q, nx, ny, nz, offsets)
    flipped_direction = flip_velocity_blocks(original_direction, nx, ny, nz)
    flipped_path = outdir/f"upwind_velocity_flip_{nx}x{ny}x{nz}.txt"
    write_direction(flipped_path, flipped_direction)
    payload_path = outdir/f"upwind_velocity_flip_{nx}x{ny}x{nz}.csv"
    flipped_meta, flipped_arrays, _, _ = run_native(
        exe, nx, ny, nz, payload_path, ("--quadratic-probe", str(flipped_path)))
    flipped_branch = branch_metadata(flipped_meta, f"upwind-flip-{nx}x{ny}x{nz}")
    original_payload_path = outdir/original_probe["native_probe_payload"]
    original_meta, original_arrays, _, _ = native.read_payload(original_payload_path)
    sigma = float(flipped_meta["sign_smooth_delta_config"])
    if sigma != float(original_meta["sign_smooth_delta_config"]):
        raise AssertionError("smooth-sign delta changed between paired native probes")
    source_channels = {}
    # Extract only the odd-in-velocity terms in U/W. Other RHS blocks can be
    # odd under this operation and are intentionally outside this comparison.
    u_start, u_end = offsets[0], offsets[1]
    w_start, w_end = offsets[2], offsets[3]
    component_results = {"U": {}, "W": {}}
    amplitudes = []
    for level, amplitude in enumerate((
            float(original_meta[f"probe_amplitude_{i}"]) for i in range(3))):
        amplitudes.append(amplitude)
        original_rhs = original_arrays[f"rhs_plus_{level}"]
        flipped_rhs = flipped_arrays[f"rhs_plus_{level}"]
        u_orig = original_rhs[u_start:u_end].reshape(ny, nz, nx+1)[..., :nx]
        u_flip = flipped_rhs[u_start:u_end].reshape(ny, nz, nx+1)[..., :nx]
        w_orig = original_rhs[w_start:w_end].reshape(ny, nz+1, nx)[:, 1:]
        w_flip = flipped_rhs[w_start:w_end].reshape(ny, nz+1, nx)[:, 1:]
        anti_u = 0.5*(u_orig-u_flip)
        anti_w = 0.5*(w_orig-w_flip)
        source_plus = reference.finite_smooth_upwind_channels(
            d["column"], q, amplitude=amplitude, delta=sigma, velocity_sign=1.0)
        source_minus = reference.finite_smooth_upwind_channels(
            d["column"], q, amplitude=amplitude, delta=sigma, velocity_sign=-1.0)
        source_u = 0.5*(np.asarray(source_plus["u_upwind"])-
                        np.asarray(source_minus["u_upwind"]))
        source_w = 0.5*(np.asarray(source_plus["w_upwind"])-
                        np.asarray(source_minus["w_upwind"]))[1:]
        expected_u = np.broadcast_to(source_u[None, :, :], anti_u.shape)
        expected_w = np.broadcast_to(source_w[None, :, :], anti_w.shape)
        for name, actual, expected, operand_plus, operand_minus in (
                ("U", anti_u, expected_u, u_orig, u_flip),
                ("W", anti_w, expected_w, w_orig, w_flip)):
            error = actual-expected
            signal_norm = float(np.linalg.norm(expected))
            operand_norm = float(np.linalg.norm(operand_plus)+np.linalg.norm(operand_minus))
            absolute_floor = 128.0*np.finfo(np.float64).eps*operand_norm
            error_norm = float(np.linalg.norm(error))
            norm_budget = 1.0e-8*signal_norm+absolute_floor
            tolerance = (1.0e-8*np.abs(expected)+
                         128.0*np.finfo(np.float64).eps*
                         (np.abs(operand_plus)+np.abs(operand_minus)))
            max_error = float(np.max(np.abs(error)))
            if (not np.isfinite(error).all() or error_norm > norm_budget or
                    np.any(np.abs(error) > tolerance)):
                raise AssertionError(f"finite smooth-upwind {name} mismatch at amplitude {amplitude:g}: "
                                     f"L2 {error_norm:.6g}>{norm_budget:.6g}; max {max_error:.6g}; "
                                     f"per-cell operand floor {float(np.max(tolerance)):.6g}")
            if signal_norm <= absolute_floor:
                raise AssertionError(f"finite smooth-upwind {name} signal is below operand floor")
            component_results[name][str(amplitude)] = {
                "signal_norm": signal_norm,
                "error_l2": error_norm,
                "operand_roundoff_floor_l2": absolute_floor,
                "accepted_l2_budget": norm_budget,
                "relative_error": float(np.linalg.norm(error)/signal_norm),
                "max_abs_error": max_error,
                "native_anti_velocity": actual.tolist(),
                "source_anti_velocity": expected.tolist(),
                "native_operands_plus": operand_plus.tolist(),
                "native_operands_flipped": operand_minus.tolist(),
                "source_regime_positive": source_plus["metadata"],
                "source_regime_flipped": source_minus["metadata"],
            }
    return {"grid": list(grid), "sigma_from_native_metadata": sigma,
            "velocity_reversal_preserved_nonvelocity_inputs": True,
            "velocity_sign_isolation": "one half of native plus-RHS difference; U/W only",
            "component_results": component_results,
            "rayleigh_activation": {
                "wrf_damp_opt_config": flipped_branch["wrf_damp_opt_config"],
                "rayleigh_damp_coef_config": flipped_branch["rayleigh_damp_coef_config"],
                "rayleigh_damp_depth_config": flipped_branch["rayleigh_damp_depth_config"],
                "inactive_by_option": flipped_branch["wrf_damp_opt_config"] == 0.0,
                "activation_contract": "Rayleigh activates only for wrf_damp_opt==2 and positive coefficient",
            },
            "native_probe_payloads": {"original": original_probe["native_probe_payload"],
                                      "flipped": payload_path.name},
            "native_branch_config": flipped_branch}


def run_trajectory(exe: Path, outdir: Path, grid: tuple[int, int, int], d: dict,
                   q: np.ndarray) -> dict:
    nx, ny, nz = grid
    offsets = packed_offsets(nx, ny, nz)
    packed = pack_mode(q, nx, ny, nz, offsets)
    zero_path = outdir/f"zero_{nx}x{ny}x{nz}.txt"
    write_direction(zero_path, np.zeros_like(packed))
    source = reference.weak_trajectory(d["column"], q, np.asarray(TIMES, dtype=np.float64))
    smoothing = sign_smoothing_regime(
        d["column"], q, d["branch_config"]["sign_smooth_delta_config"])
    source_tightening = source["tightening"]
    for field in ("q0_relative", "q2_relative"):
        if not np.isfinite(source_tightening[field]) or source_tightening[field] > 1.0e-6:
            raise AssertionError(f"weak source {field} tightening {source_tightening[field]:.6g} > 1e-6")
    projector = lambda v: project_m2(v, nx, ny, nz, offsets)
    v_projector = lambda v: project_v_m2(v, nx, ny, nz, offsets)
    floors_at_time = {t: eos_pressure_floors(d["column"], nx, ny, nz, duration=t)
                      for t in TIMES}
    floors_per_second = eos_pressure_floors(d["column"], nx, ny, nz)
    coefficients: dict[tuple[float, float, float], np.ndarray] = {}
    signed_delta_states: dict[tuple[float, float, float, float], np.ndarray] = {}
    base_state_norms: dict[tuple[float, float], float] = {}
    trajectory_v_checks: dict[str, dict] = {}
    mass_checks: dict[str, dict] = {}
    theta_budget_actual: dict[tuple[float, float, float, float], dict] = {}
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
                base_state_norms[(dt, t)] = float(np.linalg.norm(base_arrays[name]))
                for sign in (-1.0, 1.0):
                    signed_delta_states[(dt, amp, sign, t)] = (
                        signed_states[sign][name]-base_arrays[name])
                coefficients[(dt, amp, t)] = centered_coefficient(
                    projector(signed_states[1.0][name]),
                    projector(signed_states[-1.0][name]),
                    projector(base_arrays[name]), amp)
                native_mu_base = closed_mass_mean(base_arrays[name], nx, ny, offsets)
                for sign in (-1.0, 1.0):
                    mu_mean = closed_mass_mean(signed_states[sign][name], nx, ny, offsets)
                    mass_anomaly = mu_mean-native_mu_base
                    mass_scale = max(abs(float(d["column"]["M_TOTAL"])),
                                     abs(native_mu_base), 1.0)
                    mass_floor = (32.0*np.finfo(np.float64).eps*mass_scale*(index+2))
                    if abs(mass_anomaly) > mass_floor:
                        raise AssertionError(f"closed mass mean drift {mass_anomaly:.6g} Pa > "
                                             f"FP64 state/flux floor {mass_floor:.6g} Pa")
                    mass_checks[f"dt{dt:g}_a{amp:g}_sign{sign:+g}_t{t:g}"] = {
                        "mean_mu_anomaly_pa": mass_anomaly,
                        "fp64_flux_state_floor_pa": mass_floor,
                        "other_mean_fields_are_not_constrained_to_zero": True,
                    }
                    theta_change, theta_floor = mass_weighted_theta_change(
                        signed_states[sign]["initial"], signed_states[sign][name],
                        signed_states[sign]["mubase"], d["column"], nx, ny, nz, offsets)
                    theta_budget_actual[(dt, amp, sign, t)] = {
                        "native_integral_change": theta_change,
                        "fp64_product_sum_floor": theta_floor,
                    }
        for index in range(steps):
            name = f"checkpoint_{index}"
            elapsed = (index+1)*dt
            v_floor = float(np.max(floors_per_second[:ny*nz]))*elapsed
            v_raw_receipt = {"amplitude_1_sign_-1": zero_mode_check(
                raw_v_block(signed_states_by_amp[1.0][-1.0][name], nx, ny, nz, offsets),
                v_floor, f"{nx}x{ny}x{nz} dt={dt:g} t={elapsed:g} raw V a=1 -"),
                "amplitude_1_sign_+1": zero_mode_check(
                raw_v_block(signed_states_by_amp[1.0][1.0][name], nx, ny, nz, offsets),
                v_floor, f"{nx}x{ny}x{nz} dt={dt:g} t={elapsed:g} raw V a=1 +"),
                "amplitude_half_sign_-1": zero_mode_check(
                raw_v_block(signed_states_by_amp[0.5][-1.0][name], nx, ny, nz, offsets),
                v_floor, f"{nx}x{ny}x{nz} dt={dt:g} t={elapsed:g} raw V a=0.5 -"),
                "amplitude_half_sign_+1": zero_mode_check(
                raw_v_block(signed_states_by_amp[0.5][1.0][name], nx, ny, nz, offsets),
                v_floor, f"{nx}x{ny}x{nz} dt={dt:g} t={elapsed:g} raw V a=0.5 +"),
                "base": zero_mode_check(raw_v_block(base_arrays[name], nx, ny, nz, offsets),
                                         v_floor, f"{nx}x{ny}x{nz} dt={dt:g} t={elapsed:g} raw V base")}
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
            trajectory_v_checks[f"dt{dt:g}_checkpoint_{index}"] = {
                "m2_projection": v_receipt, "full_raw_v_values": v_raw_receipt}
    times = {}
    weak_times = {}
    for time_index, t in enumerate(TIMES):
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
        src_q2 = tile_source_mode(source["q2"][time_index], ny, nz)
        floors = floors_at_time[t]
        errors = {}
        cursor = 0
        for name, count in component_layout(ny, nz):
            sl = slice(cursor, cursor+count)
            error_norm = float(np.linalg.norm(time_extrapolated[sl]-src_q2[sl]))
            signal_norm = float(np.linalg.norm(src_q2[sl]))
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
            "source": [[float(x.real), float(x.imag)] for x in src_q2],
            "amplitude_scatter_norm": float(np.linalg.norm(amp_uncertainty_vector)),
            "temporal_scatter_norm": float(np.linalg.norm(time_uncertainty)),
            "source_tightening_to_native_scatter": (
                source_tightening["q2_abs"] /
                max(float(np.linalg.norm(time_uncertainty)),
                    float(np.linalg.norm(amp_uncertainty_vector)),
                    float(np.linalg.norm(floors)), np.finfo(float).tiny)
                if t == TIMES[-1] else None),
            "component_acceptance": errors,
        }
        q1 = source["q1"][time_index]
        q0 = source["q0_mean"][time_index]
        q2 = source["q2"][time_index]
        native_mean_coefficients = {}
        for dt_value in (1.0, 0.5):
            for amp in (1.0, 0.5):
                plus_mean = project_harmonic_state(
                    signed_delta_states[(dt_value, amp, 1.0, t)], 0,
                    nx, ny, nz, offsets)
                minus_mean = project_harmonic_state(
                    signed_delta_states[(dt_value, amp, -1.0, t)], 0,
                    nx, ny, nz, offsets)
                native_mean_coefficients[(dt_value, amp)] = 0.5*(plus_mean+minus_mean)/(amp*amp)
        mean_amplitude_richardson = {
            dt_value: (4.0*native_mean_coefficients[(dt_value, 0.5)]-
                       native_mean_coefficients[(dt_value, 1.0)])/3.0
            for dt_value in (1.0, 0.5)}
        mean_amplitude_uncertainty = {
            dt_value: np.abs(native_mean_coefficients[(dt_value, 1.0)]-
                             native_mean_coefficients[(dt_value, 0.5)])/3.0
            for dt_value in (1.0, 0.5)}
        native_q0 = (8.0*mean_amplitude_richardson[0.5]-
                     mean_amplitude_richardson[1.0])/7.0
        q0_temporal_scatter = np.abs(mean_amplitude_richardson[1.0]-
                                     mean_amplitude_richardson[0.5])/7.0
        q0_amplitude_scatter = np.maximum(mean_amplitude_uncertainty[1.0],
                                          mean_amplitude_uncertainty[0.5])
        source_q0 = tile_source_mode(q0, ny, nz)
        q0_component_errors = {}
        cursor = 0
        for name, count in component_layout(ny, nz):
            sl = slice(cursor, cursor+count)
            absolute = float(np.linalg.norm(native_q0[sl]-source_q0[sl]))
            signal = float(np.linalg.norm(source_q0[sl]))
            dt_uncertainty = float(np.linalg.norm(q0_temporal_scatter[sl]))
            amp_uncertainty = float(np.linalg.norm(q0_amplitude_scatter[sl]))
            eos_floor = float(np.linalg.norm(floors_at_time[t][sl]))
            absolute_floor = max(eos_floor, float(source_tightening["q0_abs"]))
            budget = (ACTIVE_RELATIVE_BUDGET*signal+2.0*dt_uncertainty+
                      2.0*amp_uncertainty+absolute_floor)
            if not np.isfinite(absolute) or absolute > budget:
                raise AssertionError(f"trajectory {t:g}s m=0 {name} source error {absolute:.6g} > "
                                     f"relative+dt+amplitude+absolute budget {budget:.6g}")
            q0_component_errors[name] = {
                "absolute_error": absolute, "source_signal_norm": signal,
                "temporal_uncertainty_norm": dt_uncertainty,
                "amplitude_uncertainty_norm": amp_uncertainty,
                "absolute_floor_norm": absolute_floor, "accepted_error_budget": budget,
            }
            cursor += count
        source_theta_budget = source_mass_weighted_theta2(d["column"], q, q0, q1)
        source_theta_floor = source_mass_weighted_theta_floor(
            d["column"], source["q0_tightening_states"][time_index])
        weak_q0 = reconstruct_source_mode(q0, 0, nx, ny, nz, offsets)
        weak_q1 = reconstruct_source_mode(q1, 1, nx, ny, nz, offsets)
        weak_q2 = reconstruct_source_mode(q2, 2, nx, ny, nz, offsets)
        weak_residuals = {}
        for amp in (1.0, 0.5):
            native_at_dt = {}
            temporal_error = {}
            for sign in (-1.0, 1.0):
                coarse_step = signed_delta_states[(1.0, amp, sign, t)]
                fine_step = signed_delta_states[(0.5, amp, sign, t)]
                native_at_dt[sign] = (8.0*fine_step-coarse_step)/7.0
                temporal_error[sign] = float(np.linalg.norm(fine_step-coarse_step)/7.0)
            predicted_odd = amp*weak_q1
            predicted_even = amp*amp*(weak_q0+weak_q2)
            odd_native = 0.5*(native_at_dt[1.0]-native_at_dt[-1.0])
            even_native = 0.5*(native_at_dt[1.0]+native_at_dt[-1.0])
            odd_residual = odd_native-predicted_odd
            even_residual = even_native-predicted_even
            plus_residual = native_at_dt[1.0]-predicted_odd-predicted_even
            full_residual_norm = float(np.linalg.norm(plus_residual))
            temporal_floor = max(temporal_error.values())
            fp64_floor = 128.0*np.finfo(float).eps*max(
                base_state_norms[(1.0, t)], base_state_norms[(0.5, t)],
                float(np.linalg.norm(native_at_dt[1.0])),
                float(np.linalg.norm(predicted_odd+predicted_even)))
            harmonic_errors = {}
            for harmonic, predicted_mode in (
                    (0, amp*amp*tile_source_mode(q0, ny, nz)),
                    (1, amp*tile_source_mode(q1, ny, nz)),
                    (2, amp*amp*tile_source_mode(q2, ny, nz))):
                actual_mode = project_harmonic_state(native_at_dt[1.0], harmonic,
                                                     nx, ny, nz, offsets)
                harmonic_errors[str(harmonic)] = {
                    "absolute_error": float(np.linalg.norm(actual_mode-predicted_mode)),
                    "native_norm": float(np.linalg.norm(actual_mode)),
                    "source_prediction_norm": float(np.linalg.norm(predicted_mode)),
                }
            weak_residuals[str(amp)] = {
                "one_sided_full_state_residual_norm": full_residual_norm,
                "one_sided_residual_norm_divided_by_a3_diagnostic": full_residual_norm/(amp**3),
                "odd_residual_norm": float(np.linalg.norm(odd_residual)),
                "odd_residual_norm_divided_by_a3_diagnostic":
                    float(np.linalg.norm(odd_residual)/(amp**3)),
                "even_residual_norm": float(np.linalg.norm(even_residual)),
                "even_residual_norm_divided_by_a4_diagnostic":
                    float(np.linalg.norm(even_residual)/(amp**4)),
                "temporal_uncertainty_plus_norm": temporal_error[1.0],
                "temporal_uncertainty_minus_norm": temporal_error[-1.0],
                "observed_temporal_scatter_floor": temporal_floor,
                "fp64_state_construction_floor": fp64_floor,
                "residual_resolved_above_numerical_floor": full_residual_norm > temporal_floor+fp64_floor,
                "harmonic_projection_errors": harmonic_errors,
                "mass_weighted_theta_budget": {
                    f"dt{dt:g}_sign{sign:+g}": {
                        **theta_budget_actual[(dt, amp, sign, t)],
                        "source_q0_q2_prediction": amp*amp*source_theta_budget,
                        "source_tightening_floor": amp*amp*source_theta_floor,
                        "difference_from_source": (
                            theta_budget_actual[(dt, amp, sign, t)]["native_integral_change"]
                            - amp*amp*source_theta_budget),
                    }
                    for dt in (1.0, 0.5) for sign in (-1.0, 1.0)},
            }
        residual_full = weak_residuals["1.0"]["one_sided_full_state_residual_norm"]
        residual_half = weak_residuals["0.5"]["one_sided_full_state_residual_norm"]
        if (weak_residuals["1.0"]["residual_resolved_above_numerical_floor"] and
                weak_residuals["0.5"]["residual_resolved_above_numerical_floor"] and
                residual_full > 0.0 and residual_half > 0.0):
            measured_degree = float(np.log(residual_full/residual_half)/np.log(2.0))
        else:
            measured_degree = None
        q0_mu_source = complex(q0[-1])
        mass_source_floor = 128.0*np.finfo(np.float64).eps*max(
            abs(float(d["column"]["M_TOTAL"])), 1.0)
        if abs(q0_mu_source) > mass_source_floor:
            raise AssertionError(f"closed source mean-MU tendency {q0_mu_source} exceeds "
                                 f"FP64 source floor {mass_source_floor:.6g} Pa/s")
        weak_times[str(int(t))] = {
            "q0_mean_source": [[float(x.real), float(x.imag)] for x in q0],
            "native_q0_mean_time_richardson": [[float(x.real), float(x.imag)]
                                                for x in native_q0],
            "native_q0_mean_amplitude_richardson_dt1": [
                [float(x.real), float(x.imag)] for x in mean_amplitude_richardson[1.0]],
            "native_q0_mean_amplitude_richardson_dt_half": [
                [float(x.real), float(x.imag)] for x in mean_amplitude_richardson[0.5]],
            "q0_component_acceptance": q0_component_errors,
            "measured_amplitude_degree_of_full_residual_if_resolved": measured_degree,
            "degree_interpretation": "measured only above temporal/FP64 floors; sign smoothing sets regime",
            "q1_source": [[float(x.real), float(x.imag)] for x in q1],
            "q2_source": [[float(x.real), float(x.imag)] for x in q2],
            "source_mean_mu_expected_zero": {"value": [q0_mu_source.real, q0_mu_source.imag],
                                               "fp64_floor_pa_per_second": mass_source_floor},
            "source_mass_weighted_theta_q0_q2_budget": source_theta_budget,
            "source_mass_weighted_theta_tightening_floor": source_theta_floor,
            "finite_amplitude_residuals": weak_residuals,
            "unconstrained_non_mass_mean_fields": ["U", "W", "PH", "THETA"],
        }
    return {"grid": list(grid), "dt_values": [1.0, 0.5], "physical_duration": 30.0,
            "temporal_order_assumed": 3, "runs": run_receipts,
            "V_zero_projection_checks": trajectory_v_checks,
            "closed_mass_mean_checks": mass_checks,
            "sign_smoothing_regime": smoothing,
            "source_integration": source_tightening,
            "weak_nonlinear_q0_q1_q2": weak_times, "times": times}


def run_long_weak_prediction(exe: Path, outdir: Path, grid: tuple[int, int, int],
                             d: dict, q: np.ndarray) -> dict:
    """Compare one-sided finite-amplitude forecasts at 300/600/900 s, N<=360."""
    nx, ny, nz = grid
    offsets = packed_offsets(nx, ny, nz)
    packed = pack_mode(q, nx, ny, nz, offsets)
    endpoints = (300.0, 600.0, 900.0)
    source = reference.weak_trajectory(d["column"], q, np.asarray(endpoints, dtype=np.float64))
    smoothing = sign_smoothing_regime(
        d["column"], q, d["branch_config"]["sign_smooth_delta_config"])
    tightening = source["tightening"]
    if any(not np.isfinite(tightening[key]) or tightening[key] > 1.0e-6
           for key in ("q0_relative", "q2_relative")):
        raise AssertionError(f"long weak source integration tightening failed: {tightening}")
    zero_path = outdir/f"long_zero_{nx}x{ny}x{nz}.txt"
    write_direction(zero_path, np.zeros_like(packed))
    # A 2:1 pair resolves the acoustic path while remaining within N<=360.
    steps_by_dt = LONG_DT_STEPS
    dt_coarse, dt_fine = sorted(steps_by_dt, reverse=True)
    states: dict[tuple[float, float, float], np.ndarray] = {}
    base_state_norms: dict[tuple[float, float], float] = {}
    base_endpoint_states: dict[tuple[float, float], np.ndarray] = {}
    trajectory_initial_states: dict[tuple[float, float], np.ndarray] = {}
    theta_budget_actual: dict[tuple[float, float, float], dict] = {}
    physical_domain = {}
    receipts = []
    for dt, steps in steps_by_dt.items():
        if steps > 360 or steps*dt > 900.0:
            raise AssertionError("long forecast exceeds the native fixed-trajectory contract")
        base_path = outdir/f"long_base_{nx}_dt{dt:g}.csv"
        base_meta, base_arrays, _, _ = run_native(exe, nx, ny, nz, base_path,
            ("--quadratic-forward", str(zero_path), str(steps), str(dt)))
        if int(base_meta["trajectory_steps"]) != steps:
            raise AssertionError("native long baseline did not retain all requested steps")
        trajectory_initial_states[(dt, 0.0)] = base_arrays["initial"].copy()
        receipts.append({"dt": dt, "amplitude": 0.0, "steps": steps,
                         "duration_seconds": steps*dt, "payload": base_path.name,
                         "initial_state_sha256": hashlib.sha256(
                             np.asarray(base_arrays["initial"], dtype=np.float64).tobytes()).hexdigest()})
        for amp in (1.0, 0.5):
            direction_path = outdir/f"long_direction_{nx}_dt{dt:g}_a{amp:g}.txt"
            write_direction(direction_path, amp*packed)
            path = outdir/f"long_trajectory_{nx}_dt{dt:g}_a{amp:g}.csv"
            meta, arrays, _, _ = run_native(exe, nx, ny, nz, path,
                ("--quadratic-forward", str(direction_path), str(steps), str(dt)))
            if int(meta["trajectory_steps"]) != steps:
                raise AssertionError("native long trajectory did not retain all requested steps")
            trajectory_initial_states[(dt, amp)] = arrays["initial"].copy()
            receipts.append({"dt": dt, "amplitude": amp, "steps": steps,
                             "duration_seconds": steps*dt, "payload": path.name,
                             "initial_state_sha256": hashlib.sha256(
                                 np.asarray(arrays["initial"], dtype=np.float64).tobytes()).hexdigest()})
            domain_minima = {key: float("inf") for key in (
                "full_column_mass_pa", "theta_k", "total_geopotential_thickness",
                "inverse_density_alpha", "pressure_pa", "density")}
            invalid_at = None
            first_invalid_checks = None
            all_finite = True
            for index in range(steps):
                checkpoint = arrays[f"checkpoint_{index}"]
                elapsed = (index+1)*dt
                domain_check = native_physical_domain(
                    checkpoint, arrays["phb"], arrays["mubase"], d["column"],
                    nx, ny, nz, offsets)
                all_finite = all_finite and domain_check["finite_input"]
                for key, value in domain_check["minima"].items():
                    if np.isfinite(value):
                        domain_minima[key] = min(domain_minima[key], value)
                if not domain_check["valid"] and invalid_at is None:
                    invalid_at = elapsed
                    first_invalid_checks = domain_check
            physical_domain[f"dt{dt:g}_a{amp:g}"] = {
                "minimum_values_over_all_checkpoints": domain_minima,
                "all_checkpoints_finite": all_finite,
                "checks": ["all packed state values finite", "MUB+MU>0", "300+theta_pert>0",
                           "diff(PHB+PH)>0", "alpha>0", "dry EOS pressure>0", "rho>0"],
                "valid_until_end": invalid_at is None,
                "first_invalid_time_seconds": invalid_at,
                "first_invalid_values": first_invalid_checks,
            }
            for endpoint in endpoints:
                step_index = int(round(endpoint/dt))-1
                if abs((step_index+1)*dt-endpoint) > 1.0e-9:
                    raise AssertionError(f"endpoint {endpoint:g}s is not a checkpoint for dt={dt:g}")
                name = f"checkpoint_{step_index}"
                base_state_norms[(dt, endpoint)] = float(np.linalg.norm(base_arrays[name]))
                base_endpoint_states[(dt, endpoint)] = base_arrays[name].copy()
                states[(dt, amp, endpoint)] = arrays[name]-base_arrays[name]
                theta_change, theta_floor = mass_weighted_theta_change(
                    arrays["initial"], arrays[name], arrays["mubase"],
                    d["column"], nx, ny, nz, offsets)
                theta_budget_actual[(dt, amp, endpoint)] = {
                    "native_integral_change": theta_change,
                    "fp64_product_sum_floor": theta_floor,
                }
    for amp in (0.0, 1.0, 0.5):
        if not np.array_equal(trajectory_initial_states[(dt_coarse, amp)],
                              trajectory_initial_states[(dt_fine, amp)]):
            raise AssertionError(f"long dt pair does not use an identical initial state for a={amp:g}")
    # A T=300 s h=1.25/N=240 run checks the cached h=5/h=2.5 pair at the
    # same coarse grid, controls, and initial states; it does not extend T900.
    dt_check, steps_check, endpoint_check = 1.25, 240, 300.0
    check_base_path = outdir/f"long_base_{nx}_dt{dt_check:g}_t300.csv"
    check_base_meta, check_base, _, _ = run_native(exe, nx, ny, nz, check_base_path,
        ("--quadratic-forward", str(zero_path), str(steps_check), str(dt_check)))
    if int(check_base_meta["trajectory_steps"]) != steps_check:
        raise AssertionError("h=1.25 T300 baseline did not retain all requested steps")
    if (not np.array_equal(check_base["initial"], trajectory_initial_states[(dt_coarse, 0.0)]) or
            not np.array_equal(check_base["initial"], trajectory_initial_states[(dt_fine, 0.0)])):
        raise AssertionError("h=1.25 T300 baseline differs from the h=5/h=2.5 initial profile")
    trajectory_initial_states[(dt_check, 0.0)] = check_base["initial"].copy()
    receipts.append({"dt": dt_check, "amplitude": 0.0, "steps": steps_check,
                     "duration_seconds": steps_check*dt_check, "payload": check_base_path.name,
                     "initial_state_sha256": hashlib.sha256(
                         np.asarray(check_base["initial"], dtype=np.float64).tobytes()).hexdigest()})
    for amp in (1.0, 0.5):
        direction_path = outdir/f"long_direction_{nx}_dt{dt_check:g}_a{amp:g}.txt"
        write_direction(direction_path, amp*packed)
        path = outdir/f"long_trajectory_{nx}_dt{dt_check:g}_a{amp:g}_t300.csv"
        meta, arrays, _, _ = run_native(exe, nx, ny, nz, path,
            ("--quadratic-forward", str(direction_path), str(steps_check), str(dt_check)))
        if int(meta["trajectory_steps"]) != steps_check:
            raise AssertionError("h=1.25 T300 trajectory did not retain all requested steps")
        if (not np.array_equal(arrays["initial"], trajectory_initial_states[(dt_coarse, amp)]) or
                not np.array_equal(arrays["initial"], trajectory_initial_states[(dt_fine, amp)])):
            raise AssertionError(f"h=1.25 T300 initial profile differs for a={amp:g}")
        trajectory_initial_states[(dt_check, amp)] = arrays["initial"].copy()
        receipts.append({"dt": dt_check, "amplitude": amp, "steps": steps_check,
                         "duration_seconds": steps_check*dt_check, "payload": path.name,
                         "initial_state_sha256": hashlib.sha256(
                             np.asarray(arrays["initial"], dtype=np.float64).tobytes()).hexdigest()})
        invalid_time = None
        for index in range(steps_check):
            domain_check = native_physical_domain(
                arrays[f"checkpoint_{index}"], arrays["phb"], arrays["mubase"],
                d["column"], nx, ny, nz, offsets)
            if not domain_check["valid"]:
                invalid_time = (index+1)*dt_check
                break
        physical_domain[f"dt{dt_check:g}_a{amp:g}"] = {
            "valid_until_end": invalid_time is None,
            "first_invalid_time_seconds": invalid_time,
            "same_initial_as_long_pair": True,
        }
        if invalid_time is not None:
            raise AssertionError(f"h=1.25 T300 state left physical domain at t={invalid_time:g}s")
        index_check = int(endpoint_check/dt_check)-1
        check_name = f"checkpoint_{index_check}"
        states[(dt_check, amp, endpoint_check)] = (
            arrays[check_name]-check_base[f"checkpoint_{index_check}"])
        base_state_norms[(dt_check, endpoint_check)] = float(
            np.linalg.norm(check_base[f"checkpoint_{index_check}"]))
        base_endpoint_states[(dt_check, endpoint_check)] = check_base[
            f"checkpoint_{index_check}"].copy()
        theta_change, theta_floor = mass_weighted_theta_change(
            arrays["initial"], arrays[check_name], arrays["mubase"],
            d["column"], nx, ny, nz, offsets)
        theta_budget_actual[(dt_check, amp, endpoint_check)] = {
            "native_integral_change": theta_change,
            "fp64_product_sum_floor": theta_floor,
        }
    invalid_domains = {key: record for key, record in physical_domain.items()
                       if not record["valid_until_end"]}
    if invalid_domains:
        failure = {"physical_domain": physical_domain,
                   "invalid_amplitude_timestep_cases": invalid_domains,
                   "inputs_receipt": "wave_quadratic_inputs_receipt.json"}
        (outdir/"long_weak_domain_failure.json").write_text(
            json.dumps(failure, indent=2, sort_keys=True)+"\n")
        raise AssertionError(f"long native forecast left the valid dry EOS domain: {invalid_domains}")
    weak_times = {}
    for ti, endpoint in enumerate(endpoints):
        q0 = source["q0_mean"][ti]
        q1 = source["q1"][ti]
        q2 = source["q2"][ti]
        source_theta_budget = source_mass_weighted_theta2(d["column"], q, q0, q1)
        source_theta_floor = source_mass_weighted_theta_floor(
            d["column"], source["q0_tightening_states"][ti])
        expected_linear = reconstruct_source_mode(q1, 1, nx, ny, nz, offsets)
        expected_even = (reconstruct_source_mode(q0, 0, nx, ny, nz, offsets)+
                         reconstruct_source_mode(q2, 2, nx, ny, nz, offsets))
        quadratic_gates = {}
        state_floors = eos_pressure_floors(d["column"], nx, ny, nz,
                                            duration=endpoint)
        gate_dts = ((dt_coarse, dt_fine, dt_check) if endpoint == endpoint_check
                    else (dt_coarse, dt_fine))
        time_pairs = tuple(zip(gate_dts[:-1], gate_dts[1:]))
        for harmonic, source_mode in ((0, q0), (2, q2)):
            expected_mode = tile_source_mode(source_mode, ny, nz)
            coefficient_by_dt = {}
            amplitude_uncertainty_by_dt = {}
            for dt in gate_dts:
                coeffs_by_amplitude = {}
                for amp in (1.0, 0.5):
                    coeffs_by_amplitude[amp] = project_harmonic_state(
                        states[(dt, amp, endpoint)], harmonic, nx, ny, nz, offsets)/(amp*amp)
                coefficient_by_dt[dt] = (4.0*coeffs_by_amplitude[0.5]-
                                          coeffs_by_amplitude[1.0])/3.0
                amplitude_uncertainty_by_dt[dt] = np.abs(
                    coeffs_by_amplitude[1.0]-coeffs_by_amplitude[0.5])/3.0
            time_scatter_by_pair = {
                f"{dt0:g}_vs_{dt1:g}": np.abs(coefficient_by_dt[dt1]-coefficient_by_dt[dt0])
                for dt0, dt1 in time_pairs}
            time_scatter = np.maximum.reduce(list(time_scatter_by_pair.values()))
            amplitude_scatter = np.maximum.reduce(
                [amplitude_uncertainty_by_dt[dt] for dt in gate_dts])
            block_gates = {}
            cursor = 0
            for name, count in component_layout(ny, nz):
                sl = slice(cursor, cursor+count)
                signal = float(np.linalg.norm(expected_mode[sl]))
                time_uncertainty = float(np.linalg.norm(time_scatter[sl]))
                amp_uncertainty = float(np.linalg.norm(amplitude_scatter[sl]))
                absolute_floor = float(np.linalg.norm(state_floors[sl]))
                resolved = signal > (time_uncertainty+amp_uncertainty+absolute_floor)
                if signal > absolute_floor and not resolved:
                    raise AssertionError(f"long m={harmonic} {name} signal is below observed "
                                         f"time/amplitude scatter at {endpoint:g}s")
                dt_errors = {}
                for dt in gate_dts:
                    error = float(np.linalg.norm(coefficient_by_dt[dt][sl]-expected_mode[sl]))
                    budget = (ACTIVE_RELATIVE_BUDGET*signal+2.0*time_uncertainty+
                              2.0*amp_uncertainty+absolute_floor)
                    if not np.isfinite(error) or error > budget:
                        raise AssertionError(f"long m={harmonic} {name} coefficient error "
                                             f"{error:.6g} > time/amplitude/source budget {budget:.6g}")
                    dt_errors[str(dt)] = {"absolute_error": error,
                                          "accepted_error_budget": budget}
                block_gates[name] = {
                    "source_signal_norm": signal, "time_scatter_norm": time_uncertainty,
                    "amplitude_scatter_norm": amp_uncertainty,
                    "absolute_floor_norm": absolute_floor,
                    "resolved_above_observed_scatter": resolved,
                    "errors_by_dt": dt_errors,
                }
                cursor += count
            quadratic_gates[str(harmonic)] = {
                "native_amplitude_richardson_by_dt": {
                    str(dt): [[float(x.real), float(x.imag)] for x in coefficient_by_dt[dt]]
                    for dt in gate_dts},
                "time_scatter_by_dt_pair": {
                    pair: [[float(x.real), float(x.imag)] for x in vector]
                    for pair, vector in time_scatter_by_pair.items()},
                "time_scatter_vector": [[float(x.real), float(x.imag)] for x in time_scatter],
                "amplitude_scatter_vector": [[float(x.real), float(x.imag)]
                                              for x in amplitude_scatter],
                "source_vector": [[float(x.real), float(x.imag)] for x in expected_mode],
                "component_acceptance": block_gates,
            }
        finite_upwind_sources = {}
        if endpoint == endpoint_check:
            delta = float(d["branch_config"]["sign_smooth_delta_config"])
            for amp in (1.0, 0.5):
                finite_upwind_sources[amp] = reference.propagate_finite_smooth_upwind(
                    d["column"], q, amplitude=amp,
                    times=np.asarray([endpoint], dtype=np.float64), delta=delta)
        compare_dts = ((dt_coarse, dt_fine, dt_check) if endpoint == endpoint_check
                       else (dt_coarse, dt_fine))
        time_pairs = tuple(zip(compare_dts[:-1], compare_dts[1:]))
        block_slices = packed_block_slices(nx, ny, nz)
        amplitudes = {}
        for amp in (1.0, 0.5):
            time_scatter_by_pair = {
                f"{dt0:g}_vs_{dt1:g}": raw_packed_temporal_scatter(
                    states[(dt0, amp, endpoint)], states[(dt1, amp, endpoint)], nx, ny, nz)
                for dt0, dt1 in time_pairs}
            observed_temporal_ratio = {}
            if endpoint == endpoint_check:
                coarse_pair = time_scatter_by_pair[f"{dt_coarse:g}_vs_{dt_fine:g}"]["blocks"]
                fine_pair = time_scatter_by_pair[f"{dt_fine:g}_vs_{dt_check:g}"]["blocks"]
                for name in PACKED_BLOCK_NAMES:
                    numerator = coarse_pair[name]["raw_packed_l2"]
                    denominator = fine_pair[name]["raw_packed_l2"]
                    observed_temporal_ratio[name] = (numerator/denominator
                        if denominator > 0.0 else None)
            scatter_by_block = {
                name: max(record["blocks"][name]["raw_packed_l2"]
                          for record in time_scatter_by_pair.values())
                for name in PACKED_BLOCK_NAMES}
            evaluation_dt = dt_check if endpoint == endpoint_check else dt_fine
            fine = states[(evaluation_dt, amp, endpoint)]
            prediction = amp*expected_linear+amp*amp*expected_even
            linear_prediction = amp*expected_linear
            block_errors = {}
            for name, block in block_slices.items():
                temporal_scatter = scatter_by_block[name]
                fp64_floor = 128.0*np.finfo(float).eps*max(
                    *(base_state_norms[(dt, endpoint)] for dt in compare_dts),
                    float(np.linalg.norm(fine[block])),
                    float(np.linalg.norm(prediction[block])))
                linear_error = float(np.linalg.norm(fine[block]-linear_prediction[block]))
                weak_error = float(np.linalg.norm(fine[block]-prediction[block]))
                improvement = linear_error-weak_error
                correction_signal = float(np.linalg.norm(
                    (prediction-linear_prediction)[block]))
                uncertainty_floor = temporal_scatter+fp64_floor
                resolved = correction_signal > uncertainty_floor
                if amp == 1.0 and resolved and improvement <= uncertainty_floor:
                    raise AssertionError(f"long Q0+Q2 {name} block did not improve over linear-only "
                                         f"beyond raw temporal/FP64 scatter at {endpoint:g}s")
                block_errors[name] = {
                    "unit": PACKED_BLOCK_UNITS[name], "storage": PACKED_BLOCK_STORAGE[name],
                    "raw_storage_count": block.stop-block.start,
                    "weak_full_state_residual_raw_l2": weak_error,
                    "weak_residual_divided_by_a3_diagnostic": weak_error/(amp**3),
                    "linear_only_residual_raw_l2": linear_error,
                    "improvement_over_linear_raw_l2": improvement,
                    "source_second_order_correction_raw_l2": correction_signal,
                    "raw_temporal_scatter_floor": temporal_scatter,
                    "fp64_state_construction_floor": fp64_floor,
                    "correction_resolved_above_scatter": resolved,
                    "improvement_gate_passed": (improvement > uncertainty_floor if resolved else None),
                }
            finite_upwind_diagnostic = None
            if endpoint == endpoint_check:
                finite_source = finite_upwind_sources[amp]
                upwind_full = np.zeros_like(fine)
                upwind_tight = np.zeros_like(fine)
                for harmonic, correction in finite_source["state_correction"].items():
                    upwind_full += reconstruct_source_mode(
                        correction[0], harmonic, nx, ny, nz, offsets)
                    upwind_tight += reconstruct_source_mode(
                        finite_source["tightening_states"][harmonic][0],
                        harmonic, nx, ny, nz, offsets)
                actual_physical = physical_blocks(fine, nx, ny, nz, offsets)
                weak_physical = physical_blocks(prediction, nx, ny, nz, offsets)
                upwind_physical = physical_blocks(upwind_full, nx, ny, nz, offsets)
                upwind_tight_physical = physical_blocks(upwind_tight, nx, ny, nz, offsets)
                corrected_physical = weak_physical+upwind_physical
                temporal_physical = (physical_blocks(states[(dt_fine, amp, endpoint)],
                                                      nx, ny, nz, offsets)
                                     -physical_blocks(states[(dt_check, amp, endpoint)],
                                                      nx, ny, nz, offsets))
                baseline_physical = physical_blocks(
                    base_endpoint_states[(dt_check, endpoint)], nx, ny, nz, offsets)
                finite_component_results = {}
                physical_cursor = 0
                for name, count in component_layout(ny*nx, nz):
                    sl = slice(physical_cursor, physical_cursor+count)
                    actual_error = float(np.linalg.norm(actual_physical[sl]-weak_physical[sl]))
                    corrected_error = float(np.linalg.norm(actual_physical[sl]-corrected_physical[sl]))
                    correction_signal = float(np.linalg.norm(upwind_physical[sl]))
                    temporal_floor = float(np.linalg.norm(temporal_physical[sl]))
                    source_floor = float(np.linalg.norm(upwind_tight_physical[sl]))
                    fp64_floor = 128.0*np.finfo(float).eps*max(
                        float(np.linalg.norm(baseline_physical[sl])),
                        float(np.linalg.norm(actual_physical[sl])),
                        float(np.linalg.norm(corrected_physical[sl])))
                    uncertainty = temporal_floor+source_floor+fp64_floor
                    resolved = correction_signal > uncertainty
                    improvement = actual_error-corrected_error
                    if resolved:
                        status = "improved" if improvement > uncertainty else "resolved_without_improvement"
                    else:
                        status = "open_unresolved"
                    finite_component_results[name] = {
                        "unit": PACKED_BLOCK_UNITS[name],
                        "owned_physical_raw_l2_norm": True,
                        "native_q0_q1_q2_residual": actual_error,
                        "native_q0_q1_q2_plus_upwind_residual": corrected_error,
                        "upwind_correction_signal": correction_signal,
                        "observed_h2.5_vs_h1.25_scatter": temporal_floor,
                        "source_bank_tightening_floor": source_floor,
                        "fp64_state_construction_floor": fp64_floor,
                        "resolved_above_uncertainty": resolved,
                        "improvement_over_Q0_Q1_Q2": improvement,
                        "status": status,
                        "native_delta": actual_physical[sl].tolist(),
                        "weak_prediction": weak_physical[sl].tolist(),
                        "source_upwind_correction": upwind_physical[sl].tolist(),
                        "weak_plus_upwind_prediction": corrected_physical[sl].tolist(),
                    }
                    physical_cursor += count
                finite_upwind_diagnostic = {
                    "source_integrator_tightening_relative": float(
                        finite_source["tightening_relative"]),
                    "source_delta": float(finite_source["metadata"]["sign_smooth_delta"]),
                    "source_regime": finite_source["metadata"],
                    "harmonic_state_correction_source_order": {
                        str(harmonic): [[float(x.real), float(x.imag)] for x in values[0]]
                        for harmonic, values in finite_source["state_correction"].items()},
                    "source_tightening_state_by_harmonic": {
                        str(harmonic): [[float(x.real), float(x.imag)] for x in
                                        finite_source["tightening_states"][harmonic][0]]
                        for harmonic in finite_source["state_correction"]},
                    "component_comparisons": finite_component_results,
                    "source_scope": "finite vertical smooth-upwind contribution only; not complete O(a^3) closure",
                }
            harmonic_errors = {}
            for harmonic, source_mode in ((0, amp*amp*q0), (1, amp*q1), (2, amp*amp*q2)):
                native_mode = project_harmonic_state(fine, harmonic, nx, ny, nz, offsets)
                predicted_mode = tile_source_mode(source_mode, ny, nz)
                harmonic_errors[str(harmonic)] = {
                    "absolute_error": float(np.linalg.norm(native_mode-predicted_mode)),
                    "native_norm": float(np.linalg.norm(native_mode)),
                    "source_prediction_norm": float(np.linalg.norm(predicted_mode)),
                }
            amplitudes[str(amp)] = {
                "evaluation_dt": evaluation_dt,
                "temporal_scatter_by_dt_pair": time_scatter_by_pair,
                "observed_temporal_scatter_ratio_by_raw_block": observed_temporal_ratio,
                "raw_endpoint_packed_delta_by_dt": {
                    str(dt): states[(dt, amp, endpoint)].tolist() for dt in compare_dts},
                "initial_state_sha256_by_dt": {
                    str(dt): hashlib.sha256(trajectory_initial_states[(dt, amp)].tobytes()).hexdigest()
                    for dt in compare_dts},
                "initial_state_identical_across_dt": all(
                    np.array_equal(trajectory_initial_states[compare_dts[0], amp],
                                   trajectory_initial_states[dt, amp])
                    for dt in compare_dts[1:]),
                "blockwise_weak_vs_linear_diagnostics": block_errors,
                "harmonic_projection_errors": harmonic_errors,
                "finite_upwind_source_comparison": finite_upwind_diagnostic,
                "domain_valid_by_dt": {
                    str(dt): physical_domain[f"dt{dt:g}_a{amp:g}"]["valid_until_end"]
                    for dt in compare_dts},
                "comparison_in_valid_native_domain": all(
                    physical_domain[f"dt{dt:g}_a{amp:g}"]["valid_until_end"]
                    for dt in compare_dts),
                "mass_weighted_theta_budget": {
                    str(dt): {
                        **theta_budget_actual[(dt, amp, endpoint)],
                        "source_q0_q2_prediction": amp*amp*source_theta_budget,
                        "source_tightening_floor": amp*amp*source_theta_floor,
                        "difference_from_source": (
                            theta_budget_actual[(dt, amp, endpoint)]["native_integral_change"]
                            - amp*amp*source_theta_budget),
                    }
                    for dt in compare_dts},
            }
        measured_degree = {}
        for name in PACKED_BLOCK_NAMES:
            full = amplitudes["1.0"]["blockwise_weak_vs_linear_diagnostics"][name]
            half = amplitudes["0.5"]["blockwise_weak_vs_linear_diagnostics"][name]
            if (full["weak_full_state_residual_raw_l2"] > 0.0 and
                    half["weak_full_state_residual_raw_l2"] > 0.0 and
                    full["weak_full_state_residual_raw_l2"] >
                    full["raw_temporal_scatter_floor"]+full["fp64_state_construction_floor"] and
                    half["weak_full_state_residual_raw_l2"] >
                    half["raw_temporal_scatter_floor"]+half["fp64_state_construction_floor"]):
                measured_degree[name] = float(np.log(
                    full["weak_full_state_residual_raw_l2"]/
                    half["weak_full_state_residual_raw_l2"])/np.log(2.0))
            else:
                measured_degree[name] = None
        weak_times[str(int(endpoint))] = {
            "q0_mean_source": [[float(x.real), float(x.imag)] for x in q0],
            "q1_source": [[float(x.real), float(x.imag)] for x in q1],
            "q2_source": [[float(x.real), float(x.imag)] for x in q2],
            "source_mass_weighted_theta_q0_q2_budget": source_theta_budget,
            "source_mass_weighted_theta_tightening_floor": source_theta_floor,
            "quadratic_harmonic_gates": quadratic_gates,
            "measured_amplitude_degree_by_raw_block_if_resolved": measured_degree,
            "degree_interpretation": "empirical per raw block only above temporal/FP64 floors; no global norm/order claim",
            "amplitudes": amplitudes,
        }
    return {"grid": list(grid), "endpoints_seconds": list(endpoints),
            "dt_steps": {str(dt): steps for dt, steps in steps_by_dt.items()},
            "temporal_order_claimed": False,
            "source_tightening": tightening, "physical_domain": physical_domain,
            "sign_smoothing_regime": smoothing,
            "runs": receipts, "weak_nonlinear_q0_q1_q2": weak_times}


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
    half_direction_path = outdir/"dq2_adjoint_a0.5.txt"
    write_direction(half_direction_path, 0.5*packed0)
    half_path = outdir/"dq2_adjoint_a0.5.csv"
    _, half_arrays, _, _ = run_native(exe, nx, ny, nz, half_path,
        ("--quadratic-trajectory", str(half_direction_path), str(steps), str(dt)))
    dt_half, steps_half = 0.5, 12
    fine_vjps = {}
    for amp in (1.0, 0.5):
        direction_path = outdir/f"dq2_adjoint_dt0.5_a{amp:g}.txt"
        write_direction(direction_path, amp*packed0)
        path = outdir/f"dq2_adjoint_dt0.5_a{amp:g}.csv"
        _, arrays, _, _ = run_native(exe, nx, ny, nz, path,
            ("--quadratic-trajectory", str(direction_path), str(steps_half), str(dt_half)))
        fine_vjps[amp] = arrays
    for payload in (half_arrays, *fine_vjps.values()):
        if not np.array_equal(arrays0["terminal_cotangent"], payload["terminal_cotangent"]):
            raise AssertionError("native m=2 terminal cotangent changed across amplitude/timestep")
    weak_times = np.asarray([steps*dt], dtype=np.float64)
    source_sensitivity = reference.propagate_q2_sensitivity(
        d["column"], q0, dq, weak_times)
    if (not np.isfinite(source_sensitivity["tightening_relative"]) or
            source_sensitivity["tightening_relative"] > 1.0e-6):
        raise AssertionError("polarized DQ2 source sensitivity did not meet the DOP853 tightening gate")
    source_sensitivity_mode = reconstruct_source_mode(
        source_sensitivity["dq2"][0], 2, nx, ny, nz, offsets)
    source_sensitivity_objective = float(np.dot(
        arrays0["terminal_cotangent"], source_sensitivity_mode))
    native_sensitivity = {
        (1.0, 1.0): float(np.dot(pullback, packed_delta)),
        (1.0, 0.5): float(np.dot(half_arrays["initial_pullback"], packed_delta)/0.5),
        (0.5, 1.0): float(np.dot(fine_vjps[1.0]["initial_pullback"], packed_delta)),
        (0.5, 0.5): float(np.dot(fine_vjps[0.5]["initial_pullback"], packed_delta)/0.5),
    }
    amplitude_richardson = {
        dt_value: (4.0*native_sensitivity[(dt_value, 0.5)]-
                   native_sensitivity[(dt_value, 1.0)])/3.0
        for dt_value in (1.0, 0.5)}
    amplitude_scatter = {
        dt_value: abs(native_sensitivity[(dt_value, 1.0)]-
                      native_sensitivity[(dt_value, 0.5)])/3.0
        for dt_value in (1.0, 0.5)}
    temporal_richardson_sensitivity = (8.0*amplitude_richardson[0.5]-
                                       amplitude_richardson[1.0])/7.0
    temporal_scatter_sensitivity = abs(amplitude_richardson[1.0]-
                                       amplitude_richardson[0.5])/7.0
    source_tightening_mode = reconstruct_source_mode(
        source_sensitivity["tightening_states"][0], 2, nx, ny, nz, offsets)
    source_sensitivity_floor = abs(float(np.dot(
        arrays0["terminal_cotangent"], source_tightening_mode)))
    fp64_sensitivity_floor = 128.0*np.finfo(float).eps*max(
        float(np.sum(np.abs(pullback*packed_delta), dtype=np.float64)),
        float(np.sum(np.abs(half_arrays["initial_pullback"]*packed_delta), dtype=np.float64))/0.5,
        float(np.sum(np.abs(fine_vjps[1.0]["initial_pullback"]*packed_delta), dtype=np.float64)),
        float(np.sum(np.abs(fine_vjps[0.5]["initial_pullback"]*packed_delta), dtype=np.float64))/0.5)
    amplitude_floor = 2.0*max(amplitude_scatter.values())
    temporal_floor = 2.0*temporal_scatter_sensitivity
    sensitivity_budget = (amplitude_floor+temporal_floor+
                          source_sensitivity_floor+fp64_sensitivity_floor)
    sensitivity_error = abs(temporal_richardson_sensitivity-source_sensitivity_objective)
    if not np.isfinite(sensitivity_error) or sensitivity_error > sensitivity_budget:
        raise AssertionError(f"polarized DQ2 m=2 W sensitivity error {sensitivity_error:.6g} > "
                             f"amplitude/time/DOP853/FP64 budget {sensitivity_budget:.6g}")
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
            "polarized_DQ2_sensitivity": {
                "physical_direction_normalization": "native VJP dot r divided by amplitude; source DQ2(q)[r]",
                "scope": "one selected m2 W terminal objective and one control direction; not a global Hessian bound",
                "native_vjp_normalized_by_dt_and_amplitude": {
                    f"dt{dt_value:g}_a{amp:g}": value
                    for (dt_value, amp), value in native_sensitivity.items()},
                "amplitude_richardson_by_dt": {str(k): v for k,v in amplitude_richardson.items()},
                "amplitude_scatter_by_dt": {str(k): v for k,v in amplitude_scatter.items()},
                "temporal_order_assumed": 3,
                "temporal_richardson_sensitivity": temporal_richardson_sensitivity,
                "temporal_scatter": temporal_scatter_sensitivity,
                "source_DQ2_prediction": source_sensitivity_objective,
                "absolute_error": sensitivity_error,
                "amplitude_scatter_budget": amplitude_floor,
                "temporal_scatter_budget": temporal_floor,
                "source_tightening_floor": source_sensitivity_floor,
                "fp64_dot_product_floor": fp64_sensitivity_floor,
                "accepted_budget": sensitivity_budget,
                "source_tightening_relative": float(source_sensitivity["tightening_relative"]),
            },
            "source_direction_norm": float(np.linalg.norm(q0)),
            "control_state_direction_norm": float(np.linalg.norm(packed_delta))}


def run_finite_upwind_m3_check(exe: Path, outdir: Path, d: dict,
                               controls: np.ndarray) -> dict:
    """Connect the m=3 native full-map VJP to the selected finite-upwind source bank."""
    nx, ny, nz = GRIDS[0]
    offsets = packed_offsets(nx, ny, nz)
    q, _ = source_direction(d["column"], nx, ny, nz, offsets, controls)
    direction_r, packed_r = source_direction(d["column"], nx, ny, nz,
                                              offsets, CONTROL_DIRECTION)
    delta = float(d["branch_config"]["sign_smooth_delta_config"])
    # A fixed wide/normal plateau pair. Earlier .005/.01/.02/.04 points are
    # preserved in the external epsilon-scan receipt; .005 is visibly noisy.
    epsilons = FINITE_UPWIND_FD_EPSILONS
    sources = {}
    for amp in (1.0, 0.5):
        source = reference.propagate_finite_smooth_upwind(
            d["column"], q, amplitude=amp, times=np.asarray([6.0]), delta=delta)
        sources[amp] = source

    native_vjp = {}
    terminal = None
    terminal_norm = 0.0
    for dt, steps in ((1.0, 6), (0.5, 12)):
        for amp in (1.0, 0.5):
            direction = amp*pack_mode(q, nx, ny, nz, offsets)
            direction_path = outdir/f"upwind_m3_direction_dt{dt:g}_a{amp:g}.txt"
            path = outdir/f"upwind_m3_adjoint_dt{dt:g}_a{amp:g}.csv"
            write_direction(direction_path, direction)
            meta, arrays, scalars, _ = run_native(exe, nx, ny, nz, path,
                ("--upwind-trajectory", str(direction_path), str(steps), str(dt)))
            if int(meta.get("objective_x_harmonic", -1)) != 3:
                raise AssertionError("native upwind trajectory metadata is not m=3")
            if int(meta.get("upwind_trajectory", 0)) != 1 or int(meta.get("trajectory_steps", -1)) != steps:
                raise AssertionError("native upwind trajectory mode/step metadata mismatch")
            current_terminal = arrays["terminal_cotangent"]
            if terminal is None:
                terminal = current_terminal.copy()
                terminal_mode = project_harmonic_state(terminal, 3, nx, ny, nz, offsets)
                w_start, w_end = ny*nz, 2*ny*nz
                terminal_norm = float(np.linalg.norm(terminal_mode[w_start:w_end]))
                other_norm = float(np.linalg.norm(np.r_[terminal_mode[:w_start],
                                                         terminal_mode[w_end:]]))
                if terminal_norm < 1.0e-8 or other_norm > 1.0e-10*terminal_norm:
                    raise AssertionError("native m=3 terminal is not a W-only m=3 projection")
            elif not np.array_equal(terminal, current_terminal):
                raise AssertionError("native m=3 terminal changed across dt/amplitude")
            objective = objective_m3_value(meta, scalars, arrays)
            objective_terms = objective_dot_fp64_terms(arrays["final"], current_terminal)
            if abs(objective-objective_terms["objective_from_operands"]) > objective_terms["fp64_dot_product_floor"]:
                raise AssertionError("native m3 scalar disagrees with its final-state/terminal dot operands")
            native_vjp[(dt, amp)] = {
                "vjp_control_directional_derivative": float(np.dot(
                    arrays["initial_pullback"], amp*packed_r)),
                "vjp_directional_fp64_floor": (128.0*np.finfo(float).eps*float(np.sum(
                    np.abs(arrays["initial_pullback"]*(amp*packed_r)), dtype=np.float64))),
                "objective": objective,
                "objective_fp64_floor": objective_terms["fp64_dot_product_floor"],
                "objective_operand_receipt": objective_terms,
                "payload": path.name,
            }
            if abs(native_vjp[(dt, amp)]["objective"]) <= native_vjp[(dt, amp)]["objective_fp64_floor"]:
                raise AssertionError(f"native m3 objective signal is below FP64 floor at dt={dt:g}, a={amp:g}")

    assert terminal is not None
    source_objectives = {}
    source_roundoff_floors = {}
    for amp, source in sources.items():
        correction = source["state_correction"][3][0]
        tightening = source["tightening_states"][3][0]
        correction_state = reconstruct_source_mode(correction, 3, nx, ny, nz, offsets)
        tightening_state = reconstruct_source_mode(tightening, 3, nx, ny, nz, offsets)
        source_objectives[amp] = float(np.dot(terminal, correction_state))
        source_roundoff_floors[amp] = abs(float(np.dot(terminal, tightening_state)))
        if abs(source_objectives[amp]) <= source_roundoff_floors[amp]:
            raise AssertionError(f"source m3 finite-upwind signal is below DOP853 floor at a={amp:g}")

    native_fd = {}
    native_fd_outputs = {}
    native_fd_endpoint_receipts = {}
    native_fd_roundoff_by_epsilon = {}
    for epsilon in epsilons:
        outputs = []
        endpoint_receipts = []
        endpoint_floors = []
        for sign in (-1.0, 1.0):
            q_control = mode_pair_controls(
                d["column"], controls+sign*epsilon*CONTROL_DIRECTION)
            direction_path = outdir/f"upwind_m3_fd_eps{epsilon:g}_{sign:+g}.txt"
            path = outdir/f"upwind_m3_fd_eps{epsilon:g}_{sign:+g}.csv"
            write_direction(direction_path, pack_mode(q_control, nx, ny, nz, offsets))
            meta, arrays, scalars, _ = run_native(exe, nx, ny, nz, path,
                ("--upwind-trajectory", str(direction_path), "6", "1.0"))
            objective = objective_m3_value(meta, scalars, arrays)
            terms = objective_dot_fp64_terms(arrays["final"], arrays["terminal_cotangent"])
            if abs(objective-terms["objective_from_operands"]) > terms["fp64_dot_product_floor"]:
                raise AssertionError("m3 FD scalar disagrees with its endpoint operands")
            outputs.append(objective)
            endpoint_floors.append(terms["fp64_dot_product_floor"])
            endpoint_receipts.append({
                "sign": int(sign), "objective_m3": objective, **terms,
                "final_state": arrays["final"].tolist(),
                "terminal_cotangent": arrays["terminal_cotangent"].tolist(),
            })
        native_fd_outputs[str(epsilon)] = outputs
        native_fd_endpoint_receipts[str(epsilon)] = endpoint_receipts
        native_fd[epsilon] = (outputs[1]-outputs[0])/(2.0*epsilon)
        native_fd_roundoff_by_epsilon[epsilon] = (
            endpoint_floors[0]+endpoint_floors[1])/(2.0*epsilon)
    native_ad = native_vjp[(1.0, 1.0)]["vjp_control_directional_derivative"]
    fd_epsilon = epsilons[-1]
    fd_signal = abs(native_fd[fd_epsilon])
    fd_floor = native_fd_roundoff_by_epsilon[fd_epsilon]
    if fd_signal <= fd_floor:
        raise AssertionError("m=3 native finite-difference directional signal is at its FP64 floor")
    native_ad_floor = native_vjp[(1.0, 1.0)]["vjp_directional_fp64_floor"]
    if abs(native_ad) <= native_ad_floor:
        raise AssertionError("m=3 native AD directional signal is at its FP64 floor")
    fd_ad_relative = abs(native_ad-native_fd[fd_epsilon])/max(abs(native_ad), fd_signal)
    fd_epsilon_relative = abs(native_fd[epsilons[0]]-native_fd[epsilons[1]])/fd_signal
    if fd_ad_relative > ADJOINT_RELATIVE_BUDGET or fd_epsilon_relative > ADJOINT_RELATIVE_BUDGET:
        raise AssertionError(f"m=3 VJP/native-FD consistency failed: AD {fd_ad_relative:.6g}, "
                             f"epsilon scatter {fd_epsilon_relative:.6g}")

    source_fd = {}
    source_fd_outputs = {}
    source_fd_roundoff = {}
    for amp in (1.0, 0.5):
        for epsilon in epsilons:
            outputs = []
            dop_floors = []
            dot_operand_floors = []
            for sign in (-1.0, 1.0):
                q_control = mode_pair_controls(
                    d["column"], controls+sign*epsilon*CONTROL_DIRECTION)
                source = reference.propagate_finite_smooth_upwind(
                    d["column"], q_control, amplitude=amp,
                    times=np.asarray([6.0]), delta=delta)
                source_state = reconstruct_source_mode(
                    source["state_correction"][3][0], 3, nx, ny, nz, offsets)
                tightening_state = reconstruct_source_mode(
                    source["tightening_states"][3][0], 3, nx, ny, nz, offsets)
                dot_terms = objective_dot_fp64_terms(source_state, terminal)
                outputs.append(dot_terms["objective_from_operands"])
                dop_floors.append(abs(float(np.dot(terminal, tightening_state))))
                dot_operand_floors.append(dot_terms["fp64_dot_product_floor"])
            source_fd_outputs[f"a{amp:g}_eps{epsilon:g}"] = outputs
            source_fd[(amp, epsilon)] = (outputs[1]-outputs[0])/(2.0*epsilon)
            total_endpoint_floors = [dop+dot for dop, dot in
                                     zip(dop_floors, dot_operand_floors)]
            source_fd_roundoff[f"a{amp:g}_eps{epsilon:g}"] = {
                "minus_plus_dop_tightening_floors": dop_floors,
                "minus_plus_dot_operand_floors": dot_operand_floors,
                "minus_plus_total_objective_floors": total_endpoint_floors,
                "directional_derivative_dop_floor": (dop_floors[0]+dop_floors[1])/(2.0*epsilon),
                "directional_derivative_dot_operand_floor": (
                    dot_operand_floors[0]+dot_operand_floors[1])/(2.0*epsilon),
                "directional_derivative_floor": sum(total_endpoint_floors)/(2.0*epsilon),
                "floor_scope": "source m3 objective dot operands plus DOP853 tightening; not native solver error",
            }
    source_fd_directional = source_fd[(1.0, fd_epsilon)]
    source_fd_signal = abs(source_fd_directional)
    source_fd_floor = source_fd_roundoff[f"a1_eps{fd_epsilon:g}"]["directional_derivative_floor"]
    native_m3_directional = native_vjp[(1.0, 1.0)]["vjp_control_directional_derivative"]
    native_m3_floor = native_vjp[(1.0, 1.0)]["vjp_directional_fp64_floor"]
    source_comparison_budget = (
        ADJOINT_RELATIVE_BUDGET*max(abs(native_m3_directional), source_fd_signal)
        + source_fd_floor+native_m3_floor)
    source_comparison_error = abs(native_m3_directional-source_fd_directional)
    source_comparison_relative = source_comparison_error/max(
        abs(native_m3_directional), source_fd_signal)
    if (source_fd_signal <= source_fd_floor or abs(native_m3_directional) <= native_m3_floor or
            source_comparison_error > source_comparison_budget):
        raise AssertionError(f"m3 full-map VJP/source upwind-FD mismatch {source_comparison_error:.6g} "
                             f"> source/FP64 budget {source_comparison_budget:.6g}")
    source_epsilon_scatter = abs(source_fd[(1.0, epsilons[0])]-source_fd[(1.0, epsilons[1])])
    source_epsilon_relative = source_epsilon_scatter/source_fd_signal
    if source_epsilon_relative > ADJOINT_RELATIVE_BUDGET:
        raise AssertionError(f"m3 source FD epsilon scatter {source_epsilon_relative:.6g} > "
                             f"{ADJOINT_RELATIVE_BUDGET}")

    return {"harmonic": 3, "seconds": 6.0,
            "terminal_w_m3_norm": terminal_norm,
            "control_direction": CONTROL_DIRECTION.tolist(),
            "source_sign_smooth_delta": delta,
            "native_vjp_full_map_by_dt_and_amplitude": {
                f"dt{dt:g}_a{amp:g}": result for (dt, amp), result in native_vjp.items()},
            "source_finite_smooth_upwind_objective_by_amplitude": {
                str(amp): {"objective": source_objectives[amp],
                           "projected_dop_tightening_floor": source_roundoff_floors[amp],
                           "tightening_relative": float(sources[amp]["tightening_relative"])}
                for amp in (1.0, 0.5)},
            "native_fd_m3_objectives_minus_plus": native_fd_outputs,
            "native_fd_endpoint_operands_for_recalculation": native_fd_endpoint_receipts,
            "native_fd_directional_derivative_by_epsilon": {
                str(eps): value for eps, value in native_fd.items()},
            "native_fd_objective_floor_by_epsilon": {
                str(eps): value for eps, value in native_fd_roundoff_by_epsilon.items()},
            "source_finite_upwind_control_fd_objectives_minus_plus": source_fd_outputs,
            "source_finite_upwind_control_fd_roundoff_floors": source_fd_roundoff,
            "source_finite_upwind_control_fd_by_amplitude_and_epsilon": {
                f"a{amp:g}_eps{eps:g}": value
                for (amp, eps), value in source_fd.items()},
            "native_ad_vs_native_fd_relative_error": fd_ad_relative,
            "native_fd_epsilon_scatter_relative": fd_epsilon_relative,
            "native_fd_directional_signal": fd_signal,
            "native_fd_roundoff_floor": fd_floor,
            "native_fd_signal_to_floor_ratio": fd_signal/max(fd_floor, np.finfo(float).tiny),
            "native_fd_signal_cancellation_ratio": fd_floor/max(fd_signal, np.finfo(float).tiny),
            "objective_floor_cancellation_falsifier": objective_floor_cancellation_falsifier(),
            "source_m3_fullmap_directional_gate": {
                "amplitude": 1.0, "native_fullmap_vjp": native_m3_directional,
                "source_finite_upwind_fd": source_fd_directional,
                "source_finite_upwind_fd_absolute_signal": source_fd_signal,
                "absolute_error": source_comparison_error,
                "relative_error": source_comparison_relative,
                "relative_budget": ADJOINT_RELATIVE_BUDGET,
                "native_vjp_fp64_floor": native_m3_floor,
                "source_fd_tightening_floor": source_fd_floor,
                "accepted_absolute_budget": source_comparison_budget,
                "source_fd_epsilon_scatter_relative": source_epsilon_relative,
            },
            "source_finite_upwind_vs_native_objective_gap_by_amplitude": {
                str(amp): {
                    "native_fullmap_objective": native_vjp[(1.0, amp)]["objective"],
                    "source_upwind_objective": source_objectives[amp],
                    "signed_gap": native_vjp[(1.0, amp)]["objective"]-source_objectives[amp],
                } for amp in (1.0, 0.5)},
            "source_vs_full_map_directional_gap": {
                str(amp): native_vjp[(1.0, amp)]["vjp_control_directional_derivative"]-
                          source_fd[(amp, fd_epsilon)]
                for amp in (1.0, 0.5)},
            "fd_epsilon_pair": [float(epsilons[0]), float(epsilons[1])],
            "interpretation": "finite-upwind-dominance diagnostic: source FD compares a selected finite upwind channel with full-map m3 VJP; omitted m3 contributions are not bounded and complete cubic closure remains open"}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path,
                        help="built test_native_wave_refinement executable")
    parser.add_argument("--output-dir", type=Path,
                        help="retain native CSVs and machine-readable results here")
    parser.add_argument("--skip-trajectories", action="store_true",
                        help="omit the 30-second and 300/600/900-second source/native trajectory comparisons; the separate adjoint check still runs")
    parser.add_argument("--skip-adjoint", action="store_true",
                        help="omit the short 6-second m=2 DQ2 and m=3 finite-upwind adjoint checks")
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
    theta_calibrations = {
        "x".join(map(str, grid)): mass_theta_uniform_calibration(
            descriptors[grid]["column"], *grid)
        for grid in GRIDS}
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
    phase_mixed_q = q0+1j*q1
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
            "source_A0_sha256": hashlib.sha256(
                np.asarray(reference.source_zero_matrix(c), dtype=np.complex128).tobytes()).hexdigest(),
            "source_A2_sha256": hashlib.sha256(
                np.asarray(reference.source_matrix(c2), dtype=np.complex128).tobytes()).hexdigest(),
            "source_initial_m1": [[float(v.real), float(v.imag)] for v in q],
            "packed_direction_sha256": hashlib.sha256(packed_direction.tobytes()).hexdigest(),
            "packed_native_initial_sha256": hashlib.sha256(
                (np.asarray(desc["arrays"]["base"], dtype=np.float64)+packed_direction).tobytes()).hexdigest(),
        }
        flipped_direction = flip_velocity_blocks(packed_direction, nx, ny, nz)
        input_receipt.setdefault("velocity_reversal_probe_directions", {})[
            "x".join(map(str, grid))] = {
                "original_packed_direction_sha256": hashlib.sha256(packed_direction.tobytes()).hexdigest(),
                "flipped_packed_direction_sha256": hashlib.sha256(flipped_direction.tobytes()).hexdigest(),
                "nonvelocity_blocks_bit_identical": all(
                    np.array_equal(packed_direction[packed_block_slices(nx, ny, nz)[name]],
                                   flipped_direction[packed_block_slices(nx, ny, nz)[name]])
                    for name in ("PH", "THETA", "MU")),
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
    phase_mixed_direction = pack_mode(phase_mixed_q, coarse_nx, coarse_ny,
                                      coarse_nz, coarse_offsets)
    input_receipt["coarse_probe_directions"]["phase_mixed_mean"] = {
        "source_m1": [[float(v.real), float(v.imag)] for v in phase_mixed_q],
        "packed_direction_sha256": hashlib.sha256(phase_mixed_direction.tobytes()).hexdigest(),
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
    long_grid = GRIDS[0]
    long_nx, long_ny, long_nz = long_grid
    long_packed = pack_mode(coarse_q, long_nx, long_ny, long_nz,
                            packed_offsets(long_nx, long_ny, long_nz))
    long_steps_by_dt = LONG_DT_STEPS
    input_receipt["long_weak_forward_inputs"] = {
        "grid": list(long_grid), "duration_seconds": 900.0,
        "steps_by_dt": {str(dt): steps for dt, steps in long_steps_by_dt.items()},
        "zero_base_direction_sha256": hashlib.sha256(
            np.zeros_like(long_packed).tobytes()).hexdigest(),
        "positive_direction_sha256": {
            f"dt{dt:g}_a{amp:g}": hashlib.sha256((amp*long_packed).tobytes()).hexdigest()
            for dt in long_steps_by_dt for amp in (1.0, 0.5)},
    }
    input_receipt["matched_T300_resolution_check"] = {
        "grid": list(long_grid), "dt": 1.25, "steps": 240,
        "positive_direction_sha256": {
            str(amp): hashlib.sha256((amp*long_packed).tobytes()).hexdigest()
            for amp in (0.0, 1.0, 0.5)},
        "same_base_and_control_directions_as_long_pair": True,
    }
    adjoint_offsets = packed_offsets(*GRIDS[0])
    adjoint_nx, adjoint_ny, adjoint_nz = GRIDS[0]
    _, adjoint_center = source_direction(coeffs[GRIDS[0]], adjoint_nx, adjoint_ny,
                                         adjoint_nz, adjoint_offsets, controls)
    _, adjoint_delta = source_direction(coeffs[GRIDS[0]], adjoint_nx, adjoint_ny,
                                        adjoint_nz, adjoint_offsets, CONTROL_DIRECTION)
    input_receipt["adjoint_inputs"] = {
        "step_schedules": {"dt1": 6, "dt0.5": 12},
        "finite_difference_epsilons": [0.01, 0.005],
        "center_direction_sha256": hashlib.sha256(adjoint_center.tobytes()).hexdigest(),
        "half_amplitude_direction_sha256": hashlib.sha256((0.5*adjoint_center).tobytes()).hexdigest(),
        "control_direction_state_sha256": hashlib.sha256(adjoint_delta.tobytes()).hexdigest(),
    }
    input_receipt["finite_upwind_m3_inputs"] = {
        "grid": list(GRIDS[0]), "terminal_harmonic": 3, "duration_seconds": 6.0,
        "dt_steps": {"1": 6, "0.5": 12},
        "control_direction": CONTROL_DIRECTION.tolist(),
        "amplitude_direction_sha256": {
            f"dt{dt:g}_a{amp:g}": hashlib.sha256((amp*adjoint_center).tobytes()).hexdigest()
            for dt in (1.0, 0.5) for amp in (1.0, 0.5)},
        "control_fd_epsilon_directions": {
            str(epsilon): {
                str(sign): hashlib.sha256(pack_mode(
                    mode_pair_controls(coeffs[GRIDS[0]], controls+
                        sign*epsilon*CONTROL_DIRECTION), adjoint_nx, adjoint_ny,
                    adjoint_nz, adjoint_offsets).tobytes()).hexdigest()
                for sign in (-1.0, 1.0)}
            for epsilon in FINITE_UPWIND_FD_EPSILONS},
        "native_fd_epsilon_directions": {
            str(epsilon): {
                str(sign): hashlib.sha256(pack_mode(
                    mode_pair_controls(coeffs[GRIDS[0]], controls+
                        sign*epsilon*CONTROL_DIRECTION), adjoint_nx, adjoint_ny,
                    adjoint_nz, adjoint_offsets).tobytes()).hexdigest()
                for sign in (-1.0, 1.0)}
            for epsilon in FINITE_UPWIND_FD_EPSILONS},
        "native_velocity_direction_sha256": {
            f"dt{dt:g}_a{amp:g}": hashlib.sha256(
                (amp*adjoint_center).tobytes()).hexdigest()
            for dt in (1.0, 0.5) for amp in (1.0, 0.5)},
        "sign_smooth_delta_config": descriptors[GRIDS[0]]["branch_config"][
            "sign_smooth_delta_config"],
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
              "packed_block_layout_self_check": {
                  "x".join(map(str, grid)): packed_block_marker(*grid) for grid in GRIDS},
              "mass_weighted_theta_calibration": theta_calibrations,
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
              "instantaneous_coefficients": {}, "finite_smooth_upwind": {}, "trajectory": {}, "scope":
              "source-transcribed fixture-scope complete Q2; native primal trajectories provide forward responses, AD supplies the selected VJP"}
    for grid in GRIDS:
        label = "x".join(map(str, grid))
        q_initial = coarse_q if grid == GRIDS[0] else fine_q
        result["instantaneous_coefficients"][label] = run_probe(
            exe, outdir, grid, descriptors[grid], q_initial)
    for grid in GRIDS:
        label = "x".join(map(str, grid))
        q_initial = coarse_q if grid == GRIDS[0] else fine_q
        result["finite_smooth_upwind"][label] = run_finite_upwind_probe(
            exe, outdir, grid, descriptors[grid], q_initial,
            result["instantaneous_coefficients"][label])
    result["coarse_phase_mixed_mean_probe"] = run_mean_probe(
        exe, outdir, GRIDS[0], descriptors[GRIDS[0]], phase_mixed_q)
    # Verify each selected mode's self term and the independently formed
    # bilinear cross term, so combined forcing cannot pass by amplitude-only
    # scaling or cancellation between the two controls.
    coarse = descriptors[GRIDS[0]]["column"]
    probe0 = run_probe(exe, outdir/"mode0_self", GRIDS[0], descriptors[GRIDS[0]], q0,
                       check_translation=False)
    probe1 = run_probe(exe, outdir/"mode1_self", GRIDS[0], descriptors[GRIDS[0]], q1,
                       check_translation=False)
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
    # Preserve the short independent DQ2/VJP result even if a later long
    # forecast state is rejected by its dry-EOS domain or weak-prediction gate.
    if not args.skip_adjoint:
        result["adjoint_directional_check"] = run_adjoint_check(
            exe, outdir, descriptors[GRIDS[0]], controls)
        result["finite_upwind_m3_adjoint"] = run_finite_upwind_m3_check(
            exe, outdir, descriptors[GRIDS[0]], controls)
    if not args.skip_trajectories:
        for grid in GRIDS:
            label = "x".join(map(str, grid))
            result["trajectory"][label] = run_trajectory(
                exe, outdir, grid, descriptors[grid],
                coarse_q if grid == GRIDS[0] else fine_q)
        result["long_weak_prediction"] = run_long_weak_prediction(
            exe, outdir, GRIDS[0], descriptors[GRIDS[0]], coarse_q)
    def json_scalar(value: object) -> object:
        if isinstance(value, np.generic):
            return value.item()
        raise TypeError(f"Object of type {type(value).__name__} is not JSON serializable")

    rendered = json.dumps(result, indent=2, sort_keys=True, default=json_scalar)
    (outdir/"wave_quadratic_forcing_report.json").write_text(rendered+"\n")
    print(rendered)
    print(f"\nArtifacts: {outdir}")


if __name__ == "__main__":
    main()
