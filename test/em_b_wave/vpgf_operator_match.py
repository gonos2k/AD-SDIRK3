#!/usr/bin/env python3
"""
Offline reference for advance_w's RHS vertical pressure-gradient operator
(module_small_step_em.F:1408-1413), the one remaining discretization TYPE in the acoustic
sub-step: a vertical SECOND difference (divergence of a vertical gradient), distinct from the
first-difference PGF/divergence in upgf/divergence_operator_match.py. Same c2a/rdnw/mh metrics
that calc_coef_w (Inc 2) is built from — this checks them directly as a 1st-power application.

Interior full level k (em_b_wave msf=1, cqw=1), rhs = ph' on full levels:
  vpgf(k) = 0.5*dts*g*(1+epssm)*rdn(k)*[  c2a(k)  *rdnw(k)  /mh(k)  *(rhs(k+1)-rhs(k))
                                        - c2a(k-1)*rdnw(k-1)/mh(k-1)*(rhs(k)-rhs(k-1)) ]
with mh(k)=c1h(k)*MUT+c2h(k). Constant metrics + rhs=R0 cos(pi*eta) -> analytic
0.5*dts*g*(1+epssm)*(c2a/mh)*d2(rhs)/deta2 = -0.5*dts*g*(1+epssm)*(c2a/mh)*R0*pi^2*cos(pi eta).
"""
import sys
import numpy as np

nz  = 40                      # mass levels
nzw = nz + 1                  # full levels 0..nz
eta_f = np.linspace(0.0, 1.0, nzw)          # full-level eta
deta  = 1.0 / nz
rdnw = np.full(nz,  1.0 / deta)             # mass-level reciprocal spacing (const)
rdn  = np.full(nzw, 1.0 / deta)             # full-level reciprocal spacing (const)

# constant thermodynamic metrics
MUT = 9.0e4
c1h = np.ones(nz); c2h = np.zeros(nz)       # sigma -> mh = MUT
mh  = c1h * MUT + c2h                        # (nz,) mass-level hybrid weight
c2a = np.full(nz, 1.16e6)                    # sound-speed stiffness (const)
dts = 200.0; g = 9.81; epssm = 0.1

# vertical mode: zero gradient at eta=0,1 -> clean interior second difference
R0 = 1000.0
rhs = R0 * np.cos(np.pi * eta_f)             # (nzw,) on full levels

def vpgf_op(rhs):
    # interior full levels k=1..nz-1 use mass levels k (upper) and k-1 (lower)
    out = np.zeros(nzw)
    for k in range(1, nz):                   # libtorch full index k
        up = c2a[k]   * rdnw[k]   / mh[k]   * (rhs[k+1] - rhs[k])
        lo = c2a[k-1] * rdnw[k-1] / mh[k-1] * (rhs[k]   - rhs[k-1])
        out[k] = 0.5 * dts * g * (1.0 + epssm) * rdn[k] * (up - lo)
    return out

vpgf = vpgf_op(rhs)

# analytic: 0.5*dts*g*(1+epssm)*(c2a/mh)*d2rhs/deta2, d2rhs/deta2 = -R0 pi^2 cos(pi eta)
analytic = 0.5 * dts * g * (1.0 + epssm) * (c2a[0] / mh[0]) * (-R0 * np.pi**2 * np.cos(np.pi * eta_f))
k_int = np.arange(1, nz)
mask = np.abs(analytic[k_int]) > 0.05 * np.abs(analytic[k_int]).max()
rel = np.abs(vpgf[k_int][mask] - analytic[k_int][mask]) / np.abs(analytic[k_int][mask])
worst = rel.max()
expect = (np.pi * deta) ** 2 / 12.0          # O(deta^2) 2nd-difference truncation floor

print(f"# vertical-PGF (2nd-difference) discretization check (analytic rhs=R0 cos(pi eta))")
print(f"#   nz={nz} deta={deta:.4f}  pi*deta={np.pi*deta:.4f}  expected O(deta^2) err ~ {expect:.3e}")
print(f"#   vpgf L2={np.linalg.norm(vpgf):.4f}  max={np.abs(vpgf).max():.4f}")
print(f"#   worst rel-err vs analytic d2/deta2 (non-zero pts): {worst:.3e}")
ok = worst < 3.0 * expect + 1e-3
print(f"# vertical-PGF discretization == analytic d2(rhs)/deta2 : {'PASS' if ok else 'FAIL'}")
sys.exit(0 if ok else 1)   # non-zero exit on FAIL so a regression can't pass silently
