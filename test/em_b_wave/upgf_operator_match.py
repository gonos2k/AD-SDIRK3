#!/usr/bin/env python3
"""
Offline reference + validation for advance_uv's u-PGF (module_small_step_em.F:828-831),
the biv_operator_match.py analog for Inc 3.

Purpose: validate the STAGGERED-GRADIENT DISCRETIZATION of the horizontal pressure-gradient
force independently of the (small-step acoustic) perturbation state — because at acoustic
substep 1 the perturbations p',ph' are ~0 and a dyn_em norm-comparison is uninformative
(measured 2026-07-06: dyn_em dpxy_L2=0.13 ~ 0). Here we drive the formula with a KNOWN
analytic perturbation and check it reproduces the analytic pressure gradient to O(dx^2).

u-PGF, hydrostatic 3 terms, em_b_wave (msfux/msfuy=1, cqu=1), at u-point i (between mass i-1,i):
  dpxy(i,k) = 0.5*rdx*(c1h(k)*muu(i)+c2h(k)) * [
       (ph(i,k+1)-ph(i-1,k+1)) + (ph(i,k)-ph(i-1,k))          # geopotential gradient
     + (alt(i,k)+alt(i-1,k))*(p(i,k)-p(i-1,k))                # alpha * dp'/dx  (alt = 1/rho)
     + (al(i,k)+al(i-1,k))*(pb(i,k)-pb(i-1,k)) ]              # alpha' * dpb/dx
with muu(i) = 0.5*(mu(i-1)+mu(i)).
"""
import sys
import numpy as np

# --- grid (em_b_wave-like: periodic x, uniform) ---
nx = 41           # mass points
nz = 40           # mass levels (nz+1 full levels for ph)
L  = 4.0e6        # domain length (m)
dx = L / nx
rdx = 1.0 / dx
xm = (np.arange(nx) + 0.5) * dx      # mass-point x
xu = np.arange(nx) * dx              # u-point x (staggered, at mass-cell edges)

# terrain-following coeffs: pure sigma here -> c1h=1, c2h=0 (so c1h*muu+c2h = muu = mu)
c1h = np.ones(nz)
c2h = np.zeros(nz)

# --- analytic perturbation field: p'(x) = P0 sin(2 pi x / L), everything else trivial ---
P0 = 500.0
kx = 2.0 * np.pi / L
mu = np.full(nx, 9.0e4)              # full column mass (const)
A0 = 2.5                            # inverse density alt (const)
p  = np.tile((P0 * np.sin(kx * xm))[None, :], (nz, 1))   # p'(k,i) -> shape (nz, nx)
pb = np.zeros((nz, nx))                                  # base pressure const -> dpb=0
alt = np.full((nz, nx), A0)
al  = np.zeros((nz, nx))                                 # perturbation inverse density 0
ph  = np.zeros((nz + 1, nx))                             # geopotential perturbation 0

# --- staggered u-PGF at interior u-points i=1..nx-1 (mass i and i-1) ---
def dpxy_upgf(p, pb, alt, al, ph, mu, c1h, c2h, rdx):
    lo = slice(0, nx - 1)   # i-1
    hi = slice(1, nx)       # i
    dp  = p[:, hi] - p[:, lo]
    dpb = pb[:, hi] - pb[:, lo]
    alt_s = alt[:, hi] + alt[:, lo]
    al_s  = al[:, hi] + al[:, lo]
    ph_k   = ph[0:nz, :]
    ph_kp1 = ph[1:nz + 1, :]
    D_ph = (ph_kp1[:, hi] - ph_kp1[:, lo]) + (ph_k[:, hi] - ph_k[:, lo])
    muu = 0.5 * (mu[hi] + mu[lo])                        # (nx-1,)
    coef = c1h[:, None] * muu[None, :] + c2h[:, None]    # (nz, nx-1)
    return 0.5 * rdx * coef * (D_ph + alt_s * dp + al_s * dpb)

dpxy = dpxy_upgf(p, pb, alt, al, ph, mu, c1h, c2h, rdx)   # (nz, nx-1)

# --- analytic reference at interior u-points x_u[1..nx-1] ---
# dpxy -> 0.5*rdx*(c1h*muu+c2h)*(alt(i)+alt(i-1))*(p(i)-p(i-1))
#       = mu * A0 * rdx*(p(i)-p(i-1))  ~ mu*A0 * dp'/dx = mu*A0*P0*kx*cos(kx x_u)
xu_int = xu[1:nx]
analytic = mu[1:nx] * A0 * P0 * kx * np.cos(kx * xu_int)  # (nx-1,) — same at every k
dpxy_row = dpxy[0, :]                                     # any k (field is k-independent)

# relative error (skip points where analytic ~0, i.e. cos ~ 0)
mask = np.abs(analytic) > 0.05 * np.abs(analytic).max()
rel = np.abs(dpxy_row[mask] - analytic[mask]) / np.abs(analytic[mask])
worst = rel.max()
# expected O(dx^2) discretization error: (kx*dx)^2/6 for centered-ish staggered diff
expect = (kx * dx) ** 2 / 6.0

print(f"# u-PGF staggered-gradient discretization check (analytic p'=P0 sin(kx))")
print(f"#   nx={nx} dx={dx:.1f}m  kx*dx={kx*dx:.4f}  expected O(dx^2) err ~ {expect:.3e}")
print(f"#   dpxy L2={np.linalg.norm(dpxy):.4f}  max={np.abs(dpxy).max():.4f}")
print(f"#   worst rel-err vs analytic gradient (non-zero pts): {worst:.3e}")
ok = worst < 3.0 * expect + 1e-3
print(f"# u-PGF discretization == analytic dp/dx : {'PASS' if ok else 'FAIL'}")
sys.exit(0 if ok else 1)   # non-zero exit on FAIL so a regression can't pass silently
