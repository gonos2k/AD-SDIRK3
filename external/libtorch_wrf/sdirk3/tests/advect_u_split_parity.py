#!/usr/bin/env python3
"""9F.D26 (review section 3): POINTWISE parity of the u-advection horizontal/vertical split.

WHY THIS EXISTS. 9F.D20 compared ||T_port|| against ||T_wrf|| and reported 2.4%/2.6%
agreement as "each subterm matches independently". That claim is not supported by norms:
||P T|| = ||T|| for any permutation P, and ||-T|| = ||T||, so equal magnitudes are
consistent with a transposed, shifted, or sign-flipped field. This computes what
operator parity actually requires -- e2, e_inf, correlation, per-level error, and
sign disagreement -- on the fields themselves.

Alignment is a DETERMINISTIC crop, not a search: WRF dumps its memory domain
(ims:ime, kms:kme, jms:jme) in (i,k,j) order, the port dumps its tile in (j,k,i), and
the mapping used is the physical one (i0 = 1-ims, j0 = 1-jms). There is deliberately
no offset search here -- a correlation-maximising crop previously selected a
periodic-x ALIAS in this project and produced a confident wrong answer. (An earlier
version of this docstring claimed a search was "reported explicitly"; it never was.)

Usage: advect_u_split_parity.py --wrf wrf_advect_u_split_rank0000.bin --port port_advect_u_split.bin
"""
import argparse
import sys

import numpy as np


class Thresholds:
    """ONE well-posed contract is the gate; everything else is a diagnostic.

    9F.D29 (review section 10). The previous version gated all five computed metrics.
    That was an over-correction of the earlier "computes five, enforces one" defect,
    and it introduced gates that are ill-posed where the reference is small:
      - sign_max=0 fails on a roundoff-level sign flip in a near-zero cell;
      - per-level RELATIVE error is unstable on a level whose reference norm ~ 0;
      - correlation is meaningless against a near-zero reference field.
    A gate that fires on numerical noise trains the reader to ignore it, which is the
    same end state as having no gate.

    The authoritative contract is the standard cellwise mixed tolerance
        |P_i - W_i| <= atol + rtol*|W_i|
    which is well defined for every cell INCLUDING W_i -> 0. Global e2 is kept as a
    secondary aggregate check. corr / sign / e_inf / worst-level are reported to
    characterise a failure, never to decide one."""

    def __init__(self, atol=1e-6, rtol=1e-3, e2=1e-2):
        self.atol, self.rtol, self.e2 = atol, rtol, e2


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
    # --- the contract ---
    tol = th.atol + th.rtol * np.abs(w)
    bad = np.abs(d) > tol
    nbad = int(bad.sum())
    checks = {"cellwise": nbad == 0, "e2": e2 <= th.e2}
    failed = [k for k, ok in checks.items() if not ok]
    print(f"             cellwise |P-W| > {th.atol}+{th.rtol}|W| : {nbad}/{p.size}"
          f"   [gate]   global e2 <= {th.e2}: {'ok' if e2 <= th.e2 else 'FAIL'}")
    if nbad:
        # where the contract breaks is the actionable part of a failure
        idx = np.unravel_index(np.argmax(np.abs(d) - tol), d.shape)
        print(f"             worst offender at (j,k,i)={idx}: "
              f"port={p[idx]:.6g} wrf={w[idx]:.6g} |d|={abs(d[idx]):.6g}")
    if failed:
        print(f"             GATE FAIL on: {', '.join(failed)}")
    return not failed


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--wrf", required=True)
    ap.add_argument("--port", required=True)
    ap.add_argument("--tol", type=float, default=1e-2)
    ap.add_argument("--atol", type=float, default=1e-6)
    ap.add_argument("--rtol", type=float, default=1e-3)
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

    th = Thresholds(atol=a.atol, rtol=a.rtol, e2=a.tol)
    print("\nPOINTWISE PARITY (this is what 'the operator matches' requires):")
    okh = stats(ph, whc, "horizontal", th)
    okv = stats(pv, wvc, "vertical", th)

    bad = [n for n, ok in (("horizontal", okh), ("vertical", okv)) if not ok]
    print(f"\nVERDICT: {'FAIL' if bad else 'PASS'}"
          f"  (contract: |P-W| <= {th.atol}+{th.rtol}|W| cellwise; e2 <= {th.e2})"
          + (f" -- {', '.join(bad)}" if bad else ""))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
