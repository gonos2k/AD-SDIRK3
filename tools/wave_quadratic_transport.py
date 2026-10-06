#!/usr/bin/env python3
"""Source-transcribed m=1 -> m=2 transport and curvature for the wave column.

This is a transport/curvature companion to :mod:`wave_quadratic_reference`.
It evaluates the coefficient of ``a**2`` for fields ``Re(q exp(i k x))``;
every product of two first-mode amplitudes contributes ``a*b/2``.  It is a
small Fourier-symbol oracle for the fixed periodic-X, uniform-map, y-uniform
(``v=0``) fixture, not a general WRF grid implementation.  No native AD or
finite-difference Hessian is used.

Source path: canonical U/W/theta advection is in the ``do_explicit`` block
(``wrf_sdirk3_tile_unified_impl.cpp:16575-16621, 16890-16903, 17323-17333,
17536-17616``); its legacy horizontal fallback is guarded by
``!canonical_horizontal``. PH transport and mass tendency are in the fast-mode
block (``18479-18529, 19208-19316``). WRF mass flux/omega follows
``wrf_sdirk3_ww_cp.h:181-321`` (and Fortran
``module_big_step_utilities_em.F:640-782``).  The order-2 periodic face flux
is transcribed from ``tests/test_periodic_staggered_advection.cpp:108-121,
343-407`` and ``wrf_sdirk3_horizontal_momentum.h:35-105``.  Native-W PH
horizontal advection is ``wrf_sdirk3_phi_horizontal.h:64-129``.  The
canonical curvature formula is ``tests/curvature_scalar_oracle.h:65-190``;
its native caller and final coupled-to-primitive conversion are at
``wrf_sdirk3_tile_unified_impl.cpp:23526-23570, 25009-25029, 25065-25083``.

The function includes horizontal and vertical U/W/theta/PH transport,
column-mass tendency, the PH buoyancy product, canonical curvature, and the
primitive-variable mass product-rule terms. These are physical RHS channels,
not a claim that every channel belongs to ``ExplicitOnly``: U/W/theta
advection is under ``do_explicit``; PH and column mass are in the fast-mode
block; canonical curvature is in an unconditional source block after the
mode-gated terms. The m2 channels are named separately so a caller can apply
native pass ownership. The smoothed order-3 vertical
upwind correction has no quadratic coefficient: its sign factor is O(a),
velocity is O(a), and the correction stencil annihilates the linear theta
background.  This module does not include EOS/PGF terms or legacy-only
transport paths.
"""
from __future__ import annotations

import numpy as np


def _m2(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    """Positive second-harmonic amplitude of a product of two real m=1 fields."""
    return 0.5 * np.asarray(a, dtype=np.complex128) * np.asarray(b, dtype=np.complex128)


def _omega_modes(c: dict, u: np.ndarray, mu: complex, k1: float,
                 rdx: float, half_cos: float, mass: float) -> tuple[np.ndarray, np.ndarray,
                                                                       complex, complex]:
    """Return m=1/m=2 diagnosed omega and corresponding Mdot/map-y modes."""
    n = int(c["nz"])
    # The descriptor stores rdnw=1/dnw (negative). calc_ww_cp receives
    # dnw=-1/|rdnw|, the signed eta layer thickness.
    dnw = 1.0 / np.asarray(c["rdnw"], dtype=np.float64)
    d1 = 2j * np.sin(0.5 * k1 * float(c["dx"])) * rdx
    d2 = 2j * np.sin(k1 * float(c["dx"])) * rdx
    cu1 = mass * u
    cu2 = _m2(half_cos * mu, u)
    div1 = dnw * d1 * cu1
    div2 = dnw * d2 * cu2
    dmdt1, dmdt2 = complex(np.sum(div1)), complex(np.sum(div2))
    om1 = np.zeros(n + 1, dtype=np.complex128)
    om2 = np.zeros(n + 1, dtype=np.complex128)
    for k in range(n - 1):
        # calc_ww_cp: ww(k+1)=ww(k)-dnw*c1h*dmdt-divv(k), c1h=1.
        om1[k + 1] = om1[k] - dnw[k] * dmdt1 - div1[k]
        om2[k + 1] = om2[k] - dnw[k] * dmdt2 - div2[k]
    # Kinematic boundary values in diagnose_wrf_mass_flux are exactly zero.
    om1[-1] = om2[-1] = 0.0
    return om1, om2, dmdt1, dmdt2


def _vertical_interp_u(q: np.ndarray, top: tuple[float, float]) -> np.ndarray:
    """Interpolate mass-level q to W levels with the native fixture coefficients."""
    n = q.size
    out = np.zeros(n + 1, dtype=np.complex128)
    out[1:n] = 0.5 * (q[1:] + q[:-1])
    out[n] = top[0] * q[-1] + top[1] * q[-2]
    return out


def _u_vertical_advection(u: np.ndarray, omega_u: np.ndarray,
                          rdnw_abs: np.ndarray, rdn_abs: np.ndarray) -> np.ndarray:
    """m=2 coefficient of wrf_vert_adv3, omitting its cubic smooth-upwind part."""
    n = u.size
    flux = np.zeros(n + 1, dtype=np.complex128)
    for face in range(1, n):
        if n >= 4 and 1 < face < n - 1:
            # flux4(q[k-2],q[k-1],q[k],q[k+1]); sign correction is O(a^3).
            qf = (7.0 * (u[face] + u[face - 1]) -
                  (u[face + 1] + u[face - 2])) / 12.0
        else:
            dnw = 1.0 / rdnw_abs[face - 1]
            dnv = 1.0 / rdn_abs[face]
            fzm = 0.5 * dnw / dnv
            fzp = 0.5 * (1.0 / rdnw_abs[face]) / dnv
            qf = fzm * u[face] + fzp * u[face - 1]
        flux[face] = _m2(omega_u[face], np.asarray([qf]))[0]
    return rdnw_abs[:n] * (flux[1:] - flux[:-1])


def _w_vertical_advection(w: np.ndarray, omega: np.ndarray,
                          rdn_abs: np.ndarray) -> np.ndarray:
    """m=2 coefficient of wrf_vert_adv3_w, including bottom/top flux rules."""
    n = w.size - 1
    romm = 0.5 * (omega[:-1] + omega[1:])
    vflux = np.zeros(n, dtype=np.complex128)
    vflux[0] = _m2(romm[0], np.asarray([0.5 * (w[0] + w[1])]))[0]
    for m in range(1, n - 1):
        if n >= 4:
            # In wrf_vert_adv3_w, vflux[m] uses q_im2=w[m-1], q_im1=w[m],
            # q_i=w[m+1], q_ip1=w[m+2].
            qf = (7.0 * (w[m + 1] + w[m]) - (w[m + 2] + w[m - 1])) / 12.0
        else:
            qf = 0.5 * (w[m] + w[m + 1])
        vflux[m] = _m2(romm[m], np.asarray([qf]))[0]
    if n > 1:
        vflux[-1] = _m2(romm[-1], np.asarray([0.5 * (w[-2] + w[-1])]))[0]
    out = np.zeros(n + 1, dtype=np.complex128)
    if n > 1:
        out[1:n] = rdn_abs[1:n] * (vflux[1:] - vflux[:-1])
        out[n] = -2.0 * rdn_abs[n - 1] * vflux[-1]
    return out


def transport_channels(c: dict, q: np.ndarray) -> dict[str, np.ndarray]:
    """Return independently named physical m=2 forcing components.

    Inputs are limited to the source fixture: N=4/8, periodic X, symmetric
    uniform Y, v=0, unit maps, c1h=c1f=1/c2h=c2f=0, order-2 horizontal
    momentum/scalar transport, native-W PH horizontal order 2, and active
    canonical curvature.  Arrays use the source state order ``[u,w,phi,theta,mu]``.
    """
    n = int(c["nz"])
    q = np.asarray(q, dtype=np.complex128)
    if q.shape != (4 * n + 1,):
        raise ValueError(f"expected a source state of length {4*n+1}")
    if n < 4 or int(c.get("nx", 4)) < 4:
        raise ValueError("the periodic wave transport oracle requires N>=4 and nx>=4")
    dx = float(c["dx"])
    lx = float(c["lx"])
    k1 = float(c.get("k_physical", 2.0 * np.pi / lx))
    k2 = 2.0 * k1
    # Prefer exact values imported from a native descriptor. Otherwise reproduce
    # the source's default-REAL metric initialization from the fixture geometry.
    rdx = float(c.get("rdx_f32", c.get("rdx", c.get("rdx_fp32", np.float32(1.0 / dx)))))
    # rdy is retained as an actual source metric input; all Y derivatives
    # multiply either v=0 or a uniform map gradient, so the fixture's Y terms vanish.
    rdy = float(c.get("rdy_f32", c.get("rdy", np.float32(1.0 / float(c["ly"])))) )
    g = float(c.get("g_f32", np.float32(c["g"])))
    reradius = float(c.get("reradius_f32", c.get("reradius", np.float32(1.0 / 6_370_000.0))))
    mass = float(np.sum(np.asarray(c["eta_delta"]) * np.asarray(c["layer_mass"])))
    half_cos = float(np.cos(0.5 * k1 * dx))
    d2_half = 2j * np.sin(0.5 * k2 * dx) * rdx
    u = q[:n]
    # State stores W faces k=1..n; only the bottom face k=0 is fixed.
    w = np.r_[0j, q[n:2*n]]
    phi = np.r_[0j, q[2*n:3*n]]
    theta = q[3*n:4*n]
    # computeUnifiedRHS stores theta' = theta - 300 K in the source state;
    # explicit scalar advection sees the nonzero base theta' as well.
    theta0 = np.asarray(c["theta"], dtype=np.float64) - 300.0
    mu = complex(q[-1])
    rdnw_abs = np.abs(np.asarray(c["rdnw"], dtype=np.float64))
    rdn_abs = np.abs(np.asarray(c["rdn"], dtype=np.float64))
    if rdnw_abs.size < n or rdn_abs.size < n:
        raise ValueError("rdnw and rdn must cover every mass level")
    omega, omega2, dmdt, dmdt2 = _omega_modes(c, u, mu, k1, rdx,
                                              half_cos, mass)
    # Column mass rate is msfty*dmdt; this fixture has msfty=1.
    mdot1, mdot2 = dmdt, dmdt2

    channels: dict[str, np.ndarray] = {}
    # Order-2 canonical U horizontal momentum flux: -div( avg(cu)*avg(u) ).
    u_flux2 = _m2(mass * half_cos * u, half_cos * u)
    channels["u_horizontal"] = -d2_half * u_flux2 / mass
    # WRF's canonical U vertical order-3 flux (centered quadratic term only).
    omega_u = half_cos * omega
    channels["u_vertical"] = _u_vertical_advection(u, omega_u, rdnw_abs, rdn_abs) / mass

    # Canonical horizontal W transport uses native fnm/fnp interpolation of cu.
    fnm = np.asarray(c.get("fnm", np.full(n + 1, 0.5)), dtype=float)
    fnp = np.asarray(c.get("fnp", np.full(n + 1, 0.5)), dtype=float)
    if fnm.size < n + 1 or fnp.size < n + 1:
        raise ValueError("fnm/fnp must include the native top W level")
    cu1 = mass * u
    transport_w = np.zeros(n + 1, dtype=np.complex128)
    transport_w[1:n] = fnm[1:n] * cu1[1:] + fnp[1:n] * cu1[:-1]
    transport_w[n] = (2.0 - fnm[n-1]) * cu1[-1] - fnp[n-1] * cu1[-2]
    w_flux2 = _m2(transport_w, half_cos * w)
    channels["w_horizontal"] = -d2_half * w_flux2 / mass
    channels["w_vertical"] = _w_vertical_advection(w, omega, rdn_abs) / mass

    # Scalar order-2 flux: -rdx*div[cu * (theta_west+theta_east)/2].
    theta_flux2 = (_m2(mass * u, half_cos * theta) +
                   0.5 * half_cos * mu * u * theta0)
    theta_n2_h = -d2_half * theta_flux2
    channels["theta_horizontal"] = theta_n2_h / mass
    theta0_w = np.r_[theta0[0], 0.5 * (theta0[1:] + theta0[:-1]), theta0[-1]]
    theta1_w = np.r_[theta[0], 0.5 * (theta[1:] + theta[:-1]), theta[-1]]
    theta_flux_z2 = omega2 * theta0_w + _m2(omega, theta1_w)
    theta_n2_v = rdnw_abs[:n] * (theta_flux_z2[1:] - theta_flux_z2[:-1])
    channels["theta_vertical"] = theta_n2_v / mass
    # The same mass-conserving scalar flux gives N1 - theta0*Mdot1; include
    # that linear transport in the reciprocal-mass product rule as well.
    d1 = 2j * np.sin(0.5 * k1 * dx) * rdx
    theta_n1_h = -d1 * mass * u * theta0
    theta_n1_v = rdnw_abs[:n] * ( (omega * theta0_w)[1:] - (omega * theta0_w)[:-1] )
    theta_n1 = theta_n1_h + theta_n1_v
    theta_a1 = theta_n1 - theta0 * mdot1
    channels["theta_mass_conversion"] = (
        -(theta0 * mdot2 + _m2(theta, np.full(n, mdot1))) / mass
        - _m2(np.full(n, mu), theta_a1) / mass**2
    )

    # Native-W PH horizontal order-2 flux. The top velocity uses phi_cfn/fn1;
    # equal eta spacing gives 1/2,1/2. Bottom horizontal velocity is zero.
    xvel = np.zeros(n + 1, dtype=np.complex128)
    xvel[1:n] = u[1:] + u[:-1]
    # Tile caller computes cfn1=-r_below/(r_top+r_below), cfn=1-cfn1;
    # equal spacing therefore gives the native 1.5,-0.5 top extrapolation.
    xvel[n] = 1.5 * u[-1] - 0.5 * u[-2]
    phi_grad = 2j * np.sin(0.5 * k1 * dx) * phi
    # helper: fx=.25*cu*xvel*grad; WRF adds neighboring face fluxes.
    phi_flux2 = 0.25 * _m2(mass * xvel, phi_grad)
    phi_flux2[-1] *= 2.0  # native-W order-2 operator doubles the extrapolated lid flux
    channels["phi_horizontal"] = -2.0 * np.cos(0.5 * k2 * dx) * rdx * phi_flux2 / mass

    # PH vertical advection uses total background+perturbation Phi and omega.
    # WRF eta derivative is -|rdnw|*Delta(Phi); fnm/fnp are 1/2 on this fixture.
    phi_total = np.asarray(c.get("phi_total_w", c.get("phi_base_w", np.zeros(n + 1))), dtype=float)
    if phi_total.size != n + 1:
        raise ValueError("phi_total_w/phi_base_w must have nz+1 faces")
    dph_bg = -rdnw_abs * np.diff(phi_total)
    dph_q = -rdnw_abs * np.diff(phi)
    wdwn_up1 = np.zeros(n + 1, dtype=np.complex128)
    wdwn_lo1 = np.zeros(n + 1, dtype=np.complex128)
    wdwn_up1[1:n] = dph_q[1:]
    wdwn_lo1[1:n] = dph_q[:-1]
    wdwn_up0 = np.zeros(n + 1, dtype=float)
    wdwn_lo0 = np.zeros(n + 1, dtype=float)
    wdwn_up0[1:n] = dph_bg[1:]
    wdwn_lo0[1:n] = dph_bg[:-1]
    # omega2*background-gradient + omega1*perturbation-gradient.
    phi_grad_bg = -0.5 * omega * (wdwn_up0 + wdwn_lo0)
    phi_z2 = (-0.5 * omega2 * (wdwn_up0 + wdwn_lo0) -
              0.25 * omega * (wdwn_up1 + wdwn_lo1))
    channels["phi_vertical"] = phi_z2 / mass
    # The numerator buoyancy is (M+mu)*g*w, while final PH conversion divides
    # by the same M+mu. Keep both quadratic pieces named; their sum is zero.
    channels["phi_buoyancy"] = g * _m2(np.full(n + 1, mu), w) / mass
    phi_n1 = g * mass * w + phi_grad_bg
    channels["phi_mass_conversion"] = -_m2(np.full(n + 1, mu), phi_n1) / mass**2

    # Canonical curvature, maps=1, y-uniform v=0, map_proj!=6/polar.
    rw1 = mass * w
    rw_u = half_cos * 0.5 * (rw1[:-1] + rw1[1:])
    curv_u2 = -reradius * _m2(u, rw_u) / mass
    ru_mass = half_cos * (mass * u)
    u_mass = half_cos * u
    ru_w = np.zeros(n + 1, dtype=np.complex128)
    u_w = np.zeros(n + 1, dtype=np.complex128)
    ru_w[1:n] = 0.5 * (ru_mass[1:] + ru_mass[:-1])
    u_w[1:n] = 0.5 * (u_mass[1:] + u_mass[:-1])
    curv_w2 = np.zeros(n + 1, dtype=np.complex128)
    curv_w2[1:n] = reradius * _m2(ru_w[1:n], u_w[1:n]) / mass
    channels["curvature_u"] = curv_u2
    channels["curvature_w"] = curv_w2
    channels["u_mass_conversion"] = -_m2(u, np.full(n, half_cos * mdot1)) / mass
    channels["w_mass_conversion"] = -_m2(w, np.full(n + 1, mdot1)) / mass
    channels["mass"] = np.asarray([mdot2], dtype=np.complex128)

    # Keep the m=2 stored face layout (bottom W/PH excluded from the source vector).
    out = {
        "u": channels["u_horizontal"] + channels["u_vertical"] +
             channels["curvature_u"] + channels["u_mass_conversion"],
        "w": channels["w_horizontal"] + channels["w_vertical"] +
             channels["curvature_w"] + channels["w_mass_conversion"],
        "phi": (channels["phi_horizontal"] + channels["phi_vertical"] +
                channels["phi_buoyancy"] + channels["phi_mass_conversion"])[1:],
        "theta": (channels["theta_horizontal"] + channels["theta_vertical"] +
                  channels["theta_mass_conversion"]),
        "mu": channels["mass"],
    }
    channels["source_state"] = np.concatenate((out["u"], out["w"][1:],
                                                out["phi"], out["theta"], out["mu"]))
    return channels


def transport_forcing(c: dict, q: np.ndarray) -> np.ndarray:
    """Return transport/curvature ``F''/2`` in source state order.

    The result is a physical RHS sum. Use :func:`transport_channels` to retain
    source-term ownership when assembling ExplicitOnly/ImplicitOnly totals.
    """
    return transport_channels(c, q)["source_state"]
