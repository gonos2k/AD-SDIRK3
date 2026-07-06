#!/usr/bin/env python3
"""
Offline reference for advance_mu_t's horizontal mass-flux divergence dvdxi
(module_small_step_em.F:1094-1098), the last distinct staggered discretization in the
acoustic sub-step. Complements upgf_operator_match.py.

Staggering here is the DUAL of the u-PGF: the u-PGF differences a mass field onto u-points
(X[i]-X[i-1]); the divergence differences a FLUX at u-points onto mass points
(flux[i+1]-flux[i]). Getting the direction / index base right is the whole point.

advance_mu_t (em_b_wave msf=1), 1-D in x for the check (v=0):
  dvdxi(i,k) = rdx * ( u_flux(i+1) - u_flux(i) )        # mass-point divergence
with u_flux(i) = u(i) + (c1h(k)*muu(i)+c2h(k))*u_1(i)   # acoustic u + coupled base
Pure-acoustic check: u_1=0 -> dvdxi = rdx*(u(i+1)-u(i)) ~ du'/dx.
"""
import sys
import numpy as np

# --- grid: periodic x, uniform (em_b_wave-like) ---
nx = 41                    # mass points 0..nx-1
nxu = nx + 1               # u points 0..nx  (u(i) sits at mass-cell left edge; u(nx)=u(0) periodic)
L  = 4.0e6
dx = L / nx
rdx = 1.0 / dx
xm = (np.arange(nx) + 0.5) * dx     # mass-point x
xu = np.arange(nxu) * dx            # u-point x (nxu points; xu[nx]=L wraps to xu[0]=0)

# --- analytic acoustic u at u-points: u'(x_u) = U0 sin(kx x_u), u_1=0 ---
U0 = 30.0
kx = 2.0 * np.pi / L
u = U0 * np.sin(kx * xu)            # (nxu,)  u at u-points; u[nx]=U0 sin(2pi)=0=u[0] (periodic-consistent)

def dvdxi_div(u, rdx):
    # mass-point i divergence uses u_flux at u-points i (left) and i+1 (right)
    return rdx * (u[1:nxu] - u[0:nx])   # (nx,)  at mass points 0..nx-1

dvdxi = dvdxi_div(u, rdx)              # (nx,)

# --- analytic reference: du'/dx at mass points = U0 kx cos(kx x_m) ---
analytic = U0 * kx * np.cos(kx * xm)
mask = np.abs(analytic) > 0.05 * np.abs(analytic).max()
rel = np.abs(dvdxi[mask] - analytic[mask]) / np.abs(analytic[mask])
worst = rel.max()
expect = (kx * dx) ** 2 / 6.0          # O(dx^2) staggered truncation floor

print(f"# mass-flux divergence discretization check (analytic u'=U0 sin(kx))")
print(f"#   nx={nx} dx={dx:.1f}m  kx*dx={kx*dx:.4f}  expected O(dx^2) err ~ {expect:.3e}")
print(f"#   dvdxi L2={np.linalg.norm(dvdxi):.6f}  max={np.abs(dvdxi).max():.6f}")
print(f"#   worst rel-err vs analytic du/dx (non-zero pts): {worst:.3e}")
ok = worst < 3.0 * expect + 1e-3
print(f"# divergence discretization == analytic du/dx : {'PASS' if ok else 'FAIL'}")
sys.exit(0 if ok else 1)   # non-zero exit on FAIL so a regression can't pass silently
