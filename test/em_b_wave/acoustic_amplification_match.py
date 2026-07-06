#!/usr/bin/env python3
"""
Inc 5 ASSEMBLY-STABILITY proof (von-Neumann). Where upgf/divergence/vpgf_operator_match.py
validate the acoustic solver's discretization PRIMITIVES, this validates the CORE VERTICAL
w-phi coupling STRUCTURE is stable and the implicit A is load-bearing — including under
VARIABLE stratification c2a(z).

SCOPE (do NOT overclaim): 1-D vertical column, pure w-phi mode with t'=0 (buoyancy term
dropped), NO horizontal coupling, NO calc_p_rho feedback. It proves the vertical coupling
STRUCTURE + A's load-bearing role, now across CONSTANT and VARIABLE c2a(z) (incl. a sharp
tropopause jump and a x20 range). It does NOT prove the FULL scheme with the buoyancy term
(c2a*alt*t_2ave) and 3-D horizontal coupling — that requires validating the assembled substep
against a dyn_em [PARITY substep] dump in Inc 5.

Method: build the exact amplification matrix M (state [w; ph], size 2*nzw) of one acoustic
substep of advance_w's scheme, using the EXACT inverse of the calc_coef_w tridiagonal A
(np.linalg.inv — NOT a hand-rolled Thomas, whose own bug can masquerade as a scheme defect;
a hand-rolled column mis-implemented it and falsely "proved" instability, 2026-07-07).
Semi-implicit off-centering (epssm) => |lambda| slightly < 1 (damped); epssm=0 => |lambda| = 1
(neutral/energy-conserving). Without the implicit A the explicit scheme is |lambda| ~ 68.

Scheme (em_b_wave-style, msf=1, cqw=1), module_small_step_em.F:
  rhs(k)  = ph(k) + (dts*0.5*g*(1-eps)/mut)*w_old(k)                         (:1318 -> :1368)
  w(k)   += 0.5*dts*g*rdn(k)*[ c2a rdnw/mh ((1+eps)d(rhs)+(1-eps)d(ph)) ]_k   (:1405-1413)
  w      <- A^{-1} w   with A = I + calc_coef_w tridiagonal                    (:1433-1443)
  ph(k)   = rhs(k) + 0.5*dts*g*(1+eps)*w_new(k)/(c1f*muts+c2f)                (:1462)
"""
import sys
import numpy as np

nz = 40; nzw = nz + 1; deta = 1.0 / nz
rdnw = np.full(nz, 1/deta); rdn = np.full(nzw, 1/deta)
mut = 9.0e4; mh = np.full(nz, mut); mf = np.full(nzw, mut); g = 9.81; kde = nzw - 1
dts = 1.0

def lambda_max(c2a, eps, use_A):
    A = np.eye(nzw)
    if use_A:
        cof = (0.5 * dts * g * (1 + eps)) ** 2
        for k in range(1, kde):
            A[k, k]   += cof*rdn[k]*(rdnw[k]*c2a[k]/(mh[k]*mf[k]) + rdnw[k-1]*c2a[k-1]/(mh[k-1]*mf[k]))
            A[k, k+1] += -cof*rdn[k]*rdnw[k]*c2a[k]/(mh[k]*mf[k+1])
            A[k, k-1] += -cof*rdn[k]*rdnw[k-1]*c2a[k-1]/(mh[k-1]*mf[k])
        A[kde, kde]   += 2*cof*rdnw[kde-1]**2*c2a[kde-1]/(mh[kde-1]*mf[kde])
        A[kde, kde-1] += -2*cof*rdnw[kde-1]**2*c2a[kde-1]/(mh[kde-1]*mf[kde-1])
    Ainv = np.linalg.inv(A)
    def Lw(v):
        out = np.zeros(nzw)
        for k in range(1, kde):
            out[k] = 0.5*dts*g*rdn[k]*(c2a[k]*rdnw[k]/mh[k]*(v[k+1]-v[k]) - c2a[k-1]*rdnw[k-1]/mh[k-1]*(v[k]-v[k-1]))
        out[kde] = -0.5*dts*g*rdnw[kde-1]**2*2*c2a[kde-1]/mh[kde-1]*(v[kde]-v[kde-1])
        return out
    n = nzw; M = np.zeros((2*n, 2*n))
    for j in range(n):
        for which, (w0, p0) in enumerate([(np.eye(n)[j], np.zeros(n)), (np.zeros(n), np.eye(n)[j])]):
            rhs = p0 + (dts*0.5*g*(1-eps)/mut)*w0; rhs[0] = 0
            wr = w0 + (1+eps)*Lw(rhs) + (1-eps)*Lw(p0); wr[0] = 0
            ws = Ainv @ wr; ws[0] = 0
            pn = rhs.copy()
            for k in range(1, kde+1):
                pn[k] = rhs[k] + 0.5*dts*g*(1+eps)*ws[k]/mut
            c = j if which == 0 else n + j; M[:n, c] = ws; M[n:, c] = pn
    return np.abs(np.linalg.eigvals(M)).max()

z = np.linspace(0, 1, nz)
c2a0 = 1.16e6
profiles = {
    "constant":                np.full(nz, c2a0),
    "exp decay x8 (tropo)":    c2a0*np.exp(-2.08*z),
    "x20 range":               c2a0*np.exp(-3.0*z),
    "sharp tropopause jump":   np.where(z < 0.5, c2a0, c2a0*0.15),
}

print("# von-Neumann amplification: SIMPLIFIED model (1-D vertical, pure w-phi, t'=0,")
print("#   no buoyancy/horizontal/calc_p_rho) — proves coupling STRUCTURE only, NOT the full scheme.")
lam_expl = lambda_max(profiles["constant"], 0.1, False)
print(f"#   explicit, no implicit A (constant) : |lambda|_max = {lam_expl:.4f}   (>> 1: A is load-bearing)")
print("#   semi-implicit (epssm=0.1), implicit A, across stratifications:")
ok = lam_expl > 5.0
for name, c2a in profiles.items():
    ls = lambda_max(c2a, 0.1, True); ln = lambda_max(c2a, 0.0, True)
    stab = (ls <= 1.0 + 1e-6) and (abs(ln - 1.0) < 1e-3)
    ok = ok and stab
    print(f"#     {name:24s} c2a[0]/c2a[-1]={c2a[0]/c2a[-1]:6.2f}  |lambda|(eps=.1)={ls:.6f}  eps=0={ln:.6f}  {'ok' if stab else 'BAD'}")
print(f"# core vertical w-phi coupling STRUCTURE stable (constant + variable c2a) + A load-bearing : {'PASS' if ok else 'FAIL'}")
print("#   NOTE: full-scheme stability (buoyancy term, 3-D horizontal coupling) NOT proven here —")
print("#   requires the Inc 5 assembled-substep-vs-dyn_em [PARITY substep] check.")
sys.exit(0 if ok else 1)
