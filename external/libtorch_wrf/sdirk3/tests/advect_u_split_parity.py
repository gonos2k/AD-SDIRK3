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


def stats(p, w, label):
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
    return e2, corr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--wrf", required=True)
    ap.add_argument("--port", required=True)
    ap.add_argument("--tol", type=float, default=1e-2)
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

    print("\nPOINTWISE PARITY (this is what 'the operator matches' requires):")
    e2h, ch = stats(ph, whc, "horizontal")
    e2v, cv = stats(pv, wvc, "vertical")

    bad = [n for n, e in (("horizontal", e2h), ("vertical", e2v)) if e > a.tol]
    print(f"\nVERDICT: {'FAIL' if bad else 'PASS'} (tol e2<={a.tol})"
          + (f" -- {', '.join(bad)}" if bad else ""))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
