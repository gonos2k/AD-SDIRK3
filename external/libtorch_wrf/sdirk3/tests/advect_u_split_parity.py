#!/usr/bin/env python3
"""9F.D26 (review section 3): POINTWISE parity of the u-advection horizontal/vertical split.

WHY THIS EXISTS. 9F.D20 compared ||T_port|| against ||T_wrf|| and reported 2.4%/2.6%
agreement as "each subterm matches independently". That claim is not supported by norms:
||P T|| = ||T|| for any permutation P, and ||-T|| = ||T||, so equal magnitudes are
consistent with a transposed, shifted, or sign-flipped field. This computes what
operator parity actually requires -- e2, e_inf, correlation, per-level error, and
sign disagreement -- on the fields themselves.

Reports, and does NOT paper over, the alignment: WRF dumps its memory domain
(ims:ime, kms:kme, jms:jme) in (i,k,j) order; the port dumps its tile in (j,k,i). The
offset search is reported explicitly, because a correlation-maximising crop has
previously selected a periodic-x ALIAS in this project and produced a confident wrong
answer.

Usage: advect_u_split_parity.py --wrf wrf_advect_u_split.bin --port port_advect_u_split.bin
"""
import argparse
import sys

import numpy as np


class Thresholds:
    """Every computed metric is gated. Defaults are deliberately strict: this tool
    exists to decide operator parity, and a permissive gate that always passes is
    worse than no gate because it reads as evidence."""

    def __init__(self, e2=1e-2, e_inf=1e-2, corr_min=0.9999,
                 sign_max=0, per_level_e2=1e-2):
        self.e2, self.e_inf = e2, e_inf
        self.corr_min, self.sign_max, self.per_level_e2 = corr_min, sign_max, per_level_e2


def load_wrf(path):
    with open(path, "rb") as f:
        hdr = np.fromfile(f, dtype=">i4", count=6)
        if hdr.size != 6:
            raise SystemExit(f"{path}: short header")
        ims, ime, jms, jme, kms, kme = (int(x) for x in hdr)
        ni, nj, nk = ime - ims + 1, jme - jms + 1, kme - kms + 1
        n = ni * nk * nj
        # Fortran (i,k,j) column-major
        h = np.fromfile(f, dtype=">f4", count=n).reshape((nj, nk, ni))
        v = np.fromfile(f, dtype=">f4", count=n).reshape((nj, nk, ni))
    return h, v, (ims, ime, jms, jme, kms, kme)


def load_port(path):
    with open(path, "rb") as f:
        dims = np.fromfile(f, dtype="<i8", count=3)
        if dims.size != 3:
            raise SystemExit(f"{path}: short header")
        nj, nk, ni = (int(x) for x in dims)
        n = nj * nk * ni
        h = np.fromfile(f, dtype="<f4", count=n).reshape((nj, nk, ni))
        v = np.fromfile(f, dtype="<f4", count=n).reshape((nj, nk, ni))
    return h, v, (nj, nk, ni)


def stats(p, w, label, th):
    # 7.2: a NaN/Inf field would otherwise flow into every metric and produce a
    # meaningless verdict rather than an error.
    for nm, arr in (("port", p), ("wrf", w)):
        if not np.isfinite(arr).all():
            n = int((~np.isfinite(arr)).sum())
            print(f"  {label:<10} FAIL: {nm} contains {n} non-finite values")
            return False
    d = p - w
    nw = np.linalg.norm(w)
    e2 = np.linalg.norm(d) / max(nw, 1e-30)
    einf = np.abs(d).max() / max(np.abs(w).max(), 1e-30)
    a, b = p.ravel(), w.ravel()
    corr = float(np.dot(a, b) / max(np.linalg.norm(a) * np.linalg.norm(b), 1e-30))
    signdiff = int(np.sum((np.sign(p) * np.sign(w)) < 0))
    print(f"  {label:<10} e2={e2:.4e}  einf={einf:.4e}  corr={corr:+.6f}  "
          f"signdiff={signdiff}/{p.size}  "
          f"||p||={np.linalg.norm(p):.6g} ||w||={nw:.6g}")
    # per-level: a defect localised at the lid/surface hides inside a global e2
    per = []
    for k in range(p.shape[1]):
        dk = np.linalg.norm(d[:, k, :]) / max(np.linalg.norm(w[:, k, :]), 1e-30)
        per.append((dk, k))
    per.sort(reverse=True)
    worst = "  ".join(f"k{k}:{e:.3f}" for e, k in per[:6])
    print(f"             worst per-level e2: {worst}")
    # 7.1: the verdict previously used e2 ALONE, so a large error confined to a few
    # cells was diluted by the global norm and passed. Every metric that is computed
    # is now also gated -- computing a number and not acting on it is decoration.
    checks = {
        "e2":            e2   <= th.e2,
        "e_inf":         einf <= th.e_inf,
        "corr":          corr >= th.corr_min,
        "sign_mismatch": signdiff <= th.sign_max,
        "per_level_e2":  per[0][0] <= th.per_level_e2,
    }
    failed = [k for k, ok in checks.items() if not ok]
    if failed:
        print(f"             GATE FAIL on: {', '.join(failed)}")
    return not failed


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--wrf", required=True)
    ap.add_argument("--port", required=True)
    ap.add_argument("--tol", type=float, default=1e-2)
    ap.add_argument("--corr-min", type=float, default=0.9999)
    ap.add_argument("--sign-max", type=int, default=0)
    ap.add_argument("--per-level-tol", type=float, default=1e-2)
    a = ap.parse_args()

    wh, wv, whdr = load_wrf(a.wrf)
    ph, pv, phdr = load_port(a.port)
    print(f"WRF  hdr(ims,ime,jms,jme,kms,kme)={whdr}  array{wh.shape} (nj,nk,ni)")
    print(f"PORT shape{ph.shape} (nj,nk,ni)")

    # The port tile is the physical interior; WRF's dump includes the halo. Map with
    # the KNOWN physical offset i0 = 1 - ims, j0 = 1 - jms rather than a correlation
    # search: a correlation-maximising crop previously selected a periodic-x alias here.
    ims, ime, jms, jme, kms, kme = whdr
    i0, j0 = 1 - ims, 1 - jms
    nj, nk, ni = ph.shape
    print(f"deterministic crop: i0={i0} j0={j0} k0=0  (physical mass-interior mapping)")
    if j0 + nj > wh.shape[0] or i0 + ni > wh.shape[2] or nk > wh.shape[1]:
        raise SystemExit("port tile does not fit inside the WRF memory domain at that offset")
    whc = wh[j0:j0 + nj, 0:nk, i0:i0 + ni]
    wvc = wv[j0:j0 + nj, 0:nk, i0:i0 + ni]

    th = Thresholds(e2=a.tol, e_inf=a.tol, corr_min=a.corr_min,
                    sign_max=a.sign_max, per_level_e2=a.per_level_tol)
    print("\nPOINTWISE PARITY (this is what 'the operator matches' requires):")
    okh = stats(ph, whc, "horizontal", th)
    okv = stats(pv, wvc, "vertical", th)

    bad = [n for n, ok in (("horizontal", okh), ("vertical", okv)) if not ok]
    print(f"\nVERDICT: {'FAIL' if bad else 'PASS'}"
          f"  (e2<={th.e2}, einf<={th.e_inf}, corr>={th.corr_min}, "
          f"sign<={th.sign_max}, per-level e2<={th.per_level_e2})"
          + (f" -- {', '.join(bad)}" if bad else ""))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
