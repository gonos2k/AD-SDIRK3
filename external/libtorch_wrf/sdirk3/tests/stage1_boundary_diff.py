#!/usr/bin/env python3
"""9F.D6 stage-1->2 boundary oracle: WRF stock stage-1 decoupled output vs port se_rk=1 finished.

Diffs the DECOUPLED stage-1 output state {u,v,w,ph,t,mu} that feeds RK stage 2, on the identical
step-1 em_b_wave IC. A MATCH means the port's stage-1 integration + decoupling (small_step_finish)
is faithful, so a downstream stage-2 divergence lives in stage-2's operators. A MISMATCH means the
stage-1 decoupling/handoff itself is the defect. Interior-aligned (correlation offset, reported),
per-field e2/einf/corr + per-level for w. Nonzero exit if any field e2 > --tol.

Usage: stage1_boundary_diff.py --wrf wrf_stage1_out.bin --port port_stage1_out.bin [--tol 1e-2]
"""
import argparse, sys
import numpy as np

VARS = ["u", "v", "w", "ph", "t", "mu"]


def load_wrf(path):
    with open(path, "rb") as f:
        hdr = np.fromfile(f, dtype=">i4", count=6)
        ims, ime, jms, jme, kms, kme = (int(x) for x in hdr)
        ni, nk, nj = ime - ims + 1, kme - kms + 1, jme - jms + 1
        out = {"_hdr": (ims, ime, jms, jme, kms, kme)}
        for v in VARS:
            n = ni * nj if v == "mu" else ni * nk * nj
            a = np.fromfile(f, dtype=">f4", count=n)
            if a.size != n:
                raise ValueError(f"WRF dump truncated at {v}: {a.size} want {n}")
            shp = (ni, nj) if v == "mu" else (ni, nk, nj)
            out[v] = a.astype(np.float64).reshape(shp, order="F")
        return out


def load_port(path):
    out = {}
    with open(path, "rb") as f:
        for v in VARS:
            nd = int(np.fromfile(f, dtype=np.int64, count=1)[0])
            shp = tuple(int(x) for x in np.fromfile(f, dtype=np.int64, count=nd))
            n = int(np.prod(shp))
            a = np.fromfile(f, dtype=np.float32, count=n)
            if a.size != n:
                raise ValueError(f"port dump truncated at {v}: {a.size} want {n}")
            out[v] = a.astype(np.float64).reshape(shp)  # (j[,k],i)
    return out


def corr(a, b):
    if a.std() > 0 and b.std() > 0:
        return np.corrcoef(a.ravel(), b.ravel())[0, 1]
    return 1.0 if np.allclose(a, b) else 0.0


def align(W, P, v):
    if v == "mu":
        ni, nj = W.shape
        jP, iP = P.shape
        best = (-2.0, None)
        for i0 in range(ni - iP + 1):
            for j0 in range(nj - jP + 1):
                Wp = W[i0:i0 + iP, j0:j0 + jP].T
                c = corr(Wp, P)
                if c > best[0]:
                    best = (c, Wp)
        return best
    ni, nk, nj = W.shape
    jP, kP, iP = P.shape
    best = (-2.0, None)
    for i0 in range(ni - iP + 1):
        for j0 in range(nj - jP + 1):
            Wp = np.transpose(W[i0:i0 + iP, 0:kP, j0:j0 + jP], (2, 1, 0))
            c = corr(Wp, P)
            if c > best[0]:
                best = (c, Wp)
    return best


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--wrf", required=True)
    ap.add_argument("--port", required=True)
    ap.add_argument("--tol", type=float, default=1e-2)
    args = ap.parse_args()
    W, P = load_wrf(args.wrf), load_port(args.port)
    print(f"WRF hdr {W['_hdr']}")
    all_ok = True
    for v in VARS:
        c, Wp = align(W[v], P[v], v)
        if Wp is None:
            print(f"  {v:3} could not align"); all_ok = False; continue
        Pp = P[v]
        d = Pp - Wp
        e2 = np.linalg.norm(d) / max(np.linalg.norm(Wp), 1e-30)
        einf = np.abs(d).max() / max(np.abs(Wp).max(), 1e-30)
        ok = e2 <= args.tol
        all_ok &= ok
        print(f"  {v:3} e2={e2:9.3e} einf={einf:9.3e} corr={corr(Wp, Pp):+.5f} "
              f"|W|max={np.abs(Wp).max():.4g} |P|max={np.abs(Pp).max():.4g} -> {'PASS' if ok else 'FAIL'}")
        if v == "w" and Wp.ndim == 3:
            worst = []
            for k in range(Wp.shape[1]):
                wl = np.linalg.norm(Wp[:, k, :])
                if wl > 1e-9:
                    worst.append((np.linalg.norm(Pp[:, k, :] - Wp[:, k, :]) / wl, k))
            worst.sort(reverse=True)
            if worst:
                print("        w worst per-level e2: " + " ".join(f"k{k}:{e:.2f}" for e, k in worst[:6]))
    print(f"\nSTAGE1 BOUNDARY: {'PASS (stage-1 decoupled output faithful)' if all_ok else 'FAIL (stage-1 decoupling/handoff diverges)'} (tol e2<={args.tol})")
    sys.exit(0 if all_ok else 1)


if __name__ == "__main__":
    main()
