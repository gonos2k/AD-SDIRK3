#!/usr/bin/env python3
"""H1 discriminator (2026-07-11): von-Neumann amplification |lambda| of ONE advance_w acoustic
substep with the REAL em_b_wave vertical profile (non-uniform rdnw 28->198, real c2a column,
operational dts) instead of acoustic_amplification_match.py's uniform-deta idealization.

Motivation: the split-explicit long run dies at step 3 with growth PROPORTIONAL TO SUBSTEP COUNT
(x5.4/x17/x102 for 1/2/4 substeps) => per-substep amplification suspected in the w-phi loop.
Offline |lambda|=0.998 was proven ONLY for uniform metrics. If the real profile gives |lambda|>1,
H1 (in-model coefficient/profile inconsistency) is structurally confirmed; if <=1, the pure
vertical w-phi coupling is exonerated and the instability needs theta/mu/horizontal coupling
(driver bookkeeping / omitted pg_buoy restoring term).

Same scheme + exact-inverse method as acoustic_amplification_match.py (docstring there).
Profile source: wrfout frame 0 (the balanced IC), column selectable; c2a = cpovcv*p_full/al_full
with hypso-1 geometric alpha (exactly what the model's make_thermo feeds calc_coef_w).
"""
import numpy as np
import netCDF4 as nc

d = nc.Dataset('wrfout_d01_0001-01-01_00:00:00')
g = 9.81
cpovcv = 1004.0 / 717.0  # cp/cv, WRF values

def column(j, i):
    phf = (d.variables['PH'][0, :, j, i] + d.variables['PHB'][0, :, j, i]).astype(np.float64)  # nzw
    mut = float(d.variables['MU'][0, j, i] + d.variables['MUB'][0, j, i])
    p_full = (d.variables['P'][0, :, j, i] + d.variables['PB'][0, :, j, i]).astype(np.float64)  # nz
    rdnw = np.abs(np.array(d.variables['RDNW'][0, :], dtype=np.float64))                        # nz
    rdn = np.abs(np.array(d.variables['RDN'][0, :], dtype=np.float64))                          # nz (rdn[0]=0 sentinel)
    try:
        c1h = np.array(d.variables['C1H'][0, :], dtype=np.float64)
        c2h = np.array(d.variables['C2H'][0, :], dtype=np.float64)
        c1f = np.array(d.variables['C1F'][0, :], dtype=np.float64)
        c2f = np.array(d.variables['C2F'][0, :], dtype=np.float64)
    except KeyError:
        nz = rdnw.size
        c1h, c2h = np.ones(nz), np.zeros(nz)
        c1f, c2f = np.ones(nz + 1), np.zeros(nz + 1)
    al = rdnw * np.diff(phf) / mut                     # hypso-1 geometric alpha (= model make_thermo)
    c2a = cpovcv * p_full / al
    mh = c1h * mut + c2h
    mf = c1f * mut + c2f
    return c2a, rdnw, rdn, mh, mf, mut, al

def lambda_max(c2a, rdnw, rdn, mh, mf, mut, dts, eps):
    nz = c2a.size; nzw = nz + 1; kde = nzw - 1
    A = np.eye(nzw)
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
    for jj in range(n):
        for which, (w0, p0) in enumerate([(np.eye(n)[jj], np.zeros(n)), (np.zeros(n), np.eye(n)[jj])]):
            rhs = p0 + (dts*0.5*g*(1-eps)/mut)*w0; rhs[0] = 0
            wr = w0 + (1+eps)*Lw(rhs) + (1-eps)*Lw(p0); wr[0] = 0
            ws = Ainv @ wr; ws[0] = 0
            pn = rhs.copy()
            for k in range(1, kde+1):
                pn[k] = rhs[k] + 0.5*dts*g*(1+eps)*ws[k]/mf[k]
            c = jj if which == 0 else n + jj; M[:n, c] = ws; M[n:, c] = pn
    return np.abs(np.linalg.eigvals(M)).max()

print("# REAL-PROFILE amplification |lambda| of one advance_w substep (pure w-phi mode, exact inverse)")
for (j, i, tag) in [(40, 22, "jet core (blow-up site)"), (5, 5, "south flank"), (75, 30, "north flank")]:
    c2a, rdnw, rdn, mh, mf, mut, al = column(j, i)
    print(f"# column j={j} i={i} ({tag}): mut={mut:.0f} al[0]={al[0]:.3f} al[-1]={al[-1]:.3f} "
          f"c2a[0]={c2a[0]:.3e} c2a[-1]={c2a[-1]:.3e} rdnw[0]={rdnw[0]:.1f} rdnw[-1]={rdnw[-1]:.1f}")
    for dts in (50.0, 150.0, 200.0):
        lam1 = lambda_max(c2a, rdnw, rdn, mh, mf, mut, dts, 0.1)
        lam0 = lambda_max(c2a, rdnw, rdn, mh, mf, mut, dts, 0.0)
        print(f"#   dts={dts:5.0f}  |lambda|(eps=0.1)={lam1:.6f}   (eps=0: {lam0:.6f})")
