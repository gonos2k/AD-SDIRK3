"""Source-transcribed modal reference and physical energy metric for dry waves.

The packed state and RHS below follow ``stable_wave_reference.h``.  This module
is deliberately independent of the C++ solver and uses no fitted invariant or
eigenvector-derived norm.  It also keeps the diagnostic legacy PHB/finite-
difference theta geometry separate from the analytic physical profile.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np
import math


RD = 287.0
CP = 1004.5
CV = 717.5
P0 = 100000.0
PTOP = 20000.0
MUB = 80000.0
MU_BAR = 4000.0
M_TOTAL = MUB + MU_BAR
G = float(np.float32(9.81))
THETA_TOP = 317.0


def _alpha(theta: np.ndarray | float, pressure: np.ndarray | float) -> np.ndarray:
    """WRF dry inverse density in potential-temperature coordinates."""
    theta = np.asarray(theta, dtype=float)
    pressure = np.asarray(pressure, dtype=float)
    return RD * theta / P0 * (pressure / P0) ** (-CV / CP)


def _alpha_fp32(theta: np.ndarray, pressure: np.ndarray, power_method: str = "numpy") -> np.ndarray:
    """Legacy alpha diagnostic; the power backend is explicit for portability checks."""
    f = np.float32
    th, p = np.asarray(theta, dtype=np.float32), np.asarray(pressure, dtype=np.float32)
    # Mirrors compute_inverse_density: double-formed scalar constants are
    # wrapped back to the tensor dtype at each tensor operation.
    coeff = f(float(RD) / float(P0))
    exponent = f(-float(CV) / float(CP))
    ratio = np.asarray(p / f(P0), dtype=np.float32)
    if power_method == "numpy":
        powered = np.power(ratio, exponent, dtype=np.float32)
    elif power_method == "scalar_math":
        powered = np.asarray([f(math.pow(float(r), float(exponent))) for r in ratio.flat],
                             dtype=np.float32).reshape(ratio.shape)
    else:
        raise ValueError("phb_alpha_method must be 'numpy' or 'scalar_math'")
    return np.asarray(np.asarray(coeff * th, dtype=np.float32) * powered, dtype=np.float32)


def physical_transform(column: dict, theta_gradient: str = "analytic") -> np.ndarray:
    """Map packed source perturbations to [U,W,p_E,b,zeta_top]."""
    n = int(column["nz"])
    size = 4 * n + 1
    t = np.zeros((size, size), dtype=np.complex128)
    for k in range(n):
        t[k, k] = 1.0
        t[n + k, n + k] = 1.0
    theta_z = column["theta_z_analytic"] if theta_gradient == "analytic" else column["theta_z_legacy_fd"]
    if theta_gradient not in ("analytic", "legacy_fd"):
        raise ValueError("theta_gradient must be 'analytic' or 'legacy_fd'")
    eta_delta = column["eta_delta"]
    rdnw = column["rdnw"]
    for j in range(size):
        q = np.zeros(size, dtype=np.complex128)
        q[j] = 1.0
        mu = q[4 * n]
        phi_face = np.zeros(n + 1, dtype=np.complex128)
        phi_face[1:] = q[2 * n:3 * n]
        zeta = 0.5 * (phi_face[:-1] + phi_face[1:]) / G
        for k in range(n):
            dphi = phi_face[k + 1] - phi_face[k]
            dalpha = -(column["alpha_bar"][k] * mu + rdnw[k] * dphi) / M_TOTAL
            dp = (CP / CV) * column["p_bar"][k] * (
                q[3 * n + k] / column["theta"][k] - dalpha / column["alpha_bar"][k]
            )
            p_e = dp + column["rho"][k] * G * zeta[k]
            b = G * (q[3 * n + k] - theta_z[k] * zeta[k]) / column["theta"][k]
            t[2 * n + k, j] = p_e
            t[3 * n + k, j] = b
        t[4 * n, j] = phi_face[-1] / G
    return t


def physical_energy_matrix(column: dict, theta_gradient: str = "legacy_fd") -> np.ndarray:
    """Build total H from physical bulk and EOS surface weights: E=q*Hq/2."""
    n = int(column["nz"])
    t = physical_transform(column, theta_gradient)
    d = np.zeros(4 * n + 1, dtype=float)
    # The source diagnostic's real-Fourier average and physical 1/2 energy
    # factor give E=.25*area*sum(weight*|qhat|^2), hence H=area/2*weights.
    area_factor = 0.5 * column["area"]
    d[:n] = column["layer_mass"] * column["eta_delta"] / G
    theta_z = column["theta_z_analytic"] if theta_gradient == "analytic" else column["theta_z_legacy_fd"]
    n2 = G * theta_z / column["theta"]
    for k in range(n):
        d[2 * n + k] = column["height"][k] / ((CP / CV) * column["p_bar"][k])
        d[3 * n + k] = column["height"][k] * column["rho"][k] / n2[k]
    for r in range(n):
        dual = (0.5 * (column["layer_mass"][r] + column["layer_mass"][r + 1])
                if r < n - 1 else 0.5 * column["layer_mass"][-1])
        d[n + r] = dual * column["eta_delta"] / G
    d[-1] = column["rho_top"] * G
    d *= area_factor
    return t.conj().T @ (d[:, None] * t)


def energy_budget(column: dict, q: np.ndarray) -> dict:
    """Evaluate instantaneous bulk, surface, and boundary-work rates for q."""
    q = np.asarray(q, dtype=np.complex128)
    if q.shape != (4 * column["nz"] + 1,):
        raise ValueError("q must use packed [U,W,PHI,THETA,MU] source ordering")
    n = column["nz"]
    x = column["T"] @ q
    xdot = column["A_physical"] @ x
    hsurf = np.zeros((4 * n + 1, 4 * n + 1), dtype=np.complex128)
    hsurf[-1, -1] = 0.5 * column["area"] * column["rho_top"] * G
    hbulk = column["H_source_physical"] - hsurf
    bulk_rate = float(np.real(np.vdot(x, hbulk @ xdot)))
    surface_rate = float(np.real(np.vdot(x, hsurf @ xdot)))
    zeta_top = x[-1]
    w_top = x[2 * n - 1]
    p_top = column["rho_top"] * G * zeta_top
    boundary_work = float(0.5 * column["area"] * np.real(np.conj(p_top) * w_top))
    boundary_flux_out = -boundary_work
    surface_candidate = float(0.5 * np.real(np.vdot(q, column["H_surface_last_cell_candidate"] @ q)))
    delta_dot = column["physical_defect"] @ x
    # The archived bulk diagnostic uses source pressure-layer mass Δp/g for
    # kinetic terms.  Its tiny departure from rho*dz is kept separate in
    # `C_metric` rather than silently mixed into this row-power split.
    weights = column["weights_source"]
    component_delta_power = {}
    for name, sl in (("horizontal_momentum", slice(0, n)),
                     ("vertical_momentum_interior", slice(n, 2 * n - 1)),
                     ("vertical_momentum_top", slice(2 * n - 1, 2 * n)),
                     ("pressure_thermodynamic", slice(2 * n, 3 * n)),
                     ("buoyancy_thermodynamic", slice(3 * n, 4 * n)),
                     ("surface_kinematic", slice(4 * n, 4 * n + 1))):
        component_delta_power[name] = float(
            0.5 * column["area"] * np.sum(weights[sl] *
                np.real(np.conj(x[sl]) * delta_dot[sl]))
        )
    return {
        "bulk_rate": bulk_rate,
        "surface_rate": surface_rate,
        "boundary_work": boundary_work,
        "boundary_flux_out": boundary_flux_out,
        "surface_energy_top_eos": float(np.real(np.vdot(x, hsurf @ x)) * 0.5),
        "surface_energy_last_cell_candidate": surface_candidate,
        "bulk_boundary_residual": bulk_rate - boundary_flux_out,
        "total_rate": bulk_rate + surface_rate,
        "matrix_rate": float(0.5 * np.real(np.vdot(x, column["C_source_physical"] @ x))),
        "source_vs_eulerian_row_power": component_delta_power,
    }


def integrate_energy_budget(column: dict, q0: np.ndarray, end_time: float, samples: int = 257) -> dict:
    """Integrate source bulk/surface rates and physical top flux over an interval."""
    from scipy.integrate import simpson
    from scipy.linalg import expm

    q0 = np.asarray(q0, dtype=np.complex128)
    if end_time < 0 or not np.isfinite(end_time) or samples < 3 or samples % 2 == 0:
        raise ValueError("end_time must be finite/nonnegative and samples an odd integer >= 3")
    times = np.linspace(0.0, end_time, samples)
    bulk_rates = np.empty(samples)
    surface_rates = np.empty(samples)
    boundary_fluxes = np.empty(samples)
    metric_powers = np.empty(samples)
    delta_powers = np.empty(samples)
    row_powers = {name: np.empty(samples) for name in (
        "horizontal_momentum", "vertical_momentum_interior", "vertical_momentum_top",
        "pressure_thermodynamic",
        "buoyancy_thermodynamic", "surface_kinematic")}
    for i, t in enumerate(times):
        q = expm(t * column["A"]) @ q0
        rates = energy_budget(column, q)
        bulk_rates[i] = rates["bulk_rate"]
        surface_rates[i] = rates["surface_rate"]
        boundary_fluxes[i] = rates["boundary_flux_out"]
        x = column["T"] @ q
        metric_powers[i] = 0.5 * np.real(np.vdot(x, column["C_metric"] @ x))
        delta_powers[i] = 0.5 * np.real(np.vdot(x, column["C_defect"] @ x))
        for name, value in rates["source_vs_eulerian_row_power"].items():
            row_powers[name][i] = value
    hsurf = column["H_surface"]
    hbulk = column["H_bulk"]
    bulk_start = float(0.5 * np.real(np.vdot(q0, hbulk @ q0)))
    q1 = expm(end_time * column["A"]) @ q0
    bulk_end = float(0.5 * np.real(np.vdot(q1, hbulk @ q1)))
    surface_start = float(0.5 * np.real(np.vdot(q0, hsurf @ q0)))
    surface_end = float(0.5 * np.real(np.vdot(q1, hsurf @ q1)))
    bulk_rate_integral = float(simpson(bulk_rates, x=times))
    surface_rate_integral = float(simpson(surface_rates, x=times))
    boundary_flux_integral = float(simpson(boundary_fluxes, x=times))
    metric_power_integral = float(simpson(metric_powers, x=times))
    delta_power_integral = float(simpson(delta_powers, x=times))
    row_power_integrals = {name: float(simpson(values, x=times))
                           for name, values in row_powers.items()}
    return {
        "bulk_start": bulk_start, "bulk_end": bulk_end,
        "bulk_delta": bulk_end - bulk_start,
        "bulk_rate_integral": bulk_rate_integral,
        "surface_start": surface_start, "surface_end": surface_end,
        "surface_delta": surface_end - surface_start,
        "surface_rate_integral": surface_rate_integral,
        "boundary_flux_out_integral": boundary_flux_integral,
        "interior_and_top_stencil_residual": bulk_rate_integral - boundary_flux_integral,
        "conservative_metric_exchange_integral": metric_power_integral,
        "source_operator_delta_power_integral": delta_power_integral,
        "source_vs_eulerian_row_power_integrals": row_power_integrals,
        "matrix_budget_integral": metric_power_integral + delta_power_integral,
        "total_energy_delta": (bulk_end + surface_end) - (bulk_start + surface_start),
        "rate_integral_closure_error": (bulk_end - bulk_start) - bulk_rate_integral,
        "surface_integral_closure_error": (surface_end - surface_start) - surface_rate_integral,
        "surface_flux_closure_error": surface_rate_integral + boundary_flux_integral,
    }


def _rhs(column: dict, q: np.ndarray) -> np.ndarray:
    """Linearized source RHS, transcribed row by row from stable_wave_reference.h."""
    n = int(column["nz"])
    ik = 1j * column["kappa"]
    u, w = q[:n], q[n:2 * n]
    phi = q[2 * n:3 * n]
    th = q[3 * n:4 * n]
    mu = q[4 * n]
    phi_face = np.zeros(n + 1, dtype=np.complex128)
    phi_face[1:] = phi
    dalpha = np.empty(n, dtype=np.complex128)
    dp = np.empty(n, dtype=np.complex128)
    out = np.zeros_like(q)
    for k in range(n):
        dalpha[k] = -(column["alpha_bar"][k] * mu + column["rdnw"][k] *
                      (phi_face[k + 1] - phi_face[k])) / M_TOTAL
        dp[k] = (CP / CV) * column["p_bar"][k] * (
            th[k] / column["theta"][k] - dalpha[k] / column["alpha_bar"][k]
        )
        phi_mass = 0.5 * (phi_face[k] + phi_face[k + 1])
        out[k] = -ik * (phi_mass + column["alpha_bar"][k] * dp[k])

    out[4 * n] = -ik * np.sum(column["eta_delta"] * column["layer_mass"] * u)
    omega = np.zeros(n + 1, dtype=np.complex128)
    for k in range(n):
        divv = ik * column["layer_mass"][k] * u[k]
        omega[k + 1] = omega[k] + column["eta_delta"] * (out[4 * n] + divv)

    for r in range(n - 1):
        k = r + 1
        mass_w = M_TOTAL
        out[n + r] = G / mass_w * column["rdn"][k] * (dp[k] - dp[k - 1]) - G / mass_w * mu
    out[2 * n - 1] = G / M_TOTAL * (2.0 * column["rdnw"][-1] * (-dp[-1])) - G / M_TOTAL * mu

    theta_face = np.empty(n + 1, dtype=float)
    theta_face[0], theta_face[-1] = column["theta"][0], column["theta"][-1]
    theta_face[1:-1] = 0.5 * (column["theta"][:-1] + column["theta"][1:])
    for k in range(n):
        lower = 0.0 if k == 0 else theta_face[k] - column["theta"][k]
        upper = 0.0 if k == n - 1 else theta_face[k + 1] - column["theta"][k]
        out[3 * n + k] = (omega[k + 1] * upper - omega[k] * lower) / (
            column["eta_delta"] * column["layer_mass"][k]
        )

    phi_eta = column["rdnw"] * np.diff(column["phi_total_w"])
    for r in range(n):
        adv = 0j
        if r < n - 1:
            adv = -omega[r + 1] * 0.5 * (phi_eta[r + 1] + phi_eta[r])
        out[2 * n + r] = G * w[r] + adv / M_TOTAL
    return out


def source_matrix(column: dict) -> np.ndarray:
    """Return the source-transcribed modal matrix in packed-state order."""
    n = int(column["nz"])
    size = 4 * n + 1
    a = np.empty((size, size), dtype=np.complex128)
    for j in range(size):
        q = np.zeros(size, dtype=np.complex128)
        q[j] = 1.0
        a[:, j] = _rhs(column, q)
    return a


def conservative_eulerian_matrix(column: dict, theta_gradient: str = "analytic") -> np.ndarray:
    """Independently assemble an energy-compatible physical-coordinate operator.

    Its pressure and vertical-flux blocks use the C-grid incidence matrix and
    physical cell/dual-cell weights.  Gravity-pressure, buoyancy, and moving
    surface blocks are paired with weighted adjoints and the EOS top density.
    This is a comparator, not a transcription of the source rows.
    """
    n = int(column["nz"])
    size = 4 * n + 1
    # x = [u(N), w(N), p_E(N), b(N), zeta_top].
    e = np.zeros((size, size), dtype=np.complex128)
    iu, iw, ip, ib, iz = 0, n, 2 * n, 3 * n, 4 * n
    h = column["height"]
    rho = column["rho"]
    gamma_p = (CP / CV) * column["p_bar"]
    cell = np.diag(h)
    kinetic_w = np.empty(n)
    for r in range(n):
        kinetic_w[r] = (0.5 * (rho[r] * h[r] + rho[r + 1] * h[r + 1])
                        if r < n - 1 else 0.5 * rho[-1] * h[-1])
    # The independent divergence is a finite-volume incidence over mass cells.
    bvert = np.zeros((n, n), dtype=float)
    for k in range(n):
        bvert[k, k] += 1.0 / h[k]
        if k > 0:
            bvert[k, k - 1] -= 1.0 / h[k]
    # A bottom fixed face is omitted; the last column is the free top face.
    div_u = 1j * column["kappa"] * np.eye(n)
    div_w = bvert
    # Pressure evolution: -gamma p div(v) + rho*g*interpolated W.
    e[ip:ip + n, iu:iu + n] = -np.diag(gamma_p) @ div_u
    e[ip:ip + n, iw:iw + n] = -np.diag(gamma_p) @ div_w
    interp = np.zeros((n, n), dtype=float)
    for k in range(n):
        interp[k, k] = 0.5
        if k > 0:
            interp[k, k - 1] = 0.5
    e[ip:ip + n, iw:iw + n] += np.diag(rho * G) @ interp
    # U pressure gradient and buoyancy force.
    e[iu:iu + n, ip:ip + n] = -1j * column["kappa"] * np.diag(1.0 / rho)
    e[ib:ib + n, iw:iw + n] = -np.diag(column["N2"]) @ interp
    # W pressure/stratification/buoyancy are independent physical weighted
    # adjoints of the finite-volume flux and cell interpolation blocks.
    inv_w = np.diag(1.0 / kinetic_w)
    grad = inv_w @ div_w.T @ cell
    e[iw:iw + n, ip:ip + n] += grad
    strat_weight = np.diag(h * rho * G / gamma_p)
    e[iw:iw + n, ip:ip + n] += -inv_w @ interp.T @ strat_weight
    # Buoyancy work pairs with bdot using physical rho*h and N^2 weights.
    e[iw:iw + n, ib:ib + n] = inv_w @ interp.T @ np.diag(h * rho)
    # Moving top: the physical EOS boundary pressure and kinematic condition.
    e[iz, iw + n - 1] = 1.0
    # The top pressure flux is canceled by the moving-surface quadratic energy.
    e[iw + n - 1, iz] += -column["rho_top"] * G / kinetic_w[-1]
    return e


def column(
    nz: int = 4,
    nx: int = 8,
    lx: float = 40000.0,
    ly: float = 30000.0,
    *,
    mode_k: float | None = None,
    legacy_fp32: bool = True,
    theta_gradient: str = "legacy_fd",
    phb_alpha_method: str = "numpy",
    imported_phb_faces: np.ndarray | None = None,
) -> dict:
    """Construct the generalized equal-sigma dry reference for N>=4 layers.

    ``mode_k`` is the continuous physical Fourier wavenumber.  The source
    centered-difference symbol is ``2 sin(k dx/2)/dx`` and is retained in
    ``kappa``.  The legacy PHB uses FP32 alpha from base theta=300 K; the
    geometric comparison uses the physical EOS background and is reported
    separately.  The default ``theta_gradient='legacy_fd'`` reproduces the
    archived center finite-difference diagnostic.  Pass
    ``theta_gradient='analytic'`` to use dtheta/dz from the physical profile.
    """
    if nz < 4:
        raise ValueError("nz must be at least 4")
    if nx < 4:
        raise ValueError("nx must be at least 4")
    if lx <= 0 or not np.isfinite(lx) or ly <= 0 or not np.isfinite(ly):
        raise ValueError("lx and ly must be finite and positive")
    n = int(nz)
    dx = lx / nx
    k_physical = 2.0 * np.pi / lx if mode_k is None else float(mode_k)
    if not np.isfinite(k_physical) or k_physical <= 0:
        raise ValueError("mode_k must be finite and positive")
    rdx_fp32 = float(np.float32(1.0 / float(np.float32(dx))))
    kappa = 2.0 * np.sin(0.5 * k_physical * dx) * rdx_fp32
    eta_delta = 1.0 / n
    eta_mass = 1.0 - (np.arange(n) + 0.5) / n
    eta_w = 1.0 - np.arange(n + 1) / n
    p_base = PTOP + MUB * eta_mass
    p_bar = PTOP + M_TOTAL * eta_mass
    theta = 317.0 - 8.0 * eta_mass
    alpha_base_analytic = _alpha(np.full(n, 300.0), p_base)
    alpha_phb_fp32 = _alpha_fp32(np.full(n, 300.0), p_base, phb_alpha_method).astype(float)
    # The source linearization reads its analytic double alphaBase.  The base
    # PHB fixture was separately integrated from its legacy FP32 alpha.
    alpha_base = alpha_base_analytic
    alpha_for_phb = alpha_phb_fp32 if legacy_fp32 else alpha_base_analytic
    alpha_bar = _alpha(theta, p_bar)
    layer_mass = np.full(n, M_TOTAL)
    rdnw = np.full(n, -float(n))
    # Legacy hydrostatic PHB, exactly matching integrate_phb_hydrostatic's
    # up-column recurrence with the base theta=300 K FP32 alpha.
    phi_base_w = np.zeros(n + 1)
    phi_base_analytic_w = np.zeros(n + 1)
    if legacy_fp32:
        phi32 = np.float32(0.0)
        for k in range(n):
            mass32 = np.float32(np.float32(1.0) * np.float32(MUB) + np.float32(0.0))
            incr32 = np.float32(np.float32(alpha_phb_fp32[k]) * mass32 / np.float32(rdnw[k]))
            phi32 = np.float32(phi32 - incr32)
            phi_base_w[k + 1] = float(phi32)
    else:
        for k in range(n):
            phi_base_w[k + 1] = phi_base_w[k] - alpha_base_analytic[k] * MUB / rdnw[k]
    for k in range(n):
        phi_base_analytic_w[k + 1] = phi_base_analytic_w[k] - alpha_base_analytic[k] * MUB / rdnw[k]
    phi_base_source = "legacy_fp32_reconstruction" if legacy_fp32 else "analytic_geometry"
    if imported_phb_faces is not None:
        supplied = np.asarray(imported_phb_faces)
        if supplied.shape != (n + 1,) or not np.all(np.isfinite(supplied)):
            raise ValueError(f"imported_phb_faces must be a finite vector of length {n + 1}")
        # WRF stores the imported PHB faces in default REAL.  Cast once here so
        # caller-side JSON/Python float parsing cannot widen those payload bits.
        phi_base_w = np.asarray(supplied, dtype=np.float32).astype(float)
        phi_base_source = "imported_fp32_faces"
    # Source alpha/PH balance determines the perturbation geopotential faces.
    dphi = (-layer_mass * (alpha_bar - alpha_base) - alpha_base * MU_BAR) / rdnw
    phi_pert_w = np.r_[0.0, np.cumsum(dphi)]
    phi_total_w = phi_base_w + phi_pert_w
    height = np.diff(phi_total_w) / G
    z_w = (phi_total_w - phi_total_w[0]) / G
    z_mass = 0.5 * (z_w[:-1] + z_w[1:])
    temperature = theta * (p_bar / P0) ** (RD / CP)
    rho = p_bar / (RD * temperature)
    theta_z_analytic = 8.0 * G / (alpha_bar * M_TOTAL)
    theta_z_legacy_fd = np.empty(n, dtype=float)
    theta_z_legacy_fd[0] = (theta[1] - theta[0]) / (z_mass[1] - z_mass[0])
    theta_z_legacy_fd[-1] = (theta[-1] - theta[-2]) / (z_mass[-1] - z_mass[-2])
    for k in range(1, n - 1):
        theta_z_legacy_fd[k] = (theta[k + 1] - theta[k - 1]) / (z_mass[k + 1] - z_mass[k - 1])
    # Top EOS is evaluated at the physical boundary p_top and theta(eta=0).
    temperature_top = THETA_TOP * (PTOP / P0) ** (RD / CP)
    rho_top = PTOP / (RD * temperature_top)
    theta_z = theta_z_analytic if theta_gradient == "analytic" else theta_z_legacy_fd
    n2_analytic = G * theta_z_analytic / theta
    n2_legacy_fd = G * theta_z_legacy_fd / theta
    result = {
        "nz": n, "nx": int(nx), "lx": float(lx), "ly": float(ly),
        "area": float(lx * ly), "dx": dx,
        "k_physical": k_physical, "kappa": kappa, "rdx_fp32": rdx_fp32,
        "eta_delta": eta_delta, "eta_mass": eta_mass, "eta_w": eta_w,
        "z_mass": z_mass, "z_w": z_w, "theta": theta,
        "gamma": CP / CV, "g": G, "cp": CP, "cv": CV, "rd": RD,
        "p_base": p_base, "p_bar": p_bar, "alpha_base": alpha_base,
        "alpha_base_analytic": alpha_base_analytic,
        "alpha_phb_fp32": alpha_phb_fp32,
        "alpha_geometry_from_phb": -rdnw * np.diff(phi_base_w) / MUB,
        "alpha_bar": alpha_bar, "layer_mass": layer_mass,
        "rdnw": rdnw, "rdn": rdnw.copy(), "height": height,
        "rho": rho, "p_mass": p_bar.copy(), "N2": n2_legacy_fd if theta_gradient == "legacy_fd" else n2_analytic,
        "N2_analytic": n2_analytic, "N2_legacy_fd": n2_legacy_fd, "theta_z": theta_z,
        "theta_z_analytic": theta_z_analytic,
        "theta_z_legacy_fd": theta_z_legacy_fd,
        "phi_base_w": phi_base_w, "phi_base_analytic_w": phi_base_analytic_w,
        "phi_base_source": phi_base_source,
        "phi_pert_w": phi_pert_w,
        "phi_total_w": phi_total_w, "rho_top": rho_top,
        "rho_top_last_cell_candidate": float(rho[-1]),
        "phb_geometry_relative_error": float(np.max(np.abs(
            np.diff(phi_base_w) / G - alpha_base * MUB / (n * G)
        ) / np.maximum(np.abs(height), np.finfo(float).tiny))),
    }
    result["T"] = physical_transform(result, theta_gradient)
    result["H"] = physical_energy_matrix(result, theta_gradient)
    zeta_map = result["T"][-1, :]
    result["H_surface"] = (0.5 * result["area"] * result["rho_top"] * G *
                           np.outer(zeta_map.conj(), zeta_map))
    result["H_surface_last_cell_candidate"] = (
        0.5 * result["area"] * result["rho_top_last_cell_candidate"] * G *
        np.outer(zeta_map.conj(), zeta_map)
    )
    result["H_bulk"] = result["H"] - result["H_surface"]
    result["A"] = source_matrix(result)
    result["C"] = result["A"].conj().T @ result["H"] + result["H"] @ result["A"]
    result["A_conservative_eulerian"] = conservative_eulerian_matrix(result, theta_gradient)
    result["A_conservative_source_coordinates"] = np.linalg.solve(
        result["T"], result["A_conservative_eulerian"] @ result["T"]
    )
    result["source_minus_conservative"] = result["A"] - result["A_conservative_source_coordinates"]
    result["A_physical"] = result["T"] @ result["A"] @ np.linalg.inv(result["T"])
    # Delta is the physical-coordinate source commutator.  Its p_E rows are
    # obtained by differentiating p_E=delta-p+rho*g*zeta through source delta-
    # alpha, theta, PH and MU tendencies, then subtracting the independent
    # Eulerian law p_E,t=-gamma*p*(ik*U+DivW)+rho*g*InterpW.  Its b rows apply
    # b=g*(delta-theta-theta_z*zeta)/theta and subtract b_t=-N2*InterpW.
    # W rows subtract the flux-incidence weighted-adjoint pressure gradient,
    # pressure-stratification and buoyancy force, including the free-top flux.
    # This is a reference-operator commutator, not by itself a production-bug
    # finding; `energy_budget` integrates each named physical row's work.
    result["physical_defect"] = result["A_physical"] - result["A_conservative_eulerian"]
    n = result["nz"]
    rho, h = result["rho"], result["height"]
    gamma_p = (CP / CV) * result["p_bar"]
    dual_rho_h = np.r_[0.5 * (rho[:-1] * h[:-1] + rho[1:] * h[1:]), 0.5 * rho[-1] * h[-1]]
    physical_weights = np.r_[rho * h, dual_rho_h, h / gamma_p,
                             h * rho / result["N2"], result["rho_top"] * G]
    result["weights"] = physical_weights.copy()
    result["H_physical"] = np.diag(0.5 * result["area"] * physical_weights)
    result["C_physical"] = (result["A_physical"].conj().T @ result["H_physical"] +
                            result["H_physical"] @ result["A_physical"])
    result["C_conservative"] = (
        result["A_conservative_eulerian"].conj().T @ result["H_physical"] +
        result["H_physical"] @ result["A_conservative_eulerian"]
    )
    # Explicit integral budget decomposition: source tendency = independent
    # conservative tendency + measured source-row defect.  The metric term is
    # retained because imported FP32 PHB can make rho*h differ slightly from
    # the source's Δp/g kinetic weight.
    result["H_source_physical"] = np.linalg.solve(
        result["T"].conj().T,
        result["H"] @ np.linalg.inv(result["T"]),
    )
    result["weights_source"] = np.real(np.diag(result["H_source_physical"])) * (2.0 / result["area"])
    result["source_vs_eos_metric_relative"] = float(np.max(np.abs(
        (result["weights"][:2*n] - result["weights_source"][:2*n]) /
        np.maximum(np.abs(result["weights_source"][:2*n]), np.finfo(float).tiny)
    )))
    result["C_source_physical"] = (
        result["A_physical"].conj().T @ result["H_source_physical"] +
        result["H_source_physical"] @ result["A_physical"]
    )
    result["C_metric"] = (
        result["A_conservative_eulerian"].conj().T @ result["H_source_physical"] +
        result["H_source_physical"] @ result["A_conservative_eulerian"]
    )
    result["C_defect"] = (
        result["physical_defect"].conj().T @ result["H_source_physical"] +
        result["H_source_physical"] @ result["physical_defect"]
    )
    result["C_closure_residual"] = result["C_source_physical"] - result["C_metric"] - result["C_defect"]
    closure_scale = max(np.linalg.norm(result["C_source_physical"]),
                        np.linalg.norm(result["C_metric"]) + np.linalg.norm(result["C_defect"]),
                        np.finfo(float).tiny)
    conservative_left = result["A_conservative_eulerian"].conj().T @ result["H_physical"]
    conservative_right = result["H_physical"] @ result["A_conservative_eulerian"]
    conservative_denominator = np.abs(conservative_left) + np.abs(conservative_right)
    conservative_elementwise_ratio = np.divide(
        np.abs(result["C_conservative"]), conservative_denominator,
        out=np.zeros_like(conservative_denominator), where=conservative_denominator > 0.0,
    )
    conservative_frobenius_scale = max(np.linalg.norm(conservative_left) +
                                       np.linalg.norm(conservative_right),
                                       np.finfo(float).tiny)
    result["closure_diagnostics"] = {
        "max_abs_residual": float(np.max(np.abs(result["C_closure_residual"]))),
        "relative_frobenius_residual": float(np.linalg.norm(result["C_closure_residual"]) / closure_scale),
        "conservative_max_abs": float(np.max(np.abs(result["C_conservative"]))),
        "conservative_relative_frobenius_cancellation": float(np.linalg.norm(result["C_conservative"]) / conservative_frobenius_scale),
        "conservative_max_elementwise_relative_cancellation": float(np.max(conservative_elementwise_ratio)),
        "source_energy_defect_max_abs": float(np.max(np.abs(result["C_source_physical"]))),
        "metric_exchange_max_abs": float(np.max(np.abs(result["C_metric"]))),
        "source_operator_delta_exchange_max_abs": float(np.max(np.abs(result["C_defect"]))),
    }
    blocks = {
        "U": np.arange(0, n), "W": np.arange(n, 2 * n),
        "p_E": np.arange(2 * n, 3 * n), "b": np.arange(3 * n, 4 * n),
        "surface": np.array([4 * n]),
    }
    result["physical_row_defect"] = {
        name: {
            "max_abs": float(np.max(np.abs(result["physical_defect"][indices, :]))),
            "l2": float(np.linalg.norm(result["physical_defect"][indices, :])),
            "rows": result["physical_defect"][indices, :].copy(),
        }
        for name, indices in blocks.items()
    }
    # The closure is reported by physical row/column exchange.  This preserves
    # which source stencil contributes to each independently weighted integral.
    result["energy_closure_blocks"] = {
        (row, col): result["C_source_physical"][ri[:, None], ci[None, :]].copy()
        for row, ri in {"U": np.arange(0, n), "W": np.arange(n, 2*n),
                        "p_E": np.arange(2*n, 3*n), "b": np.arange(3*n, 4*n),
                        "surface": np.array([4*n])}.items()
        for col, ci in {"U": np.arange(0, n), "W": np.arange(n, 2*n),
                        "p_E": np.arange(2*n, 3*n), "b": np.arange(3*n, 4*n),
                        "surface": np.array([4*n])}.items()
    }
    result["energy_defect_blocks"] = {
        (row, col): result["C_defect"][ri[:, None], ci[None, :]].copy()
        for row, ri in {"U": np.arange(0, n), "W": np.arange(n, 2*n),
                        "p_E": np.arange(2*n, 3*n), "b": np.arange(3*n, 4*n),
                        "surface": np.array([4*n])}.items()
        for col, ci in {"U": np.arange(0, n), "W": np.arange(n, 2*n),
                        "p_E": np.arange(2*n, 3*n), "b": np.arange(3*n, 4*n),
                        "surface": np.array([4*n])}.items()
    }
    return result


if __name__ == "__main__":
    c = column()
    eig = np.linalg.eigvals(c["A"])
    print(f"nz={c['nz']} nx={c['nx']} dx={c['dx']:.9g} k={c['k_physical']:.9g} kappa={c['kappa']:.9g}")
    print(f"max|C|={np.max(np.abs(c['C'])):.9g} max|A-Acons|={np.max(np.abs(c['source_minus_conservative'])):.9g}")
    print(f"rho_top={c['rho_top']:.9g} last_cell_rho={c['rho_top_last_cell_candidate']:.9g}")
    print(f"eigenvalue_real_max={np.max(eig.real):.9g}")
