"""Full-Y linear dry-wave source with one constant-f traditional Coriolis term.

The established 33-component source RHS remains authoritative per Y mass row.
This module adds only full-Y mass-flux divergence, its source pressure-gradient
adjoint, and the fixed-X native coupled-momentum f-plane stencil. It is for a
flat, periodic-X/symmetric-Y, fixed uniform background with e=0, no AD/Jacobian,
and no nonlinear native trajectory claim.

State order is 12 rows of old [u(8),w(8),phi(8),theta(8),mu], then V on the
11 interior Y faces with 8 levels. V wall values are omitted and exactly zero.
"""

from __future__ import annotations

import struct

import numpy as np
from scipy.linalg import expm

from wave_energy_spatial_reference import _rhs as source_rhs_1d
import test_fine_wave_inverse as inverse
import wave_y_reference as y_reference


NY_DEFAULT = 12
W_AMPLITUDE = 0.01


def fp32_coefficients(
    descriptor_meta: dict,
    descriptor_text: dict,
    ny: int = NY_DEFAULT,
    ly: float = 30_000.0,
    nx: int = 16,
) -> tuple[float, float]:
    """Read the signed uniform FP32 f plane and exact FP32 reciprocal-dy bits.

    ``f_input_max_abs`` alone cannot establish sign or spatial constancy. The
    test-only native descriptor therefore reports rotating-wave enablement,
    effective f-field min/max and shape; all must prove a positive constant
    field. e must remain zero and rdy is decoded from the native FP32 bits.
    """
    enabled = descriptor_meta.get("rotating_wave_test_enabled")
    f_min = float(descriptor_meta.get("coriolis_f_effective_field_min", np.nan))
    f_max = float(descriptor_meta.get("coriolis_f_effective_field_max", np.nan))
    f_abs = float(descriptor_meta.get("f_input_max_abs", np.nan))
    e_min = float(descriptor_meta.get("coriolis_e_effective_field_min", np.nan))
    e_max = float(descriptor_meta.get("coriolis_e_effective_field_max", np.nan))
    e_abs = float(descriptor_meta.get("e_input_max_abs", np.nan))
    sina_min = float(descriptor_meta.get("coriolis_sina_effective_field_min", np.nan))
    sina_max = float(descriptor_meta.get("coriolis_sina_effective_field_max", np.nan))
    cosa_min = float(descriptor_meta.get("coriolis_cosa_effective_field_min", np.nan))
    cosa_max = float(descriptor_meta.get("coriolis_cosa_effective_field_max", np.nan))
    field_nx = descriptor_meta.get("coriolis_f_effective_field_nx")
    field_ny = descriptor_meta.get("coriolis_f_effective_field_ny")
    if (
        enabled != 1.0
        or field_nx != float(nx)
        or field_ny != float(ny)
        or not np.isfinite(
            [
                f_min,
                f_max,
                f_abs,
                e_min,
                e_max,
                e_abs,
                sina_min,
                sina_max,
                cosa_min,
                cosa_max,
            ]
        ).all()
        or f_min <= 0.0
        or f_min != f_max
        or f_abs != f_max
        or e_min != 0.0
        or e_max != 0.0
        or e_abs != 0.0
        or sina_min != 0.0
        or sina_max != 0.0
        or cosa_min != 1.0
        or cosa_max != 1.0
    ):
        raise ValueError(
            "descriptor does not prove constant positive f with e=sina=0 and cosa=1"
        )
    f = float(np.float32(f_min))
    if f != f_min:
        raise ValueError("descriptor Coriolis f is not an exact FP32 value")
    bits_text = descriptor_text.get("rdy_fp32_bits", "")
    if not bits_text.lower().startswith("0x"):
        raise ValueError("descriptor is missing rdy_fp32_bits")
    bits = int(bits_text, 16)
    rdy = float(struct.unpack(">f", struct.pack(">I", bits))[0])
    dy = float(ly) / int(ny)
    expected_rdy = float(np.float32(1.0 / float(np.float32(dy))))
    if not np.isfinite(rdy) or rdy <= 0.0 or rdy != expected_rdy:
        raise ValueError("descriptor rdy bits do not match the fixed full-Y spacing")
    return f, rdy


def validate_descriptor_symbols(
    column: dict, descriptor_text: dict, ny: int = NY_DEFAULT
) -> dict:
    """Verify source Kx/rdy are the exact native descriptor FP32 reciprocals."""

    def decode(key: str) -> float:
        value = descriptor_text.get(key, "")
        if not value.lower().startswith("0x"):
            raise ValueError(f"descriptor is missing {key}")
        return float(struct.unpack(">f", struct.pack(">I", int(value, 16)))[0])

    rdx = decode("rdx_fp32_bits")
    rdy = decode("rdy_fp32_bits")
    if float(column["rdx_fp32"]) != rdx:
        raise ValueError(
            "source column rdx differs from the native descriptor FP32 value"
        )
    dy = float(column["ly"]) / ny
    if rdy != float(np.float32(1.0 / float(np.float32(dy)))):
        raise ValueError("descriptor rdy bits do not match the full-Y grid spacing")
    kx = 2.0 * np.sin(0.5 * float(column["k_physical"]) * float(column["dx"])) * rdx
    if float(column["kappa"]) != kx:
        raise ValueError("source discrete Kx does not use descriptor rdx")
    return {"rdx_fp32": rdx, "rdy_fp32": rdy, "kx_discrete": kx}


def state_size(column: dict, ny: int = NY_DEFAULT) -> int:
    return int(ny) * (4 * int(column["nz"]) + 1) + (int(ny) - 1) * int(column["nz"])


def _unpack(column: dict, state: np.ndarray, ny: int) -> tuple[np.ndarray, np.ndarray]:
    nz = int(column["nz"])
    packed = np.asarray(state, dtype=np.complex128)
    size = state_size(column, ny)
    if packed.shape != (size,) or not np.isfinite(packed).all():
        raise ValueError(f"expected finite full-Y source state of length {size}")
    rows_end = ny * (4 * nz + 1)
    return packed[:rows_end].reshape(ny, 4 * nz + 1), packed[rows_end:].reshape(
        ny - 1, nz
    )


def _pack(rows: np.ndarray, v_inner: np.ndarray) -> np.ndarray:
    return np.concatenate(
        (
            np.asarray(rows, dtype=np.complex128).ravel(),
            np.asarray(v_inner, dtype=np.complex128).ravel(),
        )
    )


def _validate(
    column: dict, ny: int, rdy_fp32: float, f_fp32: float
) -> tuple[int, float, float]:
    nz = int(column["nz"])
    if ny < 2:
        raise ValueError("ny must be at least 2")
    kx = float(column["kappa"])
    if not np.isfinite(kx) or kx <= 0.0:
        raise ValueError("full-Y lift requires the existing positive periodic-X Kx")
    if (
        not np.isfinite(rdy_fp32)
        or rdy_fp32 <= 0.0
        or float(np.float32(rdy_fp32)) != float(rdy_fp32)
    ):
        raise ValueError("rdy must be the exact positive FP32 descriptor coefficient")
    if (
        not np.isfinite(f_fp32)
        or f_fp32 < 0.0
        or float(np.float32(f_fp32)) != float(f_fp32)
    ):
        raise ValueError("f must be a nonnegative exact FP32 coefficient")
    return nz, kx, float(f_fp32)


def _pressure_potential(column: dict, old_rhs: np.ndarray) -> np.ndarray:
    """Recover S=phi_mass+alpha_bar*dp from the authoritative U source row."""
    kx = float(column["kappa"])
    nz = int(column["nz"])
    return old_rhs[:nz] / (-1j * kx)


def _coriolis_u_v(
    column: dict, rows: np.ndarray, v_inner: np.ndarray, f_fp32: float
) -> tuple[np.ndarray, np.ndarray]:
    """Traditional f-only slice of wrf_coriolis_tendencies in source velocities."""
    ny, nz = rows.shape[0], int(column["nz"])
    kx_phys = float(column["k_physical"])
    dx = float(column["dx"])
    cx = float(np.cos(0.5 * kx_phys * dx))
    v_faces = np.zeros((ny + 1, nz), dtype=np.complex128)
    v_faces[1:ny] = v_inner
    u = rows[:, :nz]
    alpha = np.asarray(column["layer_mass"], dtype=np.float64)
    if alpha.shape != (nz,) or not np.isfinite(alpha).all() or np.any(alpha <= 0.0):
        raise ValueError("fixed background layer-mass coefficients are invalid")
    # Native raw coupled rates divide by alpha_u/alpha_v in the physical
    # velocity accumulator. Flat fixed maps give alpha_u=alpha_v=layer_mass.
    u_ratio = alpha / alpha
    v_ratio = alpha / alpha
    udot = float(f_fp32) * cx * 0.5 * (v_faces[:-1] + v_faces[1:]) * u_ratio[None, :]
    vdot = -float(f_fp32) * cx * 0.5 * (u[:-1] + u[1:]) * v_ratio[None, :]
    return udot, vdot


def full_rhs(
    column: dict,
    state: np.ndarray,
    *,
    f_fp32: float,
    rdy_fp32: float,
    ny: int = NY_DEFAULT,
) -> np.ndarray:
    """Full-Y fixed-X source RHS: old vertical rows plus exact Y/f closures."""
    nz, kx, f = _validate(column, ny, rdy_fp32, f_fp32)
    rows, v_inner = _unpack(column, state, ny)
    v_faces = np.zeros((ny + 1, nz), dtype=np.complex128)
    v_faces[1:ny] = v_inner
    dyv = rdy_fp32 * (v_faces[1:] - v_faces[:-1])
    rhs_rows = np.empty_like(rows)
    pressure = np.empty((ny, nz), dtype=np.complex128)
    for j in range(ny):
        q = rows[j].copy()
        q[:nz] += dyv[j] / (1j * kx)
        rhs_rows[j] = source_rhs_1d(column, q)
        pressure[j] = _pressure_potential(column, rhs_rows[j])
    # V momentum is -Gy(S); interior face j sits between mass rows j-1,j.
    vdot_pressure = -rdy_fp32 * (pressure[1:] - pressure[:-1])
    coriolis_u, coriolis_v = _coriolis_u_v(column, rows, v_inner, f)
    rhs_rows[:, :nz] += coriolis_u
    return _pack(rhs_rows, vdot_pressure + coriolis_v)


def full_matrix(
    column: dict, *, f_fp32: float, rdy_fp32: float, ny: int = NY_DEFAULT
) -> np.ndarray:
    """Assemble the small 484x484 source operator from the linear RHS."""
    size = state_size(column, ny)
    matrix = np.empty((size, size), dtype=np.complex128)
    for j in range(size):
        basis = np.zeros(size, dtype=np.complex128)
        basis[j] = 1.0
        matrix[:, j] = full_rhs(column, basis, f_fp32=f_fp32, rdy_fp32=rdy_fp32, ny=ny)
    return matrix


def embed_old41_f0(
    column: dict, q41: np.ndarray, *, ny: int = NY_DEFAULT
) -> np.ndarray:
    """Lift the previous fundamental cos/sin mode into the full-Y grid."""
    nz = int(column["nz"])
    q = np.asarray(q41, dtype=np.complex128)
    if q.shape != (5 * nz + 1,) or not np.isfinite(q).all():
        raise ValueError("old Y mode must be a finite 5*nz+1 vector")
    dy, ly = float(column["ly"]) / ny, float(column["ly"])
    y_mass = (np.arange(ny) + 0.5) * dy
    y_face = np.arange(1, ny) * dy
    cy = np.cos(np.pi * y_mass / ly)
    sy = np.sin(np.pi * y_face / ly)
    rows = np.zeros((ny, 4 * nz + 1), dtype=np.complex128)
    rows[:, :nz] = cy[:, None] * q[:nz]
    rows[:, nz : 2 * nz] = cy[:, None] * q[2 * nz : 3 * nz]
    rows[:, 2 * nz : 3 * nz] = cy[:, None] * q[3 * nz : 4 * nz]
    rows[:, 3 * nz : 4 * nz] = cy[:, None] * q[4 * nz : 5 * nz]
    rows[:, -1] = cy * q[-1]
    v_inner = sy[:, None] * q[nz : 2 * nz][None, :]
    return _pack(rows, v_inner)


def verify_f0_lift(column: dict, *, rdy_fp32: float, ny: int = NY_DEFAULT) -> dict:
    """Verify roundoff-level invariant embedding of the existing 41-state mode at f=0."""
    nz = int(column["nz"])
    p = np.column_stack(
        [
            embed_old41_f0(column, np.eye(5 * nz + 1, dtype=np.complex128)[:, j], ny=ny)
            for j in range(5 * nz + 1)
        ]
    )
    full0 = full_matrix(column, f_fp32=0.0, rdy_fp32=rdy_fp32, ny=ny)
    old = y_reference.source_y_matrix(column, ny)
    lifted_full = full0 @ p
    lifted_old = p @ old
    residual = lifted_full - lifted_old
    max_abs = float(np.max(np.abs(residual)))
    return {
        "full_shape": list(full0.shape),
        "old_shape": list(old.shape),
        "embedding_shape": list(p.shape),
        "max_abs_invariant_subspace_residual": max_abs,
        "roundoff_tolerance": 2.0e-14,
        "passed": bool(max_abs < 2.0e-14),
        "bitwise_exact": bool(np.array_equal(lifted_full, lifted_old)),
    }


def uniform_energy_metric(column: dict, ny: int = NY_DEFAULT) -> np.ndarray:
    """Extend the existing source energy metric with uniform-y V kinetic weights."""
    nz = int(column["nz"])
    layer_mass = np.asarray(column["layer_mass"], dtype=np.float64)
    if layer_mass.shape != (nz,) or not np.all(layer_mass == layer_mass[0]):
        raise ValueError(
            "energy identity is asserted only for a uniform fixed background"
        )
    # Rows contain source [u,w,phi,theta,mu], so use H = T^* H_physical T.
    h33 = np.asarray(column["H"], dtype=np.complex128)
    if h33.shape != (4 * nz + 1, 4 * nz + 1):
        raise ValueError("source energy metric has an unexpected shape")
    size = state_size(column, ny)
    hfull = np.zeros((size, size), dtype=np.complex128)
    nrow = 4 * nz + 1
    scale = 1.0 / ny
    for j in range(ny):
        sl = slice(j * nrow, (j + 1) * nrow)
        hfull[sl, sl] = scale * h33
    v0 = ny * nrow
    u_kinetic = np.real(np.diag(h33)[:nz]) * scale
    for face in range(ny - 1):
        for k in range(nz):
            index = v0 + face * nz + k
            hfull[index, index] = u_kinetic[k]
    return hfull


def verify_uniform_f_energy_identity(
    column: dict, *, f_fp32: float, rdy_fp32: float, ny: int = NY_DEFAULT
) -> dict:
    """Check only the uniform-background physical kinetic Coriolis work identity."""
    nz = int(column["nz"])
    if not np.all(np.asarray(column["layer_mass"]) == column["layer_mass"][0]):
        raise ValueError("do not extend this f-energy identity to variable layer mass")
    a0 = full_matrix(column, f_fp32=0.0, rdy_fp32=rdy_fp32, ny=ny)
    af = full_matrix(column, f_fp32=f_fp32, rdy_fp32=rdy_fp32, ny=ny)
    cf = af - a0
    size = state_size(column, ny)
    h = uniform_energy_metric(column, ny)
    u_idx = np.concatenate([j * (4 * nz + 1) + np.arange(nz) for j in range(ny)])
    v_idx = ny * (4 * nz + 1) + np.arange((ny - 1) * nz)
    allowed = np.zeros((size, size), dtype=bool)
    allowed[np.ix_(u_idx, v_idx)] = True
    allowed[np.ix_(v_idx, u_idx)] = True
    outside = np.array(cf, copy=True)
    outside[allowed] = 0.0
    uv_idx = np.r_[u_idx, v_idx]
    h_uv = h[np.ix_(uv_idx, uv_idx)]
    cf_uv = cf[np.ix_(uv_idx, uv_idx)]
    weighted_skew = h_uv @ cf_uv + cf_uv.conj().T @ h_uv
    skew_denom = max(
        float(np.linalg.norm(h_uv, ord=np.inf) * np.linalg.norm(cf_uv, ord=np.inf)),
        np.finfo(float).tiny,
    )
    skew_residual = float(np.linalg.norm(weighted_skew, ord=np.inf) / skew_denom)
    rng = np.random.default_rng(1729)
    q = np.zeros(size, dtype=np.complex128)
    q[u_idx] = rng.standard_normal(u_idx.size) + 1j * rng.standard_normal(u_idx.size)
    q[v_idx] = rng.standard_normal(v_idx.size) + 1j * rng.standard_normal(v_idx.size)
    power = float(np.real(np.vdot(q, h @ (cf @ q))))
    kinetic_energy = float(np.real(np.vdot(q, h @ q)))
    power_residual = abs(power) / max(
        abs(float(f_fp32)) * kinetic_energy, np.finfo(float).tiny
    )
    return {
        "uniform_layer_mass": True,
        "e_sina_zero_cosa_one_assumed": True,
        "Coriolis_rows_outside_u_v_max_abs": float(np.max(np.abs(outside))),
        "kinetic_power_real": power,
        "relative_kinetic_power_residual": power_residual,
        "weighted_Coriolis_skew_residual_on_u_v": skew_residual,
        "passed": bool(
            np.max(np.abs(outside)) == 0.0
            and power_residual < 2e-13
            and skew_residual < 2e-13
        ),
    }


def full_energy_metric(column: dict, ny: int = NY_DEFAULT) -> np.ndarray:
    """Return the block-diagonal physical inner product used for mode overlap."""
    return uniform_energy_metric(column, ny)


def _block_slices(ny: int, nz: int) -> dict:
    row = 4 * nz + 1
    u = np.concatenate([j * row + np.arange(nz) for j in range(ny)])
    w = np.concatenate([j * row + nz + np.arange(nz) for j in range(ny)])
    phi = np.concatenate([j * row + 2 * nz + np.arange(nz) for j in range(ny)])
    theta = np.concatenate([j * row + 3 * nz + np.arange(nz) for j in range(ny)])
    mu = np.arange(ny) * row + 4 * nz
    v = ny * row + np.arange((ny - 1) * nz)
    return {"u": u, "v": v, "w": w, "phi": phi, "theta": theta, "mu": mu}


def _block_metrics(q: np.ndarray, ny: int, nz: int) -> dict:
    units = {
        "u": "m s-1",
        "v": "m s-1",
        "w": "m s-1",
        "phi": "m2 s-2",
        "theta": "K",
        "mu": "Pa",
    }
    return {
        name: {
            "unit": units[name],
            "rms": float(np.sqrt(np.mean(np.abs(q[idx]) ** 2))),
            "max_abs": float(np.max(np.abs(q[idx]))),
        }
        for name, idx in _block_slices(ny, nz).items()
    }


def select_seeded_inertia_gravity_mode(
    matrix: np.ndarray,
    column: dict,
    seed: np.ndarray,
    *,
    f_fp32: float,
    ny: int = NY_DEFAULT,
) -> dict:
    """Select the stable sub-Nmax branch of greatest physical-energy overlap."""
    a = np.asarray(matrix, dtype=np.complex128)
    nz = int(column["nz"])
    size = state_size(column, ny)
    if a.shape != (size, size):
        raise ValueError("full-Y matrix has unexpected shape")
    qseed = np.asarray(seed, dtype=np.complex128)
    if qseed.shape != (size,):
        raise ValueError("continuation seed has unexpected shape")
    h = full_energy_metric(column, ny)
    seed_norm = float(np.real(np.vdot(qseed, h @ qseed)))
    if seed_norm <= 0.0:
        raise ValueError("continuation seed has nonpositive physical norm")
    nmax = float(np.max(np.sqrt(column["g"] / column["theta"] * column["theta_z"])))
    vals, vecs = np.linalg.eig(a)
    blocks = _block_slices(ny, nz)
    candidates = []
    for j, lam in enumerate(vals):
        omega = float(lam.imag)
        if not (float(f_fp32) < omega <= nmax):
            continue
        if abs(lam.real) > 1e-8 * max(1.0, abs(omega)):
            continue
        v = vecs[:, j].astype(np.complex128)
        if (
            np.max(np.abs(v[blocks["v"]])) == 0.0
            or np.max(np.abs(v[blocks["w"]])) == 0.0
        ):
            continue
        norm = float(np.real(np.vdot(v, h @ v)))
        if norm <= 0.0:
            continue
        score = float(abs(np.vdot(qseed, h @ v)) / np.sqrt(seed_norm * norm))
        candidates.append((score, omega, j, lam, v))
    if not candidates:
        raise ValueError("no stable inertia-gravity branch overlaps the f=0 seed")
    score, omega, index, lam, q = max(candidates, key=lambda row: row[0])
    pivot = int(blocks["w"][np.argmax(np.abs(q[blocks["w"]]))])
    q *= np.exp(-1j * np.angle(q[pivot]))
    wmax = float(np.max(np.abs(q[blocks["w"]])))
    q /= wmax
    residual = a @ q - lam * q
    per_block = {}
    rhs = a @ q
    eig = lam * q
    for name, idx in blocks.items():
        denom = max(
            float(np.max(np.abs(rhs[idx]))),
            float(np.max(np.abs(eig[idx]))),
            np.finfo(float).tiny,
        )
        per_block[name] = float(np.max(np.abs(residual[idx])) / denom)
    return {
        "matrix": a,
        "eigenvalue": complex(lam),
        "angular_frequency": omega,
        "nmax": nmax,
        "ny": ny,
        "nz": nz,
        "overlap_with_f0_seed": score,
        "eigenvector": q,
        "eigenpair_relative_residual_by_block": per_block,
        "eigenvector_block_metrics": _block_metrics(q, ny, nz),
        "near_zero_eigenvalue_count": int(np.count_nonzero(np.abs(vals) < 1e-8)),
    }


def time_quadrature_basis(
    mode: dict, w_amplitude: float = W_AMPLITUDE
) -> tuple[tuple[np.ndarray, np.ndarray], dict]:
    """Use q and i*q as the real-field X phase controls of one eigenmode."""
    q = np.asarray(mode["eigenvector"], dtype=np.complex128)
    a = np.asarray(mode["matrix"], dtype=np.complex128)
    ny = int(mode["ny"])
    nz = int(mode["nz"])
    blocks = _block_slices(ny, nz)
    scale = float(w_amplitude) / float(np.max(np.abs(q[blocks["w"]])))
    qa = scale * q
    qb = 1j * qa
    omega = float(mode["angular_frequency"])
    ra = a @ qa - 1j * omega * qa
    rb = a @ qb + omega * qa
    basis = (qa, qb)
    residuals = {}
    for name, idx in blocks.items():
        denom = max(float(np.max(np.abs((1j * omega * qa)[idx]))), np.finfo(float).tiny)
        residuals[name] = float(
            max(np.max(np.abs(ra[idx])), np.max(np.abs(rb[idx]))) / denom
        )
    return basis, {
        "w_max_abs": float(w_amplitude),
        "eigenmode_quadratures": "q and i*q",
        "q_evolution_identity_residual_by_block": residuals,
        "control_a_block_metrics": _block_metrics(qa, ny, nz),
        "control_b_block_metrics": _block_metrics(qb, ny, nz),
    }


def physical_w_samples(
    column: dict,
    q: np.ndarray,
    xyz: np.ndarray,
    *,
    linear_geometry: bool = False,
    ny: int = NY_DEFAULT,
) -> np.ndarray:
    """Use the existing full packed-state W observer for a full-Y source state.

    q is the 484-state perturbation. The balanced column's total equilibrium
    geopotential is supplied as synthetic PHB; the native observer then adds
    q's PH block. For H_BG, zero only the perturbation PH block and preserve W.
    """
    nx, nz = int(column["nx"]), int(column["nz"])
    perturbation = pack_native_state(column, q, ny=ny)
    if linear_geometry:
        sizes = (
            ny * nz * (nx + 1),
            (ny + 1) * nz * nx,
            ny * (nz + 1) * nx,
            ny * (nz + 1) * nx,
            ny * nz * nx,
            ny * nx,
        )
        ph_offset = int(sum(sizes[:3]))
        ph_size = int(sizes[3])
        perturbation = perturbation.copy()
        perturbation[ph_offset : ph_offset + ph_size] = 0.0
    phb = np.broadcast_to(
        np.asarray(column["phi_total_w"], dtype=np.float64)[None, :, None],
        (ny, nz + 1, nx),
    ).copy()
    return inverse.numpy_physical_w_prediction(
        (nx, ny, nz),
        phb,
        perturbation,
        np.asarray(xyz, dtype=np.float64),
        float(column["g"]),
    )[0]


def source_observations(
    column: dict,
    initial_state: np.ndarray,
    matrix: np.ndarray,
    xyz: np.ndarray,
    times: tuple[float, ...] = (150.0, 300.0),
    *,
    linear_geometry: bool = False,
    ny: int = NY_DEFAULT,
) -> np.ndarray:
    state = np.asarray(initial_state, dtype=np.complex128)
    if state.shape != (state_size(column, ny),):
        raise ValueError("source initial state has invalid full-Y shape")
    return np.stack(
        [
            physical_w_samples(
                column,
                expm(float(t) * matrix) @ state,
                xyz,
                linear_geometry=linear_geometry,
                ny=ny,
            )
            for t in times
        ]
    )


def continuation_seed(column: dict, *, rdy_fp32: float, ny: int = NY_DEFAULT) -> dict:
    """Lift the existing f=0 41-state fundamental mode into full-Y."""
    old_matrix = y_reference.source_y_matrix(column, ny)
    old_mode = y_reference.select_internal_mode(old_matrix, column)
    q_full = embed_old41_f0(column, old_mode["eigenvector"], ny=ny)
    return {
        "state": q_full,
        "old_mode": old_mode,
        "old_matrix": old_matrix,
        "f0_lift": verify_f0_lift(column, rdy_fp32=rdy_fp32, ny=ny),
    }


def source_observation_matrix(
    column: dict,
    basis: tuple[np.ndarray, ...],
    matrix: np.ndarray,
    xyz: np.ndarray,
    times: tuple[float, ...] = (150.0, 300.0),
    *,
    ny: int = NY_DEFAULT,
) -> np.ndarray:
    """Build base-PH H_BG exp(t*A) sensitivities for any small source basis."""
    if not basis:
        raise ValueError("source observation basis must not be empty")
    columns = []
    for control in basis:
        obs = source_observations(
            column, control, matrix, xyz, times, linear_geometry=True, ny=ny
        )
        columns.append(np.concatenate(obs))
    return np.column_stack(columns)


def source_observation_xyz(column: dict) -> np.ndarray:
    """Use the established 105 fixed physical XYZ rows for the Y-wave case."""
    return y_reference.source_observation_xyz(column)


def pack_native_state(
    column: dict, q: np.ndarray, *, ny: int = NY_DEFAULT
) -> np.ndarray:
    """Pack 484 source coefficients into native [U,V,W,PH,theta,MU] order."""
    nz, nx = int(column["nz"]), int(column["nx"])
    rows, v_inner = _unpack(column, q, ny)
    lx = float(column["lx"])
    dx = lx / nx
    kx = float(column["k_physical"])
    xu = np.arange(nx) * dx
    xc = (np.arange(nx) + 0.5) * dx
    phase_u = np.exp(1j * kx * xu)
    phase_c = np.exp(1j * kx * xc)
    sizes = (
        ny * nz * (nx + 1),
        (ny + 1) * nz * nx,
        ny * (nz + 1) * nx,
        ny * (nz + 1) * nx,
        ny * nz * nx,
        ny * nx,
    )
    out = np.zeros(sum(sizes), dtype=np.float64)
    off = np.r_[0, np.cumsum(sizes)].astype(int)
    u = out[off[0] : off[1]].reshape(ny, nz, nx + 1)
    v = out[off[1] : off[2]].reshape(ny + 1, nz, nx)
    w = out[off[2] : off[3]].reshape(ny, nz + 1, nx)
    ph = out[off[3] : off[4]].reshape(ny, nz + 1, nx)
    th = out[off[4] : off[5]].reshape(ny, nz, nx)
    mu = out[off[5] : off[6]].reshape(ny, nx)
    for j in range(ny):
        row = rows[j]
        for k in range(nz):
            u[j, k, :nx] = np.real(row[k] * phase_u)
            u[j, k, nx] = u[j, k, 0]
            w[j, k + 1] = np.real(row[nz + k] * phase_c)
            ph[j, k + 1] = np.real(row[2 * nz + k] * phase_c)
            th[j, k] = np.real(row[3 * nz + k] * phase_c)
        mu[j] = np.real(row[-1] * phase_c)
    v[1:ny] = np.real(v_inner[:, :, None] * phase_c[None, None, :])
    if (
        np.count_nonzero(v[0])
        or np.count_nonzero(v[-1])
        or np.count_nonzero(w[:, 0])
        or np.count_nonzero(ph[:, 0])
    ):
        raise AssertionError("Y-wall V and fixed lower W/PH faces must be exactly zero")
    if not np.array_equal(u[:, :, -1], u[:, :, 0]):
        raise AssertionError("periodic U x-face alias mismatch")
    return out
