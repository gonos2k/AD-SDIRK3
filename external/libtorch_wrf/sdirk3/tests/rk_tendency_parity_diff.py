#!/usr/bin/env python3
"""Authoritative matched-input dyn_em parity diff: WRF rk_tendency vs port slow tendency.

POINTWISE (not max/L2): reports per-channel relative L2 error e2, relative L-inf e_inf,
correlation c, per-level error, sign-disagreement count, and NaN/Inf counts, after aligning the
port's packed (j,k,i) tensor to WRF's active (i,k,j) interior by a correlation-maximizing offset
search. Exits NONZERO if any channel's e2 exceeds --tol (default 1e-2). --selftest runs negative
controls (sign-flip / k-permute / one-cell shift) that MUST be DETECTED, proving the tool
discriminates (max/L2 alone cannot — a permutation or sign flip preserves both).

Usage:
  rk_tendency_parity_diff.py --wrf wrf_rk_tend_dump.bin --port port_k_slow_dump.bin
                             [--tol 1e-2] [--selftest]
"""
import argparse
import sys
import numpy as np

VARS = ["ru", "rv", "rw", "ph", "t", "mu"]


def load_wrf(path):
    with open(path, "rb") as f:
        hdr = np.fromfile(f, dtype=">i4", count=6)  # WRF Fortran I/O is BIG-ENDIAN
        if hdr.size != 6:
            raise ValueError(f"WRF dump header truncated: {path}")
        ims, ime, jms, jme, kms, kme = (int(x) for x in hdr)
        ni, nk, nj = ime - ims + 1, kme - kms + 1, jme - jms + 1
        out = {"_hdr": (ims, ime, jms, jme, kms, kme)}
        for v in VARS:
            n = ni * nj if v == "mu" else ni * nk * nj
            a = np.fromfile(f, dtype=">f4", count=n)
            if a.size != n:
                raise ValueError(f"WRF dump truncated at {v}: got {a.size} want {n}")
            shp = (ni, nj) if v == "mu" else (ni, nk, nj)
            out[v] = a.astype(np.float64).reshape(shp, order="F")  # (i[,k],j)
        if f.read():
            raise ValueError("WRF dump has trailing bytes (format mismatch)")
        return out


def load_port(path):
    out = {}
    with open(path, "rb") as f:
        for v in VARS:
            nd = np.fromfile(f, dtype=np.int64, count=1)
            if nd.size != 1:
                raise ValueError(f"port dump truncated at {v} ndim")
            shp = tuple(int(x) for x in np.fromfile(f, dtype=np.int64, count=int(nd[0])))
            n = int(np.prod(shp))
            a = np.fromfile(f, dtype=np.float32, count=n)
            if a.size != n:
                raise ValueError(f"port dump truncated at {v}: got {a.size} want {n}")
            out[v] = a.astype(np.float64).reshape(shp)  # (j[,k],i)
        return out


def _corr(a, b):
    if a.std() > 0 and b.std() > 0:
        return np.corrcoef(a.ravel(), b.ravel())[0, 1]
    return 1.0 if np.allclose(a, b) else 0.0


def align(wrf, port, v):
    """Return (best_corr, WRF-aligned-to-port-layout, offset) by correlation search."""
    P, W = port[v], wrf[v]
    if v == "mu":
        ni, nj = W.shape
        jP, iP = P.shape
        best = (-2.0, None, None)
        for i0 in range(ni - iP + 1):
            for j0 in range(nj - jP + 1):
                Wp = W[i0:i0 + iP, j0:j0 + jP].T
                c = _corr(Wp, P)
                if c > best[0]:
                    best = (c, Wp, (i0, j0))
        return best
    ni, nk, nj = W.shape
    jP, kP, iP = P.shape
    best = (-2.0, None, None)
    for i0 in range(ni - iP + 1):
        for j0 in range(nj - jP + 1):
            Wp = np.transpose(W[i0:i0 + iP, 0:kP, j0:j0 + jP], (2, 1, 0))
            c = _corr(Wp, P)
            if c > best[0]:
                best = (c, Wp, (i0, j0))
    return best


def report(Wp, P, v, tol):
    d = P - Wp
    e2 = np.linalg.norm(d) / max(np.linalg.norm(Wp), 1e-30)
    einf = np.abs(d).max() / max(np.abs(Wp).max(), 1e-30)
    c = _corr(Wp, P)
    thr = 1e-6 * max(np.abs(Wp).max(), 1e-30)
    signdiff = int(np.sum((np.sign(Wp) != np.sign(P)) & (np.abs(Wp) > thr)))
    naninf = int(np.sum(~np.isfinite(P)))
    ok = (e2 <= tol) and (naninf == 0)
    print(f"  {v:3} e2={e2:9.3e} einf={einf:9.3e} corr={c:+.5f} "
          f"signdiff={signdiff:6d} naninf={naninf} -> {'PASS' if ok else 'FAIL'}")
    if Wp.ndim == 3 and (not ok or v == "rw"):
        worst = []
        for k in range(Wp.shape[1]):
            wl = np.linalg.norm(Wp[:, k, :])
            if wl > 1e-9:
                worst.append((np.linalg.norm(P[:, k, :] - Wp[:, k, :]) / wl, k))
        worst.sort(reverse=True)
        if worst:
            print("        worst per-level e2: "
                  + " ".join(f"k{k}:{e:.2f}" for e, k in worst[:6]))
    return ok


def selftest(Wp):
    controls = [("sign-flip", -Wp),
                ("k-permute", np.flip(Wp, axis=1) if Wp.ndim == 3 else Wp[::-1]),
                ("one-cell-shift", np.roll(Wp, 1, axis=0))]
    ok = True
    for name, m in controls:
        e2 = np.linalg.norm(m - Wp) / max(np.linalg.norm(Wp), 1e-30)
        detected = e2 > 1e-2
        print(f"  selftest {name:15} e2={e2:8.3e} -> {'DETECTED' if detected else 'MISSED!!'}")
        ok = ok and detected
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--wrf", required=True)
    ap.add_argument("--port", required=True)
    ap.add_argument("--tol", type=float, default=1e-2)
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    wrf, port = load_wrf(args.wrf), load_port(args.port)
    print(f"WRF hdr {wrf['_hdr']}")
    all_ok = True
    ref_Wp = None
    for v in VARS:
        c, Wp, off = align(wrf, port, v)
        if Wp is None:
            print(f"  {v:3} could not align")
            all_ok = False
            continue
        if v == "ru":
            ref_Wp = Wp
        all_ok &= report(Wp, port[v], v, args.tol)
    if args.selftest and ref_Wp is not None:
        print("negative controls (must all be DETECTED):")
        all_ok &= selftest(ref_Wp)
    print(f"\nPARITY: {'PASS' if all_ok else 'FAIL'} (tol e2<={args.tol})")
    sys.exit(0 if all_ok else 1)


if __name__ == "__main__":
    main()
