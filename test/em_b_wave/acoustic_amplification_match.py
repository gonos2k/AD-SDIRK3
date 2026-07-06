#!/usr/bin/env python3
"""
Inc 5 ASSEMBLY-STABILITY proof (von-Neumann). Where upgf/divergence/vpgf_operator_match.py
validate the acoustic solver's discretization PRIMITIVES, this validates the CORE VERTICAL
w-phi coupling STRUCTURE is stable and the implicit A is load-bearing.

SCOPE (do NOT overclaim): this is a SIMPLIFIED model — 1-D vertical column, CONSTANT metrics/c2a,
pure w-phi mode with t'=0 (buoyancy term dropped), NO horizontal coupling, NO calc_p_rho feedback.
It proves the vertical coupling STRUCTURE + A's load-bearing role. It does NOT prove the FULL scheme
with variable stratification c2a(z), the buoyancy term, and 3-D horizontal coupling — that requires
validating the assembled substep against a dyn_em [PARITY substep] dump in Inc 5.

Method: build the exact amplification matrix M (state [w; ph], size 2*nzw) of one acoustic
substep of advance_w's scheme, using the EXACT inverse of the calc_coef_w tridiagonal A
(np.linalg.inv — NOT a hand-rolled Thomas, whose own bug can masquerade as a scheme defect;
a hand-rolled column mis-implemented it and falsely "proved" instability, 2026-07-07).
Measure |lambda|_max. Semi-implicit off-centering (epssm) => |lambda| slightly < 1 (damped);
epssm=0 => |lambda| = 1 (neutral / energy-conserving). Without the implicit A the explicit
scheme is |lambda| ~ 64 (wildly unstable) — i.e. A is LOAD-BEARING.

Scheme (em_b_wave-style, msf=1, cqw=1, pure w-phi mode), module_small_step_em.F:
  rhs(k)  = ph(k) + (dts*0.5*g*(1-eps)/mut)*w_old(k)                         (:1318 -> :1368)
  w(k)   += 0.5*dts*g*rdn(k)*[ c2a rdnw/mh ((1+eps)d(rhs)+(1-eps)d(ph)) ]_k   (:1405-1413)
  w      <- A^{-1} w   with A = I + calc_coef_w tridiagonal                    (:1433-1443)
  ph(k)   = rhs(k) + 0.5*dts*g*(1+eps)*w_new(k)/(c1f*muts+c2f)                (:1462)
"""
import sys
import numpy as np

nz = 40; nzw = nz + 1; deta = 1.0 / nz
rdnw = np.full(nz, 1/deta); rdn = np.full(nzw, 1/deta)
mut = 9.0e4; mh = np.full(nz, mut); mf = np.full(nzw, mut); muts = mut
c2a = np.full(nz, 1.16e6); g = 9.81; kde = nzw - 1
dts = 1.0    # amplification |lambda| is dts-normalized; small dts probes the resolved limit

def build_A(eps, use_A):
    A = np.eye(nzw)
    if not use_A:
        return A
    cof = (0.5 * dts * g * (1 + eps)) ** 2
    for k in range(1, kde):
        A[k, k]   += cof * rdn[k] * (rdnw[k]*c2a[k]/(mh[k]*mf[k]) + rdnw[k-1]*c2a[k-1]/(mh[k-1]*mf[k]))
        A[k, k+1] += -cof * rdn[k] * rdnw[k]   * c2a[k]   / (mh[k]   * mf[k+1])
        A[k, k-1] += -cof * rdn[k] * rdnw[k-1] * c2a[k-1] / (mh[k-1] * mf[k])
    A[kde, kde]   += 2 * cof * rdnw[kde-1]**2 * c2a[kde-1] / (mh[kde-1] * mf[kde])
    A[kde, kde-1] += -2 * cof * rdnw[kde-1]**2 * c2a[kde-1] / (mh[kde-1] * mf[kde-1])
    return A

def Lw(vec):   # w-forcing operator applied to a phi field
    out = np.zeros(nzw)
    for k in range(1, kde):
        up = c2a[k]*rdnw[k]/mh[k]*(vec[k+1]-vec[k]); lo = c2a[k-1]*rdnw[k-1]/mh[k-1]*(vec[k]-vec[k-1])
        out[k] = 0.5*dts*g*rdn[k]*(up - lo)
    out[kde] = -0.5*dts*g*rdnw[kde-1]**2*2*c2a[kde-1]/mh[kde-1]*(vec[kde]-vec[kde-1])
    return out

def lambda_max(eps, use_A):
    Ainv = np.linalg.inv(build_A(eps, use_A)); n = nzw; M = np.zeros((2*n, 2*n))
    for j in range(n):
        for which, (w0, p0) in enumerate([(np.eye(n)[j], np.zeros(n)), (np.zeros(n), np.eye(n)[j])]):
            rhs = p0 + (dts*0.5*g*(1-eps)/mut)*w0; rhs[0] = 0
            wrhs = w0 + (1+eps)*Lw(rhs) + (1-eps)*Lw(p0); wrhs[0] = 0
            ws = Ainv @ wrhs; ws[0] = 0
            pn = rhs.copy()
            for k in range(1, kde+1):
                pn[k] = rhs[k] + 0.5*dts*g*(1+eps)*ws[k]/(mut)
            col = j if which == 0 else n + j
            M[:n, col] = ws; M[n:, col] = pn
    return np.abs(np.linalg.eigvals(M)).max()

lam_expl = lambda_max(0.1, False)   # explicit (no implicit A)
lam_semi = lambda_max(0.1, True)    # semi-implicit (WRF scheme)
lam_neut = lambda_max(0.0, True)    # eps=0: neutral

print(f"# von-Neumann amplification of the assembled semi-implicit acoustic substep (exact A^-1)")
print(f"#   explicit, no implicit A   : |lambda|_max = {lam_expl:.4f}   (should be >> 1: A is load-bearing)")
print(f"#   semi-implicit (epssm=0.1) : |lambda|_max = {lam_semi:.6f}  (should be <= 1: STABLE, slightly damped)")
print(f"#   eps=0 + implicit A        : |lambda|_max = {lam_neut:.6f}  (should be ~1: energy-conserving)")
ok = (lam_semi <= 1.0 + 1e-6) and (lam_expl > 5.0) and (abs(lam_neut - 1.0) < 1e-3)
print(f"# assembled semi-implicit coupling STABLE + implicit A load-bearing : {'PASS' if ok else 'FAIL'}")
sys.exit(0 if ok else 1)
