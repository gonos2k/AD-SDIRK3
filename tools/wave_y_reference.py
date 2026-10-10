"""Independent source-coordinate reference for one periodic-X/symmetric-Y dry mode.

The existing 1-D source RHS remains authoritative for EOS, vertical pressure
forces, Omega, W, PH, theta, and mass. This module lifts that exact RHS to the
fundamental Y standing mode. It does not use native AD, Jacobians, or a
continuum sqrt(Kx**2 + Ky**2) replacement.

Modal layout is [u(nz), v(nz), w(nz), phi(nz), theta(nz), mu]. X uses the
existing complex periodic Fourier mode. Scalars and tangential components use
cos(pi*y/Ly) at Y mass points; V uses sin(pi*y/Ly) at symmetric Y faces.
"""
from __future__ import annotations

import numpy as np
from scipy.linalg import expm

from wave_energy_spatial_reference import _rhs as source_rhs_1d
from wave_energy_spatial_reference import source_matrix as source_matrix_1d


_BLOCKS = ("u", "v", "w", "phi", "theta", "mu")
_UNITS = {"u": "m s-1", "v": "m s-1", "w": "m s-1",
          "phi": "m2 s-2", "theta": "K", "mu": "Pa"}


def _symbols(column: dict, ny: int) -> tuple[float, float, float, float]:
    """Return Kx, fundamental Ky, physical ky, and dy using native FP32 rdy."""
    if ny < 2:
        raise ValueError("ny must be at least 2")
    lx, ly = float(column["lx"]), float(column["ly"])
    if not np.isfinite(lx + ly) or lx <= 0.0 or ly <= 0.0:
        raise ValueError("column lx/ly must be finite and positive")
    kx = float(column["kappa"])
    if not np.isfinite(kx) or kx <= 0.0:
        raise ValueError("the source-coordinate Y lift requires Kx > 0")
    ky_physical = np.pi / ly
    dy = ly / ny
    rdy_fp32 = float(np.float32(1.0 / float(np.float32(dy))))
    ky_discrete = 2.0 * np.sin(0.5 * ky_physical * dy) * rdy_fp32
    return kx, float(ky_discrete), ky_physical, dy


def _as_state(q: np.ndarray, nz: int) -> np.ndarray:
    state = np.asarray(q, dtype=np.complex128)
    if state.shape != (5 * nz + 1,) or not np.isfinite(state).all():
        raise ValueError(f"expected finite modal state of length {5*nz+1}")
    return state


def _source_y_rhs(column: dict, q: np.ndarray, ny: int, ky_discrete: float) -> np.ndarray:
    """Lift the authoritative source RHS at one fixed discrete Y symbol."""
    nz = int(column["nz"])
    state = _as_state(q, nz)
    kx = float(column["kappa"])
    if not np.isfinite(kx) or kx <= 0.0:
        raise ValueError("the source-coordinate Y lift requires Kx > 0")
    u, v = state[:nz], state[nz:2*nz]
    w, phi = state[2*nz:3*nz], state[3*nz:4*nz]
    theta, mu = state[4*nz:5*nz], state[-1:]
    # This makes ikx*u_eff == ikx*u + Ky*v exactly. The 1-D source rows use
    # u only through that horizontal divergence (mu and Omega/continuity).
    u_effective = u + (ky_discrete / (1j * kx)) * v
    source_1d = np.concatenate((u_effective, w, phi, theta, mu))
    old_rhs = source_rhs_1d(column, source_1d)
    udot = old_rhs[:nz]  # -ikx*(phi_mass + alpha_bar*dp), independent of u.
    vdot = (1j * ky_discrete / kx) * udot
    return np.concatenate((udot, vdot, old_rhs[nz:]))


def source_y_rhs(column: dict, q: np.ndarray, ny: int) -> np.ndarray:
    """Fundamental symmetric-Y source RHS in [u,v,w,phi,theta,mu] order."""
    _, ky_discrete, _, _ = _symbols(column, ny)
    return _source_y_rhs(column, q, ny, ky_discrete)


def _source_y_matrix(column: dict, ny: int, ky_discrete: float) -> np.ndarray:
    nz = int(column["nz"])
    size = 5 * nz + 1
    matrix = np.empty((size, size), dtype=np.complex128)
    for j in range(size):
        basis = np.zeros(size, dtype=np.complex128)
        basis[j] = 1.0
        matrix[:, j] = _source_y_rhs(column, basis, ny, ky_discrete)
    return matrix


def source_y_matrix(column: dict, ny: int) -> np.ndarray:
    """Return the 41-component fundamental-Y source operator."""
    _, ky_discrete, _, _ = _symbols(column, ny)
    return _source_y_matrix(column, ny, ky_discrete)


def verify_ky_zero_reduction(column: dict, ny: int) -> dict:
    """Prove exact 33-to-41 source-matrix embedding at ky=0, with V decoupled."""
    nz = int(column["nz"])
    full = _source_y_matrix(column, ny, ky_discrete=0.0)
    old = source_matrix_1d(column)
    old_to_new = np.r_[np.arange(nz), np.arange(2*nz, 5*nz+1)]
    reduced = full[np.ix_(old_to_new, old_to_new)]
    v_rows = np.arange(nz, 2*nz)
    return {
        "old_operator_shape": list(old.shape),
        "lifted_operator_shape": list(full.shape),
        "old_operator_max_abs_delta": float(np.max(np.abs(reduced-old))),
        "v_row_max_abs": float(np.max(np.abs(full[v_rows, :]))),
        "v_column_max_abs": float(np.max(np.abs(full[:, v_rows]))),
        "exact": bool(np.array_equal(reduced, old)
                      and np.count_nonzero(full[v_rows, :]) == 0
                      and np.count_nonzero(full[:, v_rows]) == 0),
    }


def verify_y_staggered_symbols(ny: int, ly: float) -> dict:
    """Check fundamental cosine/sine C-grid symbols and discrete SBP parity."""
    if ny < 2 or ly <= 0.0 or not np.isfinite(ly):
        raise ValueError("ny and ly must be positive")
    dy = float(ly) / int(ny)
    rdy = float(np.float32(1.0 / float(np.float32(dy))))
    ky = np.pi / float(ly)
    ky_d = 2.0 * np.sin(0.5 * ky * dy) * rdy
    centers = (np.arange(ny) + 0.5) * dy
    faces = np.arange(ny + 1) * dy
    p = np.cos(ky * centers)
    v = np.sin(ky * faces)
    div = np.diff(v) * rdy
    grad = np.zeros(ny + 1)
    grad[1:-1] = (p[1:] - p[:-1]) * rdy
    div_error = float(np.max(np.abs(div - ky_d * p)))
    grad_error = float(np.max(np.abs(grad + ky_d * v)))
    lhs = float(np.sum(p * np.diff(v)))
    rhs = float(-np.sum((p[1:] - p[:-1]) * v[1:-1]))
    return {
        "ny": int(ny), "dy": dy, "ky_physical": ky, "ky_discrete": float(ky_d),
        "v_wall_max_abs": float(max(abs(v[0]), abs(v[-1]))),
        "divergence_symbol_max_abs_error": div_error,
        "gradient_symbol_max_abs_error": grad_error,
        "summation_by_parts_residual": lhs-rhs,
        "passed": bool(div_error < 2e-14 and grad_error < 2e-14
                        and abs(lhs-rhs) < 2e-14 and abs(v[0]) < 2e-15
                        and abs(v[-1]) < 2e-15),
    }


def _block_metrics(q: np.ndarray, nz: int) -> dict:
    slices = {"u": slice(0,nz), "v": slice(nz,2*nz), "w": slice(2*nz,3*nz),
              "phi": slice(3*nz,4*nz), "theta": slice(4*nz,5*nz),
              "mu": slice(5*nz,5*nz+1)}
    return {name: {"unit": _UNITS[name],
                   "rms": float(np.sqrt(np.mean(np.abs(q[sl])**2))),
                   "max_abs": float(np.max(np.abs(q[sl])))}
            for name, sl in slices.items()}


def _relative_block_residual(residual: np.ndarray, scale_a: np.ndarray,
                             scale_b: np.ndarray, nz: int) -> dict:
    slices = {"u": slice(0,nz), "v": slice(nz,2*nz), "w": slice(2*nz,3*nz),
              "phi": slice(3*nz,4*nz), "theta": slice(4*nz,5*nz),
              "mu": slice(5*nz,5*nz+1)}
    result = {}
    for name, sl in slices.items():
        denom = max(float(np.max(np.abs(scale_a[sl]))),
                    float(np.max(np.abs(scale_b[sl]))), np.finfo(float).tiny)
        result[name] = float(np.max(np.abs(residual[sl])) / denom)
    return result


def select_internal_mode(matrix: np.ndarray, column: dict) -> dict:
    """Select the largest positive-imaginary branch below background Nmax.

    Phase is fixed by the maximum W component. The eigenvector is scaled so
    max|W|=1; no mixed-unit norm or component fractions are used.
    """
    nz = int(column["nz"])
    a = np.asarray(matrix, dtype=np.complex128)
    if a.shape != (5*nz+1, 5*nz+1) or not np.isfinite(a).all():
        raise ValueError("matrix shape or values do not match the packed Y-wave state")
    nmax = float(np.max(np.sqrt(column["g"] / column["theta"] * column["theta_z"])))
    values, vectors = np.linalg.eig(a)
    candidates = []
    for j, value in enumerate(values):
        frequency = float(value.imag)
        if not (1e-7 < frequency <= nmax):
            continue
        if abs(value.real) > 1e-8 * max(1.0, abs(frequency)):
            continue
        vector = vectors[:, j].astype(np.complex128)
        w = vector[2*nz:3*nz]
        if np.max(np.abs(w)) == 0.0 or np.max(np.abs(vector[nz:2*nz])) == 0.0:
            continue
        candidates.append((frequency, j, value, vector))
    if not candidates:
        raise ValueError("no stable oscillatory sub-Nmax eigenmode has nonzero V and W")
    frequency, index, eigenvalue, vector = max(candidates, key=lambda item:item[0])
    w = vector[2*nz:3*nz]
    pivot = 2*nz + int(np.argmax(np.abs(w)))
    vector *= np.exp(-1j * np.angle(vector[pivot]))
    vector /= float(np.max(np.abs(vector[2*nz:3*nz])))
    residual = a @ vector - eigenvalue * vector
    return {"matrix": a, "eigenvalue": complex(eigenvalue),
            "angular_frequency": frequency, "period_s": 2*np.pi/frequency,
            "background_nmax": nmax, "eigenvector_index": int(index),
            "eigenvector": vector,
            "eigenpair_relative_residual_by_block": _relative_block_residual(
                residual, a @ vector, eigenvalue * vector, nz),
            "eigenvector_block_metrics": _block_metrics(vector, nz)}


def standing_quadratures(mode: dict, w_amplitude: float = 0.01) -> tuple[tuple[np.ndarray, np.ndarray], dict]:
    """Return old-style real standing time quadratures with max|W|=w_amplitude.

    For a positive-X eigenvector ``plus``, the negative-X partner is
    ``Sx*conj(plus)`` with U sign-flipped only. V is not X-odd: it belongs to
    the real symmetric-Y sine mode. The resulting controls satisfy
    ``A*QA=-omega*QB`` and ``A*QB=omega*QA`` up to the measured eigenvalue real
    part and numerical residual.
    """
    if not np.isfinite(w_amplitude) or w_amplitude <= 0.0:
        raise ValueError("w_amplitude must be finite and positive")
    plus = np.asarray(mode["eigenvector"], dtype=np.complex128)
    a = np.asarray(mode["matrix"], dtype=np.complex128)
    nz = (plus.size - 1) // 5
    if plus.shape != (5*nz+1,):
        raise ValueError("mode vector has invalid shape")
    eigenvalue = complex(mode["eigenvalue"])
    sign_x = np.ones(plus.size)
    sign_x[:nz] = -1.0
    minus = sign_x * np.conj(plus)
    qa = 0.5 * (plus + minus)
    qb = (plus - minus) / (2j)
    wmax = max(float(np.max(np.abs(qa[2*nz:3*nz]))),
               float(np.max(np.abs(qb[2*nz:3*nz]))))
    if wmax <= 0.0:
        raise ValueError("standing quadratures have zero W amplitude")
    scale = float(w_amplitude) / wmax
    qa *= scale
    qb *= scale
    omega = float(eigenvalue.imag)
    rplus = a @ plus - eigenvalue * plus
    rminus = a @ minus - np.conj(eigenvalue) * minus
    ra = a @ qa + omega * qb
    rb = a @ qb - omega * qa
    metadata = {
        "w_amplitude_max_abs": float(max(np.max(np.abs(qa[2*nz:3*nz])),
                                          np.max(np.abs(qb[2*nz:3*nz])))),
        "quadrature_a_block_metrics": _block_metrics(qa, nz),
        "quadrature_b_block_metrics": _block_metrics(qb, nz),
        "positive_eigenpair_relative_residual_by_block": _relative_block_residual(
            rplus, a @ plus, eigenvalue * plus, nz),
        "negative_x_partner_relative_residual_by_block": _relative_block_residual(
            rminus, a @ minus, np.conj(eigenvalue) * minus, nz),
        "qa_evolution_identity_residual_by_block": _relative_block_residual(
            ra, -omega * qb, omega * qa, nz),
        "qb_evolution_identity_residual_by_block": _relative_block_residual(
            rb, omega * qa, omega * qb, nz),
        "negative_x_partner_uses_u_sign_flip_only": True,
    }
    basis = (qa, qb)
    return basis, metadata


def physical_w_samples(column: dict, q: np.ndarray, ny: int, xyz: np.ndarray,
                       linear_geometry: bool = False) -> np.ndarray:
    """Sample source W at fixed physical XYZ with native-like X/Y/PH interpolation.

    With ``linear_geometry=False``, the perturbed PH wave shifts each column's
    vertical brackets while sampling the linear source trajectory. This is
    source-generated synthetic truth, not a nonlinear native trajectory. With
    ``linear_geometry=True``, brackets stay at base PH, defining the linearized
    fixed-height observation map used only for two-control rank/metric checks.
    """
    nz, nx = int(column["nz"]), int(column["nx"])
    state = _as_state(q, nz)
    lx, ly = float(column["lx"]), float(column["ly"])
    kx_phys = float(column["k_physical"])
    _, _, ky_phys, dy = _symbols(column, ny)
    dx = float(column["dx"])
    g = float(column["g"])
    base_z = np.asarray(column["phi_total_w"], dtype=np.float64) / g
    phi_hat = np.r_[0j, state[3*nz:4*nz]]
    w_hat = np.r_[0j, state[2*nz:3*nz]]
    points = np.asarray(xyz, dtype=np.float64)
    if points.ndim != 2 or points.shape[1] != 3 or not np.isfinite(points).all():
        raise ValueError("xyz must be a finite (nobs,3) array")
    if np.any(points[:, 1] < 0.0) or np.any(points[:, 1] > ly):
        raise ValueError("observation Y coordinate is outside the symmetric domain")
    out = np.empty(points.shape[0])
    for row, (x, y, z) in enumerate(points):
        xloc = (x % lx) / dx - 0.5
        x0 = int(np.floor(xloc))
        fx = xloc - x0
        yloc = y / dy - 0.5
        y0 = int(np.floor(yloc))
        fy = yloc - y0
        value = 0.0
        for jy, wy in ((y0, 1.0-fy), (y0+1, fy)):
            if wy == 0.0:
                continue
            if not 0 <= jy < ny:
                raise ValueError("observation Y interpolation leaves mass centers")
            yfactor = np.cos(ky_phys * (jy + 0.5) * dy)
            for ix, wx in ((x0, 1.0-fx), (x0+1, fx)):
                phase_x = np.exp(1j * kx_phys * ((ix % nx) + 0.5) * dx)
                if linear_geometry:
                    zfaces = base_z
                else:
                    zfaces = base_z + yfactor * np.real(phi_hat * phase_x) / g
                wfaces = yfactor * np.real(w_hat * phase_x)
                lower = int(np.searchsorted(zfaces, z, side="left") - 1)
                lower = min(max(lower, 0), nz-1)
                if not zfaces[lower] <= z <= zfaces[lower+1]:
                    raise ValueError("observation height is outside the source column")
                alpha = (z-zfaces[lower]) / (zfaces[lower+1]-zfaces[lower])
                value += wy * wx * (wfaces[lower] + alpha*(wfaces[lower+1]-wfaces[lower]))
        out[row] = value
    return out


def source_observations(column: dict, initial_state: np.ndarray, ny: int,
                        xyz: np.ndarray, times: tuple[float, ...] = (150.0, 300.0),
                        linear_geometry: bool = False) -> np.ndarray:
    """Sample W from the linear 41-component source trajectory at fixed XYZ.

    Results are source-generated synthetic observations, not a nonlinear
    native trajectory. Set ``linear_geometry=False`` to include PH-induced
    vertical bracket shifts while retaining linear source dynamics.
    """
    state0 = _as_state(initial_state, int(column["nz"]))
    a = source_y_matrix(column, ny)
    return np.stack([physical_w_samples(column, expm(float(time)*a) @ state0,
                                        ny, xyz, linear_geometry=linear_geometry)
                     for time in times])


def source_observation_xyz(column: dict, coarse_nx: int = 8, coarse_ny: int = 6,
                           vertical_mass_indices: tuple[int, ...] = (1, 3, 5)) -> np.ndarray:
    """Create 105 fixed points on selected fine-background mass heights."""
    if coarse_nx < 2 or coarse_ny < 2:
        raise ValueError("coarse observation dimensions must be at least 2")
    z_mass = np.asarray(column["z_mass"], dtype=np.float64)
    if any(k < 0 or k >= z_mass.size for k in vertical_mass_indices):
        raise ValueError("vertical mass index is outside the fine background")
    lx, ly = float(column["lx"]), float(column["ly"])
    points = []
    for j in range(coarse_ny-1):
        y = (j+0.5) * ly / coarse_ny
        for k in vertical_mass_indices:
            for i in range(coarse_nx-1):
                x = (i+0.5) * lx / coarse_nx
                points.append((x, y, float(z_mass[k])))
    expected = (coarse_nx-1)*(coarse_ny-1)*len(vertical_mass_indices)
    result = np.asarray(points, dtype=np.float64)
    if result.shape != (expected, 3):
        raise AssertionError("observation point construction failed")
    return result


def pack_source_mode(column: dict, q: np.ndarray, ny: int) -> np.ndarray:
    """Pack a source mode into native [U,V,W,PH,theta,MU] perturbation order.

    U carries the periodic x-face alias, V is odd and exactly zero at both
    symmetric Y walls, W/PH have fixed lower z faces, and scalar fields use
    mass points. The caller adds the descriptor-backed native background.
    """
    nz, nx = int(column["nz"]), int(column["nx"])
    state = _as_state(q, nz)
    kx_phys = float(column["k_physical"])
    lx, ly = float(column["lx"]), float(column["ly"])
    dx, dy = lx/nx, ly/ny
    ky = np.pi/ly
    x_u = np.arange(nx) * dx
    x_c = (np.arange(nx)+0.5) * dx
    y_c = (np.arange(ny)+0.5) * dy
    y_f = np.arange(ny+1) * dy
    phase_u = np.exp(1j*kx_phys*x_u)
    phase_c = np.exp(1j*kx_phys*x_c)
    cos_y = np.cos(ky*y_c)
    sin_y = np.sin(ky*y_f)
    sin_y[0] = 0.0
    sin_y[-1] = 0.0
    sizes = (ny*nz*(nx+1), (ny+1)*nz*nx, ny*(nz+1)*nx,
             ny*(nz+1)*nx, ny*nz*nx, ny*nx)
    out = np.zeros(sum(sizes), dtype=np.float64)
    offsets = np.r_[0, np.cumsum(sizes)].astype(int)
    u = out[offsets[0]:offsets[1]].reshape(ny,nz,nx+1)
    v = out[offsets[1]:offsets[2]].reshape(ny+1,nz,nx)
    w = out[offsets[2]:offsets[3]].reshape(ny,nz+1,nx)
    phi = out[offsets[3]:offsets[4]].reshape(ny,nz+1,nx)
    theta = out[offsets[4]:offsets[5]].reshape(ny,nz,nx)
    mu = out[offsets[5]:offsets[6]].reshape(ny,nx)
    for k in range(nz):
        u[:,k,:nx] = cos_y[:,None] * np.real(state[k]*phase_u)[None,:]
        u[:,k,nx] = u[:,k,0]
        v[:,k,:] = sin_y[:,None] * np.real(state[nz+k]*phase_c)[None,:]
        w[:,k+1,:] = cos_y[:,None] * np.real(state[2*nz+k]*phase_c)[None,:]
        phi[:,k+1,:] = cos_y[:,None] * np.real(state[3*nz+k]*phase_c)[None,:]
        theta[:,k,:] = cos_y[:,None] * np.real(state[4*nz+k]*phase_c)[None,:]
    mu[:,:] = cos_y[:,None] * np.real(state[-1]*phase_c)[None,:]
    if np.count_nonzero(v[0,:,:]) or np.count_nonzero(v[-1,:,:]):
        raise AssertionError("V normal velocity must be exactly zero at symmetric walls")
    return out


def quadrature_observation_matrix(column: dict,
                                  basis: tuple[np.ndarray, np.ndarray], ny: int,
                                  xyz: np.ndarray,
                                  times: tuple[float, ...] = (150.0, 300.0)) -> np.ndarray:
    """Build the true linearized H_BG*exp(t*A) matrix for the two controls.

    This uses base PH brackets (``linear_geometry=True``); it is not the
    nonlinear PH-perturbed source truth sampler.
    """
    if len(basis) != 2:
        raise ValueError("the quadrature observation map requires exactly two controls")
    a = source_y_matrix(column, ny)
    sampled_columns = []
    for control in basis:
        state0 = _as_state(control, int(column["nz"]))
        values = [physical_w_samples(column, expm(float(time)*a) @ state0,
                                     ny, xyz, linear_geometry=True)
                  for time in times]
        sampled_columns.append(np.concatenate(values))
    return np.column_stack(sampled_columns)
