#!/usr/bin/env python3
"""Independent source-column gate for the dynamic native-W refinement fixture.

The C++ driver writes a deliberately small CSV protocol (`M` scalars, `A`
arrays).  This file imports the existing source-transcribed wave equations,
rebuilds A from the native serialized FP32 PHB faces, and checks modal tangent
and propagation behavior at the requested horizontal grids.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import importlib.util
import json
import math
import platform
import subprocess
import tempfile
from pathlib import Path

import numpy as np
import scipy
from scipy.linalg import expm


ROOT = Path(__file__).resolve().parents[1]
REF_PATH = ROOT / "tools" / "wave_energy_spatial_reference.py"
SPEC = importlib.util.spec_from_file_location("wave_energy_spatial_reference", REF_PATH)
assert SPEC and SPEC.loader
REF = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(REF)
RUN_LOG: list[str] = []


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_payload(path: Path) -> tuple[dict[str, float], dict[str, np.ndarray], dict[str, float], dict[str, str]]:
    meta: dict[str, float] = {}
    arrays: dict[str, np.ndarray] = {}
    scalars: dict[str, float] = {}
    text_meta: dict[str, str] = {}
    with path.open(newline="") as f:
        for row in csv.reader(f):
            if not row:
                continue
            if row[0] == "M":
                meta[row[1]] = float(row[2])
            elif row[0] == "A":
                count = int(row[2])
                value = np.asarray([float(x) for x in row[3:]], dtype=np.float64)
                if value.size != count:
                    raise AssertionError(f"{row[1]} has {value.size} values, expected {count}")
                arrays[row[1]] = value
            elif row[0] == "S":
                scalars[row[1]] = float(row[2])
            elif row[0] == "T":
                text_meta[row[1]] = row[2]
    return meta, arrays, scalars, text_meta


def source_column(nz: int, nx: int, phb_faces: np.ndarray) -> dict:
    """Use the existing source-transcribed A with the actual imported PHB faces."""
    return REF.column(nz=nz, nx=nx, lx=40000.0, ly=30000.0,
                      theta_gradient="analytic", imported_phb_faces=phb_faces)


def native_mode_state(column: dict, base: np.ndarray, nx: int, ny: int, nz: int,
                      offsets: tuple[int, ...]) -> tuple[np.ndarray, float]:
    """Build one small real wave from the independently selected sub-Nmax eigenmode."""
    values, vectors = np.linalg.eig(column["A"])
    nmax = float(np.sqrt(np.max(column["N2_analytic"])))
    candidates = [i for i, value in enumerate(values)
                  if value.imag > 1.0e-7 and value.imag <= nmax]
    if not candidates:
        raise AssertionError(f"no oscillatory source mode below Nmax={nmax}")
    selected = max(candidates, key=lambda i: values[i].imag)
    q = vectors[:, selected].astype(np.complex128)
    pivot = nz + (nz - 1)
    q *= np.exp(-1j * np.angle(q[pivot]))
    unit_scale = max(np.max(np.abs(q[:nz])), np.max(np.abs(q[nz:2*nz])),
                     np.max(np.abs(q[2*nz:3*nz])) / 100.0,
                     np.max(np.abs(q[3*nz:4*nz])), abs(q[-1]) / 100.0)
    q *= 5.0e-3 / unit_scale

    state = base.copy()
    o_u, o_v, o_w, o_ph, o_t, o_mu = offsets
    nu, nw = nx + 1, nz + 1
    dx, wave_k = column["dx"], column["k_physical"]
    phase_u = np.exp(1j * wave_k * np.arange(nu) * dx)
    phase_c = np.exp(1j * wave_k * (np.arange(nx) + 0.5) * dx)
    u = state[o_u:o_v].reshape(ny, nz, nu)
    w = state[o_w:o_ph].reshape(ny, nw, nx)
    ph = state[o_ph:o_t].reshape(ny, nw, nx)
    theta = state[o_t:o_mu].reshape(ny, nz, nx)
    mu = state[o_mu:].reshape(ny, nx)
    for k in range(nz):
        u[:, k, :] += np.real(q[k] * phase_u)[None, :]
        w[:, k + 1, :] += np.real(q[nz + k] * phase_c)[None, :]
        ph[:, k + 1, :] += np.real(q[2*nz + k] * phase_c)[None, :]
        theta[:, k, :] += np.real(q[3*nz + k] * phase_c)[None, :]
    mu += np.real(q[-1] * phase_c)[None, :]
    return state, float(values[selected].imag)


def unpack_projection(z: np.ndarray, nx: int, ny: int, nz: int, lx: float,
                      offsets: tuple[int, ...]) -> dict[str, np.ndarray]:
    """Return Fourier amplitudes of C-grid U/W/PH/theta/MU using physical coordinates."""
    nu, nv, nw = nx + 1, ny + 1, nz + 1
    su, sv, sw = ny * nz * nu, nv * nz * nx, ny * nw * nx
    st, sm = ny * nz * nx, ny * nx
    o_u, o_v, o_w, o_ph, o_t, o_m = offsets
    k = 2.0 * np.pi / lx
    def mode(a: np.ndarray, xface: bool) -> np.ndarray:
        n = a.shape[-1]
        x = np.arange(n, dtype=float) * (lx / nx) + (0.0 if xface else 0.5 * lx / nx)
        # Real-part convention: Re(q exp(ikx)) has projected amplitude q.
        return 2.0 * np.mean(a * np.exp(-1j * k * x), axis=-1)
    u = z[o_u:o_u + su].reshape(ny, nz, nu).mean(axis=0)
    v = z[o_v:o_v + sv].reshape(nv, nz, nx).mean(axis=0)
    w = z[o_w:o_w + sw].reshape(ny, nw, nx).mean(axis=0)
    ph = z[o_ph:o_ph + sw].reshape(ny, nw, nx).mean(axis=0)
    th = z[o_t:o_t + st].reshape(ny, nz, nx).mean(axis=0)
    mu = z[o_m:o_m + sm].reshape(ny, nx).mean(axis=0)
    return {"u": mode(u[:, :nx], True), "v": mode(v, False),
            "w": mode(w, False), "ph": mode(ph, False),
            "theta": mode(th, False), "mu": mode(mu, False)}


def modal_source_vector(p: dict[str, np.ndarray], nz: int) -> np.ndarray:
    # Source order is [U,W,PH(1..nz),THETA,MU].
    q = np.zeros(4 * nz + 1, dtype=np.complex128)
    q[:nz] = p["u"]
    q[nz:2 * nz] = p["w"][1:nz + 1]
    q[2 * nz:3 * nz] = p["ph"][1:nz + 1]
    q[3 * nz:4 * nz] = p["theta"]
    q[-1] = p["mu"].mean()
    return q


def reconstruct_modal_rhs(q: np.ndarray, nx: int, ny: int, nz: int, lx: float,
                          offsets: tuple[int, ...]) -> np.ndarray:
    """Reconstruct a real C-grid Fourier RHS over every horizontal row/cell."""
    total = offsets[-1] + ny * nx
    result = np.zeros(total, dtype=np.float64)
    o_u, o_v, o_w, o_ph, o_t, o_mu = offsets
    nw = nz + 1
    wave_k = 2.0 * np.pi / lx
    face_phase = np.exp(1j * wave_k * np.arange(nx + 1) * (lx / nx))
    center_phase = np.exp(1j * wave_k * (np.arange(nx) + 0.5) * (lx / nx))
    u = result[o_u:o_v].reshape(ny, nz, nx + 1)
    v = result[o_v:o_w].reshape(ny + 1, nz, nx)
    w = result[o_w:o_ph].reshape(ny, nw, nx)
    ph = result[o_ph:o_t].reshape(ny, nw, nx)
    theta = result[o_t:o_mu].reshape(ny, nz, nx)
    mu = result[o_mu:].reshape(ny, nx)
    for k in range(nz):
        u[:, k, :] = np.real(q[k] * face_phase)[None, :]
        w[:, k + 1, :] = np.real(q[nz + k] * center_phase)[None, :]
        ph[:, k + 1, :] = np.real(q[2*nz + k] * center_phase)[None, :]
        theta[:, k, :] = np.real(q[3*nz + k] * center_phase)[None, :]
    mu[:, :] = np.real(q[-1] * center_phase)[None, :]
    return result


def block_error(actual: np.ndarray, expected: np.ndarray, label: str,
                floor: float, budget: float) -> float:
    err = float(np.linalg.norm(actual - expected))
    scale = max(float(np.linalg.norm(expected)), floor * math.sqrt(expected.size))
    rel = err / scale
    if not np.isfinite(rel) or rel > budget:
        raise AssertionError(f"{label} scaled error {rel:.6g} exceeds {budget:.6g}; abs={err:.6g}")
    return rel


def require_zero_response(error: float, budget: float, label: str) -> None:
    if not np.isfinite(error) or error > budget:
        raise AssertionError(f"{label} analytic-zero response {error:.6g} exceeds absolute FD roundoff budget {budget:.6g}")


def run_grid(exe: Path, nx: int, ny: int, nz: int) -> dict:
    tmp = tempfile.TemporaryDirectory(prefix="native-wave-")
    payload = Path(tmp.name) / "native.csv"
    initial_file = Path(tmp.name) / "initial_state.txt"
    def invoke(initial: Path | None = None, descriptor_only: bool = False):
        command = [str(exe), str(nx), str(ny), str(nz), str(payload)]
        if initial is not None:
            command.append(str(initial))
        elif descriptor_only:
            command.append("--descriptor-only")
        completed = subprocess.run(command, check=False, capture_output=True, text=True)
        if completed.returncode:
            tail = "\n".join((completed.stdout + completed.stderr).splitlines()[-80:])
            raise RuntimeError(f"native wave executable failed ({completed.returncode}):\n{tail}")
        RUN_LOG.append(f"COMMAND: {' '.join(command)}\n{completed.stdout}{completed.stderr}\n")
        return read_payload(payload)

    meta, first, first_scalars, first_build_info = invoke(descriptor_only=True)
    if (int(meta["nx"]), int(meta["ny"]), int(meta["nz"])) != (nx, ny, nz):
        raise AssertionError("native driver returned a mismatched dynamic grid")
    phb3 = first["phb"].reshape(ny, nz + 1, nx)
    phb_faces = phb3[0, :, 0]
    if not np.array_equal(phb3, np.broadcast_to(phb_faces[None, :, None], phb3.shape)):
        raise AssertionError("native base PHB is not a horizontally uniform imported column")
    c = source_column(nz, nx, phb_faces)
    offsets = (0, ny * nz * (nx + 1),
               ny * nz * (nx + 1) + (ny + 1) * nz * nx,
               ny * nz * (nx + 1) + (ny + 1) * nz * nx + ny * (nz + 1) * nx,
               ny * nz * (nx + 1) + (ny + 1) * nz * nx + 2 * ny * (nz + 1) * nx,
               ny * nz * (nx + 1) + (ny + 1) * nz * nx + 2 * ny * (nz + 1) * nx + ny * nz * nx)
    pbase = first["pbase"].reshape(ny, nz, nx)
    thbase = first["thbase_perturb"].reshape(ny, nz, nx)
    mubase = first["mubase"].reshape(ny, nx)
    eta = 1.0 - (np.arange(nz, dtype=np.float64) + 0.5) / nz
    expected_pbase = (REF.PTOP + REF.MUB * eta).astype(np.float32).astype(np.float64)
    if not np.array_equal(pbase[0, :, 0], expected_pbase):
        raise AssertionError("native base pressure does not match ptop=20kPa/MUB=80kPa sigma profile")
    if np.any(thbase != 0.0) or not np.all(mubase == REF.MUB):
        raise AssertionError("native setBaseState must retain theta-base=300K and MUB=80kPa")
    ph0 = offsets[3]
    phpert = first["base"][ph0:ph0 + ny * (nz + 1) * nx].reshape(ny, nz + 1, nx)
    target_theta = 317.0 - 8.0 * eta
    target_pressure = REF.PTOP + REF.M_TOTAL * eta
    alpha_base = REF._alpha(np.full(nz, 300.0), REF.PTOP + REF.MUB * eta)
    alpha_target = REF._alpha(target_theta, target_pressure)
    expected_ph = np.r_[0.0, np.cumsum((REF.M_TOTAL * alpha_target - REF.MUB * alpha_base) / nz)]
    ph_error = float(np.max(np.abs(phpert[0, :, 0] - expected_ph)))
    ph_budget = 64.0 * np.finfo(np.float64).eps * max(1.0, float(np.max(np.abs(expected_ph))))
    if ph_error > ph_budget or not np.allclose(phpert, expected_ph[None, :, None], rtol=0.0, atol=ph_budget):
        raise AssertionError(f"native PH perturbation misses analytic EOS target: {ph_error} > {ph_budget}")
    th0 = offsets[4]
    theta_pert = first["base"][th0:th0 + ny * nz * nx].reshape(ny, nz, nx)
    expected_theta_pert = (target_theta - 300.0).astype(np.float64)
    if not np.allclose(theta_pert, expected_theta_pert[None, :, None], rtol=0.0, atol=2e-14):
        raise AssertionError("native theta perturbation does not represent 317−8η K over the 300 K base")
    # Select a true source-column wave, serialize its packed initial condition,
    # and rerun the actual native forward/VJP path with that wave.
    initial_state, frequency = native_mode_state(c, first["base"], nx, ny, nz, offsets)
    with initial_file.open("w") as f:
        f.write(f"{initial_state.size}\n")
        f.write(" ".join(f"{v:.17g}" for v in initial_state))
        f.write("\n")
    meta, a, scalars, build_info = invoke(initial_file)
    if not np.array_equal(a["phb"], first["phb"]):
        raise AssertionError("native PHB changed between descriptor and eigenmode runs")
    if first_build_info != build_info:
        raise AssertionError("native build information changed between descriptor and eigenmode runs")
    meta["mode_frequency"] = frequency
    # The serialized tangent is a same-grid directional derivative. Remove the
    # irrelevant meridional V block before comparing the source's 5-block mode.
    actual = modal_source_vector(unpack_projection(a["tangent"], nx, ny, nz, 40000.0, offsets), nz)
    actual_narrow = modal_source_vector(unpack_projection(a["tangent_narrow"], nx, ny, nz, 40000.0, offsets), nz)
    actual_projection = unpack_projection(a["tangent"], nx, ny, nz, 40000.0, offsets)
    direction = modal_source_vector(unpack_projection(a["direction"], nx, ny, nz, 40000.0, offsets), nz)
    expected = c["A"] @ direction
    floors = np.r_[np.ones(nz), np.ones(nz), np.full(nz, 100.0), np.ones(nz), 100.0]
    sizes = [nz, nz, nz, nz, 1]
    names = ["U", "W", "PH", "THETA", "MU"]
    errs, start = {}, 0
    for name, size, floor in zip(names, sizes, floors[np.cumsum([0] + sizes[:-1])]):
        errs[name] = block_error(actual[start:start + size], expected[start:start + size],
                                 f"{nx}x{ny}x{nz} tangent {name}", float(floor), 1.0e-8)
        errs[f"{name}_fd_width_closure"] = block_error(
            actual[start:start + size], actual_narrow[start:start + size],
            f"{nx}x{ny}x{nz} {name} FD-width closure", float(floor), 1.0e-8)
        start += size
    v_error = block_error(actual_projection["v"], np.zeros(nz, dtype=np.complex128),
                          f"{nx}x{ny}x{nz} tangent V", 1.0, 1.0e-8)
    errs["V"] = v_error
    # Compare every actual owned cell to a reconstructed C-grid field. This
    # keeps y-boundary and off-mode leakage visible instead of averaging it out.
    output_blocks = [("U", 0, ny*nz*(nx+1), 1.0),
                     ("V", ny*nz*(nx+1), (ny+1)*nz*nx, 1.0),
                     ("W", ny*nz*(nx+1)+(ny+1)*nz*nx, ny*(nz+1)*nx, 1.0),
                     ("PH", ny*nz*(nx+1)+(ny+1)*nz*nx+ny*(nz+1)*nx, ny*(nz+1)*nx, 100.0),
                     ("THETA", ny*nz*(nx+1)+(ny+1)*nz*nx+2*ny*(nz+1)*nx, ny*nz*nx, 1.0),
                     ("MU", ny*nz*(nx+1)+(ny+1)*nz*nx+2*ny*(nz+1)*nx+ny*nz*nx, ny*nx, 100.0)]
    canonical_worst = {name: 0.0 for name, *_ in output_blocks}
    canonical_scatter = {name: 0.0 for name, *_ in output_blocks}
    canonical_zero = {name: 0.0 for name, *_ in output_blocks}
    canonical_zero_wide = {name: 0.0 for name, *_ in output_blocks}
    canonical_zero_scatter = {name: 0.0 for name, *_ in output_blocks}
    canonical_zero_budget = {name: 0.0 for name, *_ in output_blocks}
    canonical_columns = []
    zero_violations = []
    exact_zero_threshold = 64.0 * np.finfo(float).eps
    rhs_fd_h = {"wide": 1.0e-3, "narrow": 5.0e-4}
    for col in range(4 * nz + 1):
        input_q = modal_source_vector(
            unpack_projection(a[f"direction_col_{col}"], nx, ny, nz, 40000.0, offsets), nz)
        expected_col = c["A"] @ input_q
        expected_full = reconstruct_modal_rhs(expected_col, nx, ny, nz, 40000.0, offsets)
        actual_full = a[f"tangent_col_{col}"]
        narrow_full = a[f"tangent_narrow_col_{col}"]
        col_records = {}
        for name, start, size, unit in output_blocks:
            sl = slice(start, start + size)
            expected_block = expected_full[sl]
            actual_block = actual_full[sl]
            narrow_block = narrow_full[sl]
            scaled_error = block_error(actual_block, expected_block,
                f"{nx}x{ny}x{nz} full-grid A column {col}->{name}", unit, 1.0e-8)
            width_scatter = block_error(actual_block, narrow_block,
                f"{nx}x{ny}x{nz} full-grid FD-width column {col}->{name}", unit, 1.0e-8)
            canonical_worst[name] = max(canonical_worst[name], scaled_error)
            canonical_scatter[name] = max(canonical_scatter[name], width_scatter)

            # Entries whose independent source value is analytically zero get
            # an absolute FD-roundoff bound derived from the actual +/- RHS
            # operand magnitudes and declared physical units. This prevents a
            # large PH/MU normalization floor from hiding a false coupling.
            zero_tol = exact_zero_threshold * max(1.0, float(np.max(np.abs(expected_block))))
            zero_mask = np.abs(expected_block) <= zero_tol
            zero_wide_error = float(np.max(np.abs(actual_block[zero_mask]))) if zero_mask.any() else 0.0
            zero_narrow_error = float(np.max(np.abs(narrow_block[zero_mask]))) if zero_mask.any() else 0.0
            zero_fd_scatter = float(np.max(np.abs(actual_block[zero_mask]-narrow_block[zero_mask]))) if zero_mask.any() else 0.0
            op_wide = scalars[f"rhs_operand_{col}_wide_{name}"]
            op_narrow = scalars[f"rhs_operand_{col}_narrow_{name}"]
            roundoff_bound = 16.0 * np.finfo(float).eps * max(
                op_wide / rhs_fd_h["wide"], op_narrow / rhs_fd_h["narrow"])
            unit_roundoff_bound = 64.0 * np.finfo(float).eps * unit
            zero_budget = max(roundoff_bound, unit_roundoff_bound, 2.0 * zero_fd_scatter)
            if not np.isfinite(zero_narrow_error) or zero_narrow_error > zero_budget:
                zero_violations.append({"source_column": col, "output_block": name,
                    "wide_error_abs": zero_wide_error, "narrow_error_abs": zero_narrow_error,
                    "fd_width_scatter_abs": zero_fd_scatter, "roundoff_budget_abs": zero_budget,
                    "rhs_operand_max_abs_wide": op_wide, "rhs_operand_max_abs_narrow": op_narrow})
            canonical_zero[name] = max(canonical_zero[name], zero_narrow_error)
            canonical_zero_wide[name] = max(canonical_zero_wide[name], zero_wide_error)
            canonical_zero_scatter[name] = max(canonical_zero_scatter[name], zero_fd_scatter)
            canonical_zero_budget[name] = max(canonical_zero_budget[name], zero_budget)
            col_records[name] = {
                "full_grid_scaled_error": scaled_error,
                "full_grid_fd_width_scatter": width_scatter,
                "expected_full_field_max_abs": float(np.max(np.abs(expected_block))),
                "analytic_zero_entry_count": int(zero_mask.sum()),
                "analytic_zero_response_max_abs_wide": zero_wide_error,
                "analytic_zero_response_max_abs_narrow": zero_narrow_error,
                "analytic_zero_fd_width_scatter_abs": zero_fd_scatter,
                "fd_roundoff_budget_abs": zero_budget,
                "roundoff_from_rhs_operands_abs": roundoff_bound,
                "physical_unit_roundoff_abs": unit_roundoff_bound,
                "rhs_operand_max_abs_wide": op_wide,
                "rhs_operand_max_abs_narrow": op_narrow,
            }
        canonical_columns.append({"source_column": col, "input_modal_signal": [
            [float(q.real), float(q.imag)] for q in input_q], "outputs": col_records})
    for name, *_ in output_blocks:
        errs[f"A_full_grid_{name}"] = canonical_worst[name]
        errs[f"A_full_grid_fd_width_{name}"] = canonical_scatter[name]
        errs[f"A_analytic_zero_{name}"] = canonical_zero[name]
        errs[f"A_analytic_zero_budget_{name}"] = canonical_zero_budget[name]
    if not np.isfinite(a["rhs_base"]).all() or not np.isfinite(a["vjp"]).all():
        raise AssertionError("native RHS or state-carry pullback is nonfinite")
    sizes = [ny * nz * (nx + 1), (ny + 1) * nz * nx,
             ny * (nz + 1) * nx, ny * (nz + 1) * nx, ny * nz * nx, ny * nx]
    physical_units = [1.0, 1.0, 1.0, 100.0, 1.0, 100.0]
    start = 0
    equilibrium_scaled = {}
    for name, size, unit in zip(("U", "V", "W", "PH", "THETA", "MU"), sizes, physical_units):
        value = float(np.max(np.abs(a["rhs_base"][start:start + size])) / unit)
        equilibrium_scaled[name] = value
        if value > 1.0e-12:
            raise AssertionError(f"native base {name} RHS scaled residual {value:.6g} exceeds 1e-12")
        start += size
    for steps in (1, 2):
        for block in ("W", "PH", "MU"):
            dot = scalars[f"dot{steps}_{block}"]
            fd_wide = scalars[f"fd{steps}_wide_{block}"]
            fd_narrow = scalars[f"fd{steps}_narrow_{block}"]
            scale = max(abs(dot), abs(fd_wide), abs(fd_narrow), 1.0e-12)
            adjoint_error = abs(dot - fd_narrow) / scale
            width_error = abs(fd_wide - fd_narrow) / scale
            floor = 256.0 * np.finfo(float).eps * max(abs(dot), abs(fd_wide), abs(fd_narrow), 1.0)
            if (not np.isfinite(adjoint_error) or max(abs(dot), abs(fd_wide), abs(fd_narrow)) <= floor or
                    adjoint_error > 1.0e-8 or width_error > 1.0e-8):
                raise AssertionError(f"{steps}-step {block} carry VJP/FD mismatch: dot={dot} wide={fd_wide} narrow={fd_narrow} adjoint_rel={adjoint_error} width_rel={width_error}")
            errs[f"{steps}step_{block}_vjp_fd"] = adjoint_error
            errs[f"{steps}step_{block}_fd_width"] = width_error
    adjoint_records = {}
    for steps in (1, 2):
        for block in ("W", "PH", "MU"):
            dot = scalars[f"dot{steps}_{block}"]
            wide = scalars[f"fd{steps}_wide_{block}"]
            narrow = scalars[f"fd{steps}_narrow_{block}"]
            scale = max(abs(dot), abs(wide), abs(narrow), 1.0e-12)
            adjoint_records[f"{steps}step_{block}"] = {
                "adjoint_dot": dot, "fd_wide_h_0_1": wide, "fd_narrow_h_0_05": narrow,
                "adjoint_relative_error": abs(dot - narrow) / scale,
                "fd_width_relative_scatter": abs(wide - narrow) / scale,
                "nonzero_signal_floor": 256.0 * np.finfo(float).eps *
                    max(abs(dot), abs(wide), abs(narrow), 1.0),
            }
    output_blocks = ("U", "W", "PH", "THETA", "MU")
    canonical = {
        name: {"scaled_source_error": canonical_worst[name],
               "fd_width_scatter": canonical_scatter[name], "budget": 1.0e-8}
        for name in output_blocks
    }
    mutation_budget = max(canonical_zero_budget.values())
    try:
        require_zero_response(2.0 * mutation_budget, mutation_budget, "manufactured zero-leak mutation")
        mutation_falsifier_passed = False
    except AssertionError:
        mutation_falsifier_passed = True
    if not mutation_falsifier_passed:
        raise AssertionError("absolute zero-response gate failed to reject a manufactured leak")
    evidence = {
        "dimensions": {"nx": nx, "ny": ny, "nz": nz, "lx_m": 40000.0, "ly_m": 30000.0},
        "native_build_info": build_info,
        "native_reciprocal_spacing_fp32_bits": {
            "rdx": build_info["rdx_fp32_bits"], "rdy": build_info["rdy_fp32_bits"],
        },
        "imported_phb_faces_float32_bits_hex": [
            f"0x{int(v):08x}" for v in np.asarray(phb_faces, dtype=np.float32).view(np.uint32)],
        "base_arrays": {"pbase_Pa_float32": first["pbase"].tolist(),
                        "thbase_perturb_K_float32": first["thbase_perturb"].tolist(),
                        "mubase_Pa_float32": first["mubase"].tolist()},
        "eos_gate": {"pressure_max_error_Pa": meta["eos_pressure_error"],
                     "alpha_relative_error": meta["eos_alpha_relative_error"],
                     "geometry_relative_error": meta["eos_geometry_relative_error"]},
        "divergence_damping": {"config_kdamp": meta["kdamp_config"],
                               "grid_kdamp_readback": meta["kdamp_grid"],
                               "implicit_divergence": bool(meta["implicit_divergence"])},
        "source_column": {"kappa_m_inv": float(c["kappa"]),
                          "theta_K": c["theta"].tolist(),
                          "layer_mass_kg_m2": c["layer_mass"].tolist(),
                          "N2_analytic_s_inv2": c["N2_analytic"].tolist(),
                          "layer_height_m": c["height"].tolist(),
                          "positive_layer_weights_kg_m2": c["layer_mass"].tolist(),
                          "selected_mode_omega_s_inv": frequency},
        "F0_physical_scaled_max": equilibrium_scaled,
        "canonical_source_columns_4N_plus_1": {
            "summary": canonical,
            "full_packed_field_columns": canonical_columns,
        "zero_response_gate": {
                "max_abs_error_narrow_by_output_block": canonical_zero,
                "max_abs_error_wide_by_output_block": canonical_zero_wide,
                "max_fd_width_scatter_by_output_block": canonical_zero_scatter,
                "max_abs_budget_by_output_block": canonical_zero_budget,
                "violations": zero_violations,
                "budget_formula": "max(16*eps64*max(rhs_plus_minus_wide)/h_wide,16*eps64*max(rhs_plus_minus_narrow)/h_narrow,64*eps64*declared_component_unit,2*zero_entry_fd_width_scatter_abs)",
            },
            "manufactured_zero_leak_rejected": mutation_falsifier_passed,
        },
        "carry_vjp_fd": adjoint_records,
        "input_state_sha256": hashlib.sha256(
            np.ascontiguousarray(a["initial"], dtype=np.float64).tobytes()).hexdigest(),
        "component_unit_floors": {"U_m_s": 1.0, "V_m_s": 1.0, "W_m_s": 1.0,
                                  "PH_m2_s2": 100.0, "THETA_K": 1.0, "MU_Pa": 100.0},
    }
    result = {"c": c, "nx": nx, "ny": ny, "meta": meta, "arrays": a, "scalars": scalars,
            "offsets": offsets, "equilibrium_scaled": equilibrium_scaled, "tangent_error": errs,
            "evidence": evidence,
            "canonical_columns": canonical_columns,
            "zero_violations": zero_violations,
            "canonical_metrics": {"source_error": canonical_worst, "fd_width_scatter": canonical_scatter,
                                  "analytic_zero_response_max_abs": canonical_zero,
                                  "analytic_zero_response_wide_max_abs": canonical_zero_wide,
                                  "analytic_zero_fd_width_scatter_max_abs": canonical_zero_scatter,
                                  "analytic_zero_fd_roundoff_budget_abs": canonical_zero_budget,
                                  "analytic_zero_violations": zero_violations,
                                  "manufactured_zero_leak_rejected": mutation_falsifier_passed},
            "project": lambda x: modal_source_vector(unpack_projection(x, nx, ny, nz, 40000.0, offsets), nz)}
    tmp.cleanup()
    return result


def run_damping_probe(exe: Path, component: str) -> dict:
    """Check signed damping in one U/V direction and its actual RHS adjoint dots."""
    with tempfile.TemporaryDirectory(prefix="native-damp-probe-") as tmp:
        payload=Path(tmp)/"probe.csv"
        command=[str(exe),"8","6","4",str(payload),"--damping-probe",component]
        completed=subprocess.run(command,check=False,capture_output=True,text=True)
        RUN_LOG.append(f"COMMAND: {' '.join(command)}\n{completed.stdout}{completed.stderr}\n")
        if completed.returncode:
            tail="\n".join((completed.stdout+completed.stderr).splitlines()[-80:])
            raise RuntimeError(f"damping probe {component} failed ({completed.returncode}):\n{tail}")
        meta,arrays,scalars,text_meta=read_payload(payload)
    expected_kdamp=float(np.float32(0.2))
    if text_meta.get("damping_component")!=component or meta["kdamp_config"]!=expected_kdamp or \
            meta["kdamp_grid"]!=expected_kdamp or meta["implicit_divergence"]!=1:
        raise AssertionError(f"{component} damping probe did not read back kdamp=.2/implicit_divergence=on")
    expected=arrays["damping_expected"]
    su,sv=6*4*9,7*4*8
    block=("U" if component=="U" else "V")
    start,size,unit=(0,su,1.0) if component=="U" else (su,sv,1.0)
    sl=slice(start,start+size)
    term_metrics={}
    for variant,fd_suffix,wide_name,narrow_name in (
        ("implicit_toggle","implicit","damping_implicit_path_wide","damping_implicit_path_narrow"),
        ("coefficient_toggle","coefficient","damping_coefficient_off_wide","damping_coefficient_off_narrow")):
        wide_delta=arrays[wide_name][sl]
        narrow_delta=arrays[narrow_name][sl]
        if variant=="coefficient_toggle":
            # The payload holds the raw coefficient-OFF tangent; compare the
            # active increment, just as the scalar VJP/FD contractions do.
            wide_delta=arrays["damping_on_wide"][sl]-wide_delta
            narrow_delta=arrays["damping_on_narrow"][sl]-narrow_delta
        roundoff_floor=64.0*np.finfo(float).eps*unit
        error=block_error(wide_delta,expected[sl],f"{component} {variant} signed damping increment",roundoff_floor,1.0e-8)
        width=block_error(wide_delta,narrow_delta,f"{component} {variant} damping increment FD width",roundoff_floor,1.0e-8)
        rate=float(scalars[f"damping_energy_rate_{variant}"])
        vjp=float(scalars[f"damping_vjp_dot_{variant}"])
        fdw=float(scalars[f"damping_fd_dot_{fd_suffix}_wide"])
        fdn=float(scalars[f"damping_fd_dot_{fd_suffix}_narrow"])
        jvp=float(scalars["damping_jvp_dot"])
        scale=max(abs(jvp),abs(rate),abs(vjp),abs(fdw),abs(fdn),1.0e-300)
        relative=max(abs(rate-jvp),abs(vjp-jvp),abs(fdw-jvp),abs(fdn-jvp))/scale
        if jvp>=0.0 or rate>=0.0 or vjp>=0.0 or fdw>=0.0 or fdn>=0.0 or relative>1.0e-8:
            raise AssertionError(f"{component} {variant} damping energy/JVP/VJP/FD sign or dot failed: "
                f"jvp={jvp} rate={rate} vjp={vjp} fd={fdw}/{fdn} rel={relative}")
        term_metrics[variant]={"fullfield_increment_relative_error":error,"fd_width_increment_relative_scatter":width,
            "increment_relative_budget":1.0e-8,"roundoff_unit_floor":roundoff_floor,
            "negative_energy_rate":rate,"analytic_jvp_dot":jvp,"vjp_dot":vjp,
            "fd_dot_wide":fdw,"fd_dot_narrow":fdn,"relative_dot_error":relative,
            "analytic_jvp_dot_budget":1.0e-8}
    full_implicit_abs=float(scalars["damping_full_vs_implicit_abs"])
    full_implicit_budget=float(scalars["damping_full_vs_implicit_budget"])
    if full_implicit_abs>full_implicit_budget:
        raise AssertionError(f"{component} Full/ImplicitOnly damping mismatch {full_implicit_abs} > {full_implicit_budget}")
    exclusions={}
    for mode in ("explicit_only","split_explicit"):
        error=float(scalars[f"damping_{mode}_abs"])
        budget=float(scalars[f"damping_{mode}_budget"])
        if error>budget:
            raise AssertionError(f"{component} {mode} did not exclude divergence damping: {error} > {budget}")
        exclusions[mode]={"max_abs_response":error,"fd_roundoff_budget":budget,
                          "fd_width_scatter":float(scalars[f"damping_{mode}_width_scatter"])}
    return {"component":component,"config_kdamp":meta["kdamp_config"],
        "grid_kdamp_readback":meta["kdamp_grid"],"implicit_divergence":bool(meta["implicit_divergence"]),
        "term_variants":term_metrics,
        "full_implicit_only_consistency":{"max_abs_error":full_implicit_abs,"fd_roundoff_budget":full_implicit_budget},
        "damping_exclusions":exclusions,
        "derivative_semantics":{"jvp":"independent analytical discrete damping operator",
                                 "vjp":"autograd of actual Full RHS ON minus OFF",
                                 "fd":"central finite difference of actual Full RHS ON minus OFF at two widths"}}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=Path, required=True, help="built test_native_wave_refinement executable")
    parser.add_argument("--output", type=Path, help="optional compact JSON evidence output")
    args = parser.parse_args()
    exe = args.exe.resolve()
    if not exe.is_file():
        raise SystemExit(f"native wave executable not found: {exe}")
    coarse = run_grid(exe, 8, 6, 4)
    fine = run_grid(exe, 16, 12, 8)
    damping_probe = {component: run_damping_probe(exe,component) for component in ("U","V")}
    propagation_records = {}
    for label, run in (("coarse", coarse), ("fine", fine)):
        c, arrays = run["c"], run["arrays"]
        print(f"NATIVE_WAVE grid={label} nx={run['nx']} ny={run['ny']} nz={c['nz']} "
              f"kappa={c['kappa']:.12g} mode_omega={run['meta']['mode_frequency']:.12g} "
              f"phb_source={c['phi_base_source']}")
        for name, value in run["equilibrium_scaled"].items():
            print(f"NATIVE_WAVE_BASE_RHS block={name} physical_scaled_max={value:.6e} budget=1.000000e-12")
        for name, value in run["tangent_error"].items():
            print(f"NATIVE_WAVE_GATE check={name} scaled_error={value:.6e} budget=1.000000e-08")
        for name in ("U", "V", "W", "PH", "THETA", "MU"):
            metrics=run["canonical_metrics"]
            violated=sum(v["output_block"]==name for v in run["zero_violations"])
            print(f"NATIVE_WAVE_FULLFIELD block={name} source_error={metrics['source_error'].get(name,0.0):.6e} "
                  f"fd_scatter={metrics['fd_width_scatter'].get(name,0.0):.6e} "
                  f"analytic_zero_narrow={metrics['analytic_zero_response_max_abs'].get(name,0.0):.6e} "
                  f"analytic_zero_budget={metrics['analytic_zero_fd_roundoff_budget_abs'].get(name,0.0):.6e} "
                  f"violations={violated}")
        # A source-selected eigenmode is advanced through 90 accepted native
        # steps, then compared with the same-grid matrix exponential per block.
        q0 = run["project"](arrays["initial"])
        q1 = run["project"](arrays["prop90"])
        propagation_time = run["meta"]["dt"] * run["meta"]["propagation_steps"]
        predicted = expm(propagation_time * c["A"]) @ q0
        blocks = [("U", slice(0, c["nz"]), 1.0),
                  ("W", slice(c["nz"], 2*c["nz"]), 1.0),
                  ("PH", slice(2*c["nz"], 3*c["nz"]), 100.0),
                  ("THETA", slice(3*c["nz"], 4*c["nz"]), 1.0),
                  ("MU", slice(4*c["nz"], 4*c["nz"]+1), 100.0)]
        for name, sl, floor in blocks:
            err = block_error(q1[sl], predicted[sl], f"{label} propagation {name}", floor, 1.0e-8)
            propagation_records.setdefault(label, {})[name] = {
                "scaled_error": err, "budget": 1.0e-8, "unit_floor": floor,
                "native_signal_norm": float(np.linalg.norm(q1[sl])),
                "reference_signal_norm": float(np.linalg.norm(predicted[sl])),
            }
            print(f"NATIVE_WAVE_PROPAGATION block={name} scaled_error={err:.6e} budget=1.000000e-08 "
                  f"signal={np.linalg.norm(q1[sl]):.6e} time_s={propagation_time:.1f}")
        run["evidence"]["propagation"] = {
            "steps": int(run["meta"]["propagation_steps"]), "dt_s": run["meta"]["dt"],
            "duration_s": propagation_time,
            "phase_rad": run["meta"]["mode_frequency"] * propagation_time,
            "blocks": propagation_records[label],
        }
    all_zero_violations = coarse["zero_violations"] + fine["zero_violations"]
    pass_status = not all_zero_violations
    if pass_status:
        print("NATIVE_WAVE_REFINEMENT PASS: all source columns, carry adjoints and 90-second eigenmode propagation pass")
    else:
        print(f"NATIVE_WAVE_REFINEMENT FAIL: {len(all_zero_violations)} analytic-zero fullfield responses exceed the RHS-derived FD roundoff bound")
    print("NATIVE_WAVE_DAMPING_PROBE " + json.dumps(damping_probe,sort_keys=True))
    if args.output:
        output_path = args.output.resolve()
        output_path.parent.mkdir(parents=True, exist_ok=True)
        try:
            source_revision = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT,
                                             check=True, capture_output=True, text=True).stdout.strip()
        except (subprocess.SubprocessError, FileNotFoundError):
            source_revision = "unavailable"
        evidence = {
            "schema": "native-wave-refinement-v1", "status": "pass" if pass_status else "fail",
            "source_revision": source_revision,
            "runtime": {"python": platform.python_version(), "numpy": np.__version__,
                        "scipy": scipy.__version__,
                        "native_torch": coarse["evidence"]["native_build_info"]["native_torch_version"],
                        "native_compiler": coarse["evidence"]["native_build_info"]["native_compiler_version"]},
            "artifacts": {"executable": str(exe), "executable_sha256": sha256_file(exe),
                          "cpp_sha256": sha256_file(ROOT / "external/libtorch_wrf/sdirk3/tests/test_native_wave_refinement.cpp"),
                          "python_sha256": sha256_file(Path(__file__).resolve()),
                          "source_reference_sha256": sha256_file(REF_PATH),
                          "execution_log": str(output_path.with_suffix(".log"))},
            "contracts": {"F0_physical_scaled_budget": 1.0e-12,
                          "canonical_source_scaled_budget": 1.0e-8,
                          "carry_vjp_fd_relative_budget": 1.0e-8,
                          "native_propagation_scaled_budget": 1.0e-8,
                          "rhs_fd_widths": [1.0e-3, 5.0e-4],
                          "carry_fd_widths": [0.1, 0.05],
                          "reference_propagation": "source-column matrix exponential"},
            "divergence_damping_term_identification": {
                "formula": "positive kdamp*grad(div) gives negative discrete Laplacian on U/V modes",
                "probes": damping_probe,
                "closure": "signed expected U/V tendency, negative kinetic-energy rate, isolated ON-OFF reverse-mode directional VJP and two-width FD dots",
            },
            "grids": {"coarse": coarse["evidence"], "fine": fine["evidence"]},
        }
        output_path.write_text(json.dumps(evidence, indent=2, sort_keys=True) + "\n")
        output_path.with_suffix(".log").write_text("\n".join(RUN_LOG))
    if not pass_status:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
