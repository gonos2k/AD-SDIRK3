#!/usr/bin/env python3
"""Independent quadratic m=1 -> m=2 source reference for the dry wave fixture.

The state, background, and linear operator follow ``wave_energy_spatial_reference``.
The second-order coefficient below is transcribed from the dry WRF EOS and the
one-dimensional periodic mass-coordinate equations.  It does not call native AD,
finite differences, or the solver.  The coefficient convention is the coefficient
of ``a**2`` for a real field ``Re(q exp(i k x))``; products therefore contribute
one half of the product of their positive-m Fourier amplitudes.

The source path is WRFParity + centered horizontal order 2, periodic X,
symmetric Y, fixed bottom W/PH, moving top, and phi_adv_z=2. The canonical path
uses the dedicated curvature core. EOS/PGF and transport/curvature are separate
source-transcribed equations. No native AD or finite-difference Hessian is used.
The smooth-sign order-3 upwind correction has no quadratic coefficient for this
uniform-background m=1 fixture.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct

import numpy as np
from scipy.integrate import solve_ivp
from scipy.linalg import expm

from wave_energy_spatial_reference import column, source_matrix
from wave_quadratic_transport import (finite_smooth_upwind_channels,
                                     transport_channels, transport_mean_channels)


LX = 40_000.0
LY = 30_000.0
RE_RADIUS = float(np.float32(1.0 / 6_370_000.0))
CONTROLS = np.array([0.77998406458075109, -0.35997271519301632,
                     0.35000068647687177, 0.23998213728770962], dtype=float)


def _product_m2(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    """Positive m=2 coefficient of two real m=1 fields."""
    return 0.5 * np.asarray(a) * np.asarray(b)


def _product_m0(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    """Real mean coefficient of two fields represented by positive-m phasors."""
    return 0.5 * np.real(np.asarray(a) * np.conj(np.asarray(b)))


def _eos_first_order(c: dict, q: np.ndarray) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Return alpha', p', and their horizontal m=1 amplitudes from source EOS."""
    n = int(c["nz"])
    phi_face = np.r_[0j, np.asarray(q[2*n:3*n], dtype=np.complex128)]
    mu = complex(q[4*n])
    dphi = np.diff(phi_face)
    # calc_p_rho_wrf: -(alpha_bar*mu + rdnw*dPH)/M, with signed WRF rdnw.
    mbar = float(np.sum(c["eta_delta"] * c["layer_mass"]))
    alpha1 = -(c["alpha_bar"] * mu + c["rdnw"] * dphi) / mbar
    gamma = c["cp"] / c["cv"]
    p1 = gamma * c["p_bar"] * (q[3*n:4*n] / c["theta"] - alpha1 / c["alpha_bar"])
    return alpha1, p1, dphi


def _eos_second_order(c: dict, q: np.ndarray, product) -> tuple[np.ndarray, ...]:
    """Shared dry EOS Taylor formula for m2 or real m0 product operators."""
    n = int(c["nz"])
    mbar = float(np.sum(c["eta_delta"]*c["layer_mass"]))
    alpha1,p1,dphi1 = _eos_first_order(c,q)
    mu1 = complex(q[-1])
    A1 = c["alpha_bar"]*mu1+c["rdnw"]*dphi1
    alpha2 = product(A1,np.full(n,mu1,dtype=np.complex128))/mbar**2
    r = q[3*n:4*n]/c["theta"]
    s1 = alpha1/c["alpha_bar"]
    gamma = c["cp"]/c["cv"]
    logp1 = gamma*(r-s1)
    logp2 = gamma*(-0.5*product(r,r)-alpha2/c["alpha_bar"]
                   +0.5*product(s1,s1))
    p2 = c["p_bar"]*(logp2+0.5*product(logp1,logp1))
    return alpha1,p1,alpha2,p2,dphi1,logp1


def quadratic_pressure_forcing(c: dict, q: np.ndarray) -> np.ndarray:
    """Independent EOS + U/W PGF (including native NH term 4) at m=2.

    This named subset follows ``calc_p_rho_wrf``, the canonical horizontal PGF,
    ``pg_buoy_w``, and ``native_nonhydrostatic_pressure_core``. It excludes
    mass/omega, advection, and curvature. Do not interpret it as total ``F''/2``.
    """
    n = int(c["nz"])
    q = np.asarray(q, dtype=np.complex128)
    if q.shape != (4*n+1,):
        raise ValueError(f"expected a source state of length {4*n+1}")
    dx = float(c["dx"])
    k1 = 2.0*np.pi/float(c["lx"])
    k2 = 2.0*k1
    rdx = float(c["rdx_fp32"])
    d1 = 2.0*np.sin(0.5*k1*dx)*rdx
    d2 = 2.0*np.sin(0.5*k2*dx)*rdx
    half_cos = np.cos(0.5*k1*dx)
    mbar = float(np.sum(c["eta_delta"]*c["layer_mass"]))
    mu1 = complex(q[-1])
    alpha1,p1,alpha2,p2,dphi1,logp1 = _eos_second_order(c,q,_product_m2)

    out = np.zeros(4*n+1,dtype=np.complex128)
    # Horizontal primary PGF after dividing the coupled mass factor. The native
    # face-average alpha cross term is the only bilinear EOS×gradient term here.
    out[:n] = -1j*d2*c["alpha_bar"]*p2
    out[:n] += -_product_m2(half_cos*alpha1,1j*d1*p1)

    # Native NH pressure term 4 (uniform maps, periodic X): term4_u is the
    # Fortran-oriented scalar added to dpx, while the tendency is -term4_u.
    # Here fnm=fnp=1/2, cf=(2,-1.5,0.5), c1h=1, and top_lid=false, matching
    # Grid::setVerticalInterpolationCoefficients in the native wave fixture.
    phi_face1 = np.r_[0j,q[2*n:3*n]]
    php_mass1 = 0.5*(phi_face1[:-1]+phi_face1[1:])
    dpn = np.zeros(n+1,dtype=np.complex128)
    dpn[0] = half_cos*(2.0*p1[0]-1.5*p1[1]+0.5*p1[2])
    for face in range(1,n):
        dpn[face] = 0.5*half_cos*(p1[face]+p1[face-1])
    mu_face1 = half_cos*mu1
    vertical1 = c["rdnw"]*(dpn[1:]-dpn[:-1])-mu_face1
    dphp_u1 = 2j*np.sin(0.5*k1*dx)*php_mass1
    out[:n] += -np.array([_product_m2(dphp_u1[k],vertical1[k])*rdx/mbar
                          for k in range(n)])

    # W pressure/buoyancy force with the source's interior difference and free
    # top extrapolation. Expand 1/(Mbar+mu) against the linear numerator.
    num1 = np.empty(n,dtype=np.complex128)
    num2 = np.empty(n,dtype=np.complex128)
    num1[:-1] = c["g"]*(c["rdn"][1:]*np.diff(p1)-mu1)
    num2[:-1] = c["g"]*c["rdn"][1:]*np.diff(p2)
    num1[-1] = c["g"]*(2.0*c["rdnw"][-1]*(-p1[-1])-mu1)
    num2[-1] = c["g"]*(2.0*c["rdnw"][-1]*(-p2[-1]))
    out[n:2*n] = num2/mbar-_product_m2(mu1,num1)/mbar**2
    return out


def quadratic_mean_pressure_forcing(c: dict, q: np.ndarray) -> np.ndarray:
    """Dry EOS/PGF contribution to the real x-mean at second order.

    Mean products use Hermitian m=1 amplitudes. In particular the U mean PGF
    keeps ``-<alpha1_face * D_x(p1)>``; it does not vanish when the modes have
    different phases. The free-top W extrapolation and native NH term 4 use the
    same staggered rows and coefficients as :func:`quadratic_pressure_forcing`.
    """
    n = int(c["nz"])
    q = np.asarray(q,dtype=np.complex128)
    if q.shape != (4*n+1,):
        raise ValueError(f"expected a source state of length {4*n+1}")
    dx = float(c["dx"])
    k1 = 2.0*np.pi/float(c["lx"])
    rdx = float(c["rdx_fp32"])
    d1 = 2.0*np.sin(0.5*k1*dx)*rdx
    half_cos = np.cos(0.5*k1*dx)
    mbar = float(np.sum(c["eta_delta"]*c["layer_mass"]))
    mu1 = complex(q[-1])
    alpha1,p1,alpha2,p2,dphi1,_ = _eos_second_order(c,q,_product_m0)
    out = np.zeros(4*n+1,dtype=np.complex128)

    # The m=0 derivative of p2/phi2 is zero, but alpha1(x)*D_x(p1)(x)
    # can have a nonzero mean for phase-mixed vertical modes.
    out[:n] = -_product_m0(half_cos*alpha1,1j*d1*p1)

    # NH term 4 is a collocated product of the m=1 horizontal PH derivative
    # and the m=1 vertical pressure-coordinate factor.
    phi_face1 = np.r_[0j,q[2*n:3*n]]
    php_mass1 = 0.5*(phi_face1[:-1]+phi_face1[1:])
    dpn = np.zeros(n+1,dtype=np.complex128)
    dpn[0] = half_cos*(2.0*p1[0]-1.5*p1[1]+0.5*p1[2])
    for face in range(1,n):
        dpn[face] = 0.5*half_cos*(p1[face]+p1[face-1])
    mu_face1 = half_cos*mu1
    vertical1 = c["rdnw"]*(dpn[1:]-dpn[:-1])-mu_face1
    dphp_u1 = 2j*np.sin(0.5*k1*dx)*php_mass1
    out[:n] += -np.array([rdx*_product_m0(dphp_u1[k],vertical1[k])/mbar
                          for k in range(n)])

    # W PGF: the quadratic EOS pressure mean enters the vertical difference;
    # the reciprocal full-column mass contributes the Hermitian mean of mu*N1.
    num1 = np.empty(n,dtype=np.complex128)
    num2 = np.empty(n,dtype=np.complex128)
    num1[:-1] = c["g"]*(c["rdn"][1:]*np.diff(p1)-mu1)
    num2[:-1] = c["g"]*c["rdn"][1:]*np.diff(p2)
    num1[-1] = c["g"]*(2.0*c["rdnw"][-1]*(-p1[-1])-mu1)
    num2[-1] = c["g"]*(2.0*c["rdnw"][-1]*(-p2[-1]))
    out[n:2*n] = num2/mbar-_product_m0(mu1,num1)/mbar**2
    return out


def quadratic_curvature_forcing(c: dict, q: np.ndarray) -> np.ndarray:
    """Canonical uniform-map curvature contribution in primitive U/W rows.

    This is a scalar transcription of ``wrf_sdirk3_curvature.h`` after its
    canonical alpha conversion. It deliberately omits the shared primitive
    ``-q*Mdot/M`` conversion term, which belongs to the mass-continuity channel.
    """
    n = int(c["nz"])
    q = np.asarray(q,dtype=np.complex128)
    if q.shape != (4*n+1,):
        raise ValueError(f"expected a source state of length {4*n+1}")
    half_cos = np.cos(np.pi*float(c["dx"])/float(c["lx"]))
    if not c.get("do_curvature",True):
        return np.zeros(4*n+1,dtype=np.complex128)
    u = q[:n]
    wface = np.r_[0j,q[n:2*n]]
    w_at_u = 0.5*half_cos*(wface[:-1]+wface[1:])
    out = np.zeros(4*n+1,dtype=np.complex128)
    reradius = float(c.get("reradius",RE_RADIUS))
    out[:n] = -reradius*_product_m2(u,w_at_u)
    # WRF curvature core only writes interior W faces k=1..nz-1.
    for face in range(1,n):
        uw = half_cos*(0.5*u[face]+0.5*u[face-1])  # fzm=fzp=1/2
        out[n+face-1] = reradius*_product_m2(uw,uw)
    return out


def quadratic_forcing_channels(c: dict, q: np.ndarray) -> dict[str, np.ndarray]:
    """Return source-named m=2 transport channels plus EOS/PGF and their sum."""
    channels = transport_channels(c,q)
    channels["eos_pgf_term4"] = quadratic_pressure_forcing(c,q)
    channels["total"] = channels["source_state"]+channels["eos_pgf_term4"]
    return channels


def total_quadratic_forcing(c: dict, q: np.ndarray) -> np.ndarray:
    """Independent complete fixture-scope coefficient ``Q2(q)`` in source order."""
    return quadratic_forcing_channels(c,q)["total"]


def quadratic_mean_forcing_channels(c: dict, q: np.ndarray) -> dict[str, np.ndarray]:
    """Return named real-m0 source channels, including Hermitian EOS/PGF terms."""
    channels = transport_mean_channels(c,q)
    channels["eos_pgf_term4"] = quadratic_mean_pressure_forcing(c,q)
    channels["total"] = channels["source_state"]+channels["eos_pgf_term4"]
    return channels


def total_quadratic_mean_forcing(c: dict, q: np.ndarray) -> np.ndarray:
    """Independent second-order real-mean source forcing in packed source order."""
    return quadratic_mean_forcing_channels(c,q)["total"]


def source_zero_matrix(c: dict) -> np.ndarray:
    """Source L(m=0); its neutral mean subspace is preserved without inversion."""
    mean_column = dict(c)
    mean_column["kappa"] = 0.0
    return source_matrix(mean_column)


def polarized_DQ2(c: dict, q: np.ndarray, r: np.ndarray) -> np.ndarray:
    """Exact directional derivative of homogeneous quadratic ``Q2`` at q along r.

    Polarization uses ``Q2(q+r)-Q2(q)-Q2(r)``; it is algebra on the
    source-transcribed quadratic map, not production AD or finite differences.
    """
    return total_quadratic_forcing(c,np.asarray(q)+np.asarray(r)) \
        - total_quadratic_forcing(c,q) - total_quadratic_forcing(c,r)


def _mode2_column(c: dict) -> dict:
    c2 = dict(c)
    c2["k_physical"] = 2.0*float(c["k_physical"])
    rdx = float(c["rdx_fp32"])
    c2["kappa"] = 2.0*np.sin(float(c2["k_physical"])*float(c["dx"])/2.0)*rdx
    return c2


def _checked_times(times: np.ndarray) -> np.ndarray:
    times = np.asarray(times,dtype=np.float64)
    if times.ndim != 1 or times.size < 1 or not np.isfinite(times).all():
        raise ValueError("times must be a finite one-dimensional nonempty vector")
    if times[0] < 0 or np.any(np.diff(times) <= 0):
        raise ValueError("times must be nonnegative and strictly increasing")
    return times


def _integrate_forced_matrix(A: np.ndarray, initial: np.ndarray, times: np.ndarray,
                             rhs_source) -> tuple[np.ndarray,np.ndarray,float,float]:
    """DOP853 solve plus a tighter repeat; source callback supplies forcing(t)."""
    if times[-1] == 0:
        states = np.broadcast_to(initial,(times.size,initial.size)).copy()
        return states,states.copy(),0.0,0.0
    def rhs(t: float,z: np.ndarray) -> np.ndarray:
        return A@z+rhs_source(t)
    loose = solve_ivp(rhs,(0.0,float(times[-1])),initial,t_eval=times,method="DOP853",
                      rtol=2e-12,atol=2e-14)
    tight = solve_ivp(rhs,(0.0,float(times[-1])),initial,t_eval=times,method="DOP853",
                      rtol=5e-14,atol=5e-16)
    if not loose.success: raise RuntimeError(loose.message)
    if not tight.success: raise RuntimeError(tight.message)
    gap = float(np.linalg.norm(tight.y[:,-1]-loose.y[:,-1]))
    rel = gap/max(float(np.linalg.norm(tight.y[:,-1])),np.finfo(float).tiny)
    return loose.y.T,tight.y.T,gap,rel


def weak_trajectory(c: dict, q_initial: np.ndarray, times: np.ndarray) -> dict[str,np.ndarray | dict]:
    """Return m=1, second-order m=0 mean, and m=2 trajectories at requested times.

    The generated q0/q2 start at zero. A0 has a neutral mean subspace, so the
    implementation uses DOP853 directly and does not invert L0 or select its
    zero-frequency eigenvectors.
    """
    times = _checked_times(times)
    q_initial = np.asarray(q_initial,dtype=np.complex128)
    size = 4*int(c["nz"])+1
    if q_initial.shape != (size,):
        raise ValueError(f"q_initial must have source-state length {size}")
    A1 = source_matrix(c)
    A0 = source_zero_matrix(c)
    c2 = _mode2_column(c)
    A2 = source_matrix(c2)
    mzero = np.zeros(size,dtype=np.complex128)
    def q_at(t: float) -> np.ndarray:
        return expm(t*A1)@q_initial
    a0_loose,a0_tight,a0_gap,a0_rel = _integrate_forced_matrix(
        A0,mzero,times,lambda t: total_quadratic_mean_forcing(c,q_at(t)))
    a2_loose,a2_tight,a2_gap,a2_rel = _integrate_forced_matrix(
        A2,mzero,times,lambda t: total_quadratic_forcing(c,q_at(t)))
    q1 = np.stack([q_at(float(t)) for t in times])
    return {"times":times,"q1":q1,"q0_mean":a0_tight,"q2":a2_tight,
            "q0_tightening_states":a0_tight-a0_loose,
            "q2_tightening_states":a2_tight-a2_loose,
            "tightening":{"q0_abs":a0_gap,"q0_relative":a0_rel,
                          "q2_abs":a2_gap,"q2_relative":a2_rel},
            "L0":A0,"L2":A2}


def propagate_q2_sensitivity(c: dict, q_initial: np.ndarray, r_initial: np.ndarray,
                             times: np.ndarray) -> dict[str,np.ndarray | float]:
    """Propagate DQ2[q](r) through L2 for the selected initial direction r."""
    times = _checked_times(times)
    q_initial = np.asarray(q_initial,dtype=np.complex128)
    r_initial = np.asarray(r_initial,dtype=np.complex128)
    size = 4*int(c["nz"])+1
    if q_initial.shape != (size,) or r_initial.shape != (size,):
        raise ValueError(f"q_initial and r_initial must have source-state length {size}")
    A1 = source_matrix(c)
    A2 = source_matrix(_mode2_column(c))
    def source(t: float) -> np.ndarray:
        propagator = expm(t*A1)
        return polarized_DQ2(c,propagator@q_initial,propagator@r_initial)
    loose,tight,gap,rel = _integrate_forced_matrix(
        A2,np.zeros(size,dtype=np.complex128),times,source)
    return {"times":times,"dq2":tight,"tightening_states":tight-loose,
            "tightening_abs":gap,"tightening_relative":rel}


def finite_smooth_upwind_forcing_bank(c: dict, q1: np.ndarray, amplitude: float,
                                      velocity_sign: float = 1.0,
                                      delta: float | None = None) -> dict[int,np.ndarray]:
    """Project exact finite Omega-sign U/W vertical corrections to m=0..Nyquist.

    DFT phases use U-face x=i*dx and W-center x=(i+1/2)*dx. The m=0 and
    Nyquist coefficients use factor one; interior positive harmonics use two.
    Each returned vector is in source order `[U,W_active,PH,THETA,MU]`.
    """
    q1 = np.asarray(q1,dtype=np.complex128)
    n,nx = int(c["nz"]),int(c["nx"])
    if q1.shape != (4*n+1,):
        raise ValueError(f"q1 must have source-state length {4*n+1}")
    delta = float(c.get("sign_smooth_delta",np.float32(1.0e-3))) if delta is None else float(delta)
    finite = finite_smooth_upwind_channels(c,q1,amplitude,delta=delta,
                                           velocity_sign=velocity_sign)
    u_grid = np.asarray(finite["u_upwind"],dtype=np.float64)
    w_grid = np.asarray(finite["w_upwind"],dtype=np.float64)
    if u_grid.shape != (n,nx) or w_grid.shape != (n+1,nx):
        raise ValueError("finite transport correction returned an unexpected staggered shape")
    idx = np.arange(nx,dtype=np.float64)
    bank: dict[int,np.ndarray] = {}
    for harmonic in range(nx//2+1):
        factor = 1.0 if harmonic == 0 or (nx%2 == 0 and harmonic == nx//2) else 2.0
        phase_u = np.exp(-2j*np.pi*harmonic*idx/nx)
        phase_w = np.exp(-2j*np.pi*harmonic*(idx+0.5)/nx)
        u_hat = factor*np.mean(u_grid*phase_u[None,:],axis=1)
        w_hat = factor*np.mean(w_grid*phase_w[None,:],axis=1)
        source = np.zeros(4*n+1,dtype=np.complex128)
        source[:n] = u_hat
        source[n:2*n] = w_hat[1:]
        bank[harmonic] = source
    return bank


def propagate_finite_smooth_upwind(c: dict, q_initial: np.ndarray, amplitude: float,
                                   times: np.ndarray, velocity_sign: float = 1.0,
                                   delta: float | None = None) -> dict[str,object]:
    """Propagate finite smooth-upwind corrections along the same linear q1(t).

    Returns integer-harmonic maps through Nyquist. Each map value has shape
    `(ntime, 4*nz+1)` in source order. This is a separate finite-amplitude
    correction; it does not modify Q0/Q2 or reinterpret their Taylor limit.
    """
    times = _checked_times(times)
    q_initial = np.asarray(q_initial,dtype=np.complex128)
    n,nx = int(c["nz"]),int(c["nx"])
    size = 4*n+1
    if q_initial.shape != (size,):
        raise ValueError(f"q_initial must have source-state length {size}")
    delta = float(c.get("sign_smooth_delta",np.float32(1.0e-3))) if delta is None else float(delta)
    A1 = source_matrix(c)
    harmonics = list(range(nx//2+1))
    operators=[]
    for harmonic in harmonics:
        cm = dict(c)
        cm["k_physical"] = 2.0*np.pi*harmonic/float(c["lx"])
        cm["kappa"] = 2.0*np.sin(np.pi*harmonic/nx)*float(c["rdx_fp32"])
        operators.append(source_matrix(cm))
    bank_size = len(harmonics)*size
    operator_bank = np.zeros((bank_size,bank_size),dtype=np.complex128)
    for j,A in enumerate(operators):
        sl=slice(j*size,(j+1)*size)
        operator_bank[sl,sl]=A

    def q1_at(t: float) -> np.ndarray:
        return expm(t*A1)@q_initial

    def forcing_at(t: float) -> np.ndarray:
        modes=finite_smooth_upwind_forcing_bank(
            c,q1_at(t),amplitude,velocity_sign=velocity_sign,delta=delta)
        return np.concatenate([modes[m] for m in harmonics])

    loose,tight,gap,relative=_integrate_forced_matrix(
        operator_bank,np.zeros(bank_size,dtype=np.complex128),times,forcing_at)
    correction={m:tight[:,i*size:(i+1)*size] for i,m in enumerate(harmonics)}
    tightening={m:tight[:,i*size:(i+1)*size]-loose[:,i*size:(i+1)*size]
                for i,m in enumerate(harmonics)}
    source_forcing={m:np.stack([finite_smooth_upwind_forcing_bank(
        c,q1_at(float(t)),amplitude,velocity_sign=velocity_sign,delta=delta)[m]
        for t in times]) for m in harmonics}
    return {"times":times,"source_forcing":source_forcing,
            "state_correction":correction,"tightening_states":tightening,
            "tightening_abs":gap,"tightening_relative":relative,
            "metadata":{"amplitude":float(amplitude),"velocity_sign":float(velocity_sign),
                        "sign_smooth_delta":delta,"harmonics":harmonics,
                        "nyquist_kappa":2.0*float(c["rdx_fp32"]),
                        "correction":"finite smooth-sign vertical U/W advection only"}}


def mode_pair(c: dict, rank: int) -> tuple[complex, np.ndarray, np.ndarray]:
    """Reproduce the old C++ sub-Nmax eigenmode and its two standing quadratures."""
    A = source_matrix(c)
    values, vectors = np.linalg.eig(A)
    nmax = float(np.max(np.sqrt(c["g"]/c["theta"]*c["theta_z"])))
    candidates = [i for i, value in enumerate(values)
                  if value.imag > 1e-7 and value.imag <= nmax]
    candidates.sort(key=lambda i: values[i].imag, reverse=True)
    if rank < 0 or rank >= len(candidates):
        raise ValueError(f"no sub-Nmax branch rank {rank}; found {len(candidates)}")
    i = candidates[rank]
    plus = vectors[:, i].astype(np.complex128)
    n = int(c["nz"])
    pivot = n + int(np.argmax(np.abs(plus[n:2*n])))
    plus *= np.exp(-1j*np.angle(plus[pivot]))
    minus = np.array([(-1.0 if j < n else 1.0)*np.conj(plus[j])
                      for j in range(4*n+1)], dtype=np.complex128)
    q0 = plus + minus
    q1 = 1j*(plus-minus)
    scale = 0.01/max(float(np.max(np.abs(q0[n:2*n]))), np.finfo(float).tiny)
    return complex(values[i]), scale*q0, scale*q1


def remap_eta(c0: dict, q: np.ndarray, c1: dict) -> np.ndarray:
    """Interpolate fixed physical initial quadratures between sigma grids in eta."""
    n0, n1 = int(c0["nz"]), int(c1["nz"])
    out = np.zeros(4*n1+1, dtype=np.complex128)
    eta0, eta1 = np.asarray(c0["eta_mass"]), np.asarray(c1["eta_mass"])
    face0, face1 = np.asarray(c0["eta_w"])[1:], np.asarray(c1["eta_w"])[1:]
    for start0, start1 in ((0, 0), (3*n0, 3*n1)):
        out[start1:start1+n1] = (np.interp(eta1, eta0[::-1], q[start0:start0+n0].real[::-1])
            + 1j*np.interp(eta1, eta0[::-1], q[start0:start0+n0].imag[::-1]))
    for start0, start1 in ((n0, n1), (2*n0, 2*n1)):
        # Include the fixed bottom face at eta=1 with zero amplitude.
        values0 = np.r_[0j, q[start0:start0+n0]]
        eta_faces0 = np.r_[1.0, face0]
        out[start1:start1+n1] = (np.interp(face1, eta_faces0[::-1], values0[::-1].real)
            + 1j*np.interp(face1, eta_faces0[::-1], values0[::-1].imag))
    out[4*n1] = q[4*n0]
    return out


def propagate_forcing(c: dict, initial: np.ndarray, seconds: float,
                      samples: int = 401) -> dict:
    """Propagate L(m=2) with complete source-transcribed fixture-scope Q2."""
    if seconds < 0 or samples < 2:
        raise ValueError("seconds must be nonnegative and samples >= 2")
    A1 = source_matrix(c)
    c2 = dict(c)
    c2["k_physical"] = 4*np.pi/float(c["lx"])
    rdx = float(c["rdx_fp32"])
    c2["kappa"] = 2.0*np.sin(2.0*np.pi*float(c["dx"])/float(c["lx"]))*rdx
    A2 = source_matrix(c2)
    times = np.linspace(0.0, seconds, samples)
    def rhs(t: float, z: np.ndarray) -> np.ndarray:
        q = expm(t*A1) @ initial
        return A2 @ z + total_quadratic_forcing(c,q)
    sol = solve_ivp(rhs, (0.0, seconds), np.zeros_like(initial), t_eval=times,
                    method="DOP853", rtol=2e-12, atol=2e-14)
    if not sol.success:
        raise RuntimeError(sol.message)
    tight = solve_ivp(rhs,(0.0,seconds),np.zeros_like(initial),t_eval=times,
                      method="DOP853",rtol=5e-14,atol=5e-16)
    if not tight.success:
        raise RuntimeError(tight.message)
    tightening_abs = float(np.linalg.norm(tight.y[:,-1]-sol.y[:,-1]))
    tightening_rel = tightening_abs/max(float(np.linalg.norm(tight.y[:,-1])),np.finfo(float).tiny)
    return {"times": tight.t, "states": tight.y.T, "linear_m1_endpoint": expm(seconds*A1)@initial,
            "A_m2": A2, "m2_column": c2,"tightening_abs":tightening_abs,
            "tightening_relative":tightening_rel}


def _native_arrays(path: Path) -> dict[str, np.ndarray]:
    """Read named array rows emitted by test_native_wave_refinement."""
    arrays: dict[str, np.ndarray] = {}
    for line in path.read_text().splitlines():
        fields = line.split(",")
        if len(fields) >= 4 and fields[0] == "A":
            name, count = fields[1], int(fields[2])
            values = np.asarray([float(x) for x in fields[3:]], dtype=np.float64)
            if values.size != count:
                raise ValueError(f"malformed native array {name}: expected {count}, got {values.size}")
            arrays[name] = values
    return arrays


def _native_metadata(path: Path) -> dict[str, str]:
    values: dict[str,str] = {}
    for line in path.read_text().splitlines():
        fields = line.split(",")
        if len(fields) == 3 and fields[0] in {"M","T"}:
            values[fields[1]]=fields[2]
    return values


def _apply_native_base(c: dict, path: Path) -> None:
    """Replace analytic reconstruction with the native descriptor's input bits."""
    arrays = _native_arrays(path)
    required = {"phb", "pbase", "thbase_perturb", "mubase", "base"}
    missing = required - arrays.keys()
    if missing:
        raise ValueError(f"native CSV lacks required arrays: {sorted(missing)}")
    n, nx = int(c["nz"]), int(c["nx"])
    if arrays["pbase"].size % (n*nx) != 0:
        raise ValueError("native CSV pbase size is incompatible with requested nx/nz")
    ny = arrays["pbase"].size // (n*nx)
    if arrays["phb"].size != ny*(n+1)*nx or arrays["pbase"].size != ny*n*nx:
        raise ValueError("native CSV base geometry does not match requested nx/nz/ny")
    if arrays["thbase_perturb"].size != ny*n*nx or arrays["mubase"].size != ny*nx:
        raise ValueError("native CSV theta/mass base arrays have unexpected sizes")
    eta = np.asarray(c["eta_mass"], dtype=np.float64)
    pbase = arrays["pbase"].reshape(ny,n,nx)[0,:,0]
    su, sv, sw = ny*n*(nx+1),(ny+1)*n*nx,ny*(n+1)*nx
    if arrays["base"].size != su+sv+2*sw+ny*n*nx+ny*nx:
        raise ValueError("native CSV base state has an unexpected packed length")
    ph_state = arrays["base"][su+sv+sw:su+sv+2*sw].reshape(ny,n+1,nx)
    th_state = arrays["base"][su+sv+2*sw:su+sv+2*sw+ny*n*nx].reshape(ny,n,nx)
    theta = 300.0+th_state[0,:,0]
    mubase = float(arrays["mubase"][0])
    mu_state = arrays["base"][-ny*nx:].reshape(ny,nx)
    mu_bar = float(mu_state[0,0])
    mtotal = mubase+mu_bar
    pbar = pbase+eta*mu_bar
    p0 = 100000.0
    alpha_base = c["rd"]*300.0/p0*(pbase/p0)**(-c["cv"]/c["cp"])
    alpha_bar = c["rd"]*theta/p0*(pbar/p0)**(-c["cv"]/c["cp"])
    rdnw = np.asarray(c["rdnw"])
    phi_pert = ph_state[0,:,0]
    phi_base = arrays["phb"].reshape(ny,n+1,nx)[0,:,0].astype(np.float32).astype(np.float64)
    phi_total = phi_base+phi_pert
    z_w = (phi_total-phi_total[0])/float(c["g"])
    z_mass = 0.5*(z_w[:-1]+z_w[1:])
    theta_z = np.empty(n,dtype=np.float64)
    theta_z[0] = (theta[1]-theta[0])/(z_mass[1]-z_mass[0])
    theta_z[-1] = (theta[-1]-theta[-2])/(z_mass[-1]-z_mass[-2])
    for k in range(1,n-1):
        theta_z[k] = (theta[k+1]-theta[k-1])/(z_mass[k+1]-z_mass[k-1])
    c.update({"theta":theta,"p_base":pbase,"p_bar":pbar,"alpha_base":alpha_base,
              "alpha_bar":alpha_bar,"layer_mass":np.full(n,mtotal),"rdnw":rdnw,
              "rdn":np.asarray(c["rdn"]),"phi_base_w":phi_base,
              "phi_pert_w":phi_pert,"phi_total_w":phi_total,"z_w":z_w,
              "z_mass":z_mass,"theta_z":theta_z,"theta_z_legacy_fd":theta_z,
              "theta_z_analytic":8.0*float(c["g"])/(alpha_bar*mtotal),
              "MUB":mubase,"MU_BAR":mu_bar,"M_TOTAL":mtotal,"ny":ny})
    metadata = _native_metadata(path)
    if "rdx_fp32_bits" in metadata:
        bits = int(metadata["rdx_fp32_bits"],16)
        c["rdx_fp32"] = float(struct.unpack("<f",struct.pack("<I",bits))[0])
        c["kappa"] = 2.0*np.sin(np.pi*float(c["dx"])/float(c["lx"]))*c["rdx_fp32"]
    if "reradius_grid" in metadata:
        c["reradius"] = float(metadata["reradius_grid"])
    c["sign_smooth_delta"] = float(metadata.get(
        "sign_smooth_delta_config",str(np.float32(1.0e-3))))
    if "do_curvature_config" in metadata:
        c["do_curvature"] = metadata["do_curvature_config"] == "1"


def build_case(nx: int, nz: int, phb_faces: Path | None = None,
               native_csv: Path | None = None) -> dict:
    imported = np.loadtxt(phb_faces, dtype=np.float64) if phb_faces else None
    c = column(nz=nz, nx=nx, lx=LX, ly=LY, legacy_fp32=True,
               theta_gradient="legacy_fd", imported_phb_faces=imported)
    if native_csv:
        _apply_native_base(c,native_csv)
    return c


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--nx", type=int, default=8)
    parser.add_argument("--nz", type=int, default=4)
    parser.add_argument("--fine-nx", type=int, default=16)
    parser.add_argument("--fine-nz", type=int, default=8)
    parser.add_argument("--coarse-phb", type=Path,
                        help="optional exact FP32 PHB faces, one value per line")
    parser.add_argument("--fine-phb", type=Path,
                        help="optional exact FP32 PHB faces, one value per line")
    parser.add_argument("--coarse-native-csv", type=Path,
                        help="native probe CSV containing exact base arrays")
    parser.add_argument("--fine-native-csv", type=Path,
                        help="native probe CSV containing exact base arrays")
    parser.add_argument("--seconds", type=float, default=300.0)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    coarse = build_case(args.nx, args.nz, args.coarse_phb, args.coarse_native_csv)
    modes = [mode_pair(coarse, rank) for rank in (0, 1)]
    initial = sum((CONTROLS[2*i]*modes[i][1] + CONTROLS[2*i+1]*modes[i][2]
                   for i in range(2)), np.zeros(4*args.nz+1, dtype=np.complex128))
    coarse_channels = quadratic_forcing_channels(coarse,initial)
    qforce0 = coarse_channels["total"]
    coarse_prop = propagate_forcing(coarse, initial, args.seconds)

    fine = build_case(args.fine_nx, args.fine_nz, args.fine_phb, args.fine_native_csv)
    fine_initial = sum((CONTROLS[2*i]*remap_eta(coarse, modes[i][1], fine)
                        + CONTROLS[2*i+1]*remap_eta(coarse, modes[i][2], fine)
                        for i in range(2)), np.zeros(4*args.fine_nz+1, dtype=np.complex128))
    fine_channels = quadratic_forcing_channels(fine,fine_initial)
    fine_qforce0 = fine_channels["total"]
    fine_prop = propagate_forcing(fine, fine_initial, args.seconds)

    result = {
        "source": "tools/wave_quadratic_reference.py",
        "coarse": {"nx": args.nx, "nz": args.nz, "eigenvalues": [[z.real,z.imag] for z,_,_ in modes],
                   "controls": CONTROLS.tolist(), "m2_forcing_t0": [[z.real,z.imag] for z in qforce0],
                   "m2_channels_t0": {k:[[z.real,z.imag] for z in v]
                                      for k,v in coarse_channels.items() if k != "source_state"},
                   "m2_forcing_endpoint": [[z.real,z.imag] for z in coarse_prop["states"][-1]]},
        "fine_common_eta": {"nx": args.fine_nx, "nz": args.fine_nz,
                   "m2_forcing_t0": [[z.real,z.imag] for z in fine_qforce0],
                   "m2_channels_t0": {k:[[z.real,z.imag] for z in v]
                                      for k,v in fine_channels.items() if k != "source_state"},
                   "m2_forcing_endpoint": [[z.real,z.imag] for z in fine_prop["states"][-1]]},
        "integrator": {"seconds": args.seconds, "method": "DOP853", "rtol": 2e-12, "atol": 2e-14,
                       "tight_rtol":5e-14,"tight_atol":5e-16,
                       "coarse_tightening_relative":coarse_prop["tightening_relative"],
                       "fine_tightening_relative":fine_prop["tightening_relative"]},
        "scope": "complete quadratic forcing for the stated uniform periodic-x/symmetric-y fixture; no generic full-tile Hessian claim",
    }
    rendered = json.dumps(result, indent=2)
    if args.output:
        args.output.write_text(rendered+"\n")
    else:
        print(rendered)


if __name__ == "__main__":
    main()
