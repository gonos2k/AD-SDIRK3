#!/usr/bin/env python3
"""Matched-input dyn_em parity diff: WRF rk_tendency vs port compute_k_slow at step-1 stage-1."""
import numpy as np, sys

RUN = "/private/tmp/claude-501/-Users-yhlee-SDIRK3/388f0325-39f0-411b-b6ee-a7603b77c210/scratchpad/wt-pr9f1/test/em_b_wave"
VARS = ["ru", "rv", "rw", "ph", "t", "mu"]

def load_wrf(path):
    with open(path, "rb") as f:
        hdr = np.fromfile(f, dtype=">i4", count=6)  # WRF Fortran I/O is BIG-ENDIAN
        ims, ime, jms, jme, kms, kme = hdr
        ni, nk, nj = ime-ims+1, kme-kms+1, jme-jms+1
        print(f"WRF hdr ims:ime={ims}:{ime} jms:jme={jms}:{jme} kms:kme={kms}:{kme}  => (ni,nk,nj)=({ni},{nk},{nj})")
        out = {}
        for v in VARS:
            if v == "mu":  # mu_tend is 2D (i,j)
                a = np.fromfile(f, dtype=">f4", count=ni*nj).astype(np.float32)
                out[v] = a.reshape((ni, nj), order="F")
            else:
                a = np.fromfile(f, dtype=">f4", count=ni*nk*nj).astype(np.float32)
                out[v] = a.reshape((ni, nk, nj), order="F")  # Fortran (i,k,j) col-major
        return out

def load_port(path):
    out = {}
    with open(path, "rb") as f:
        for v in VARS:
            nd = np.fromfile(f, dtype=np.int64, count=1)[0]
            shp = tuple(int(x) for x in np.fromfile(f, dtype=np.int64, count=nd))
            n = int(np.prod(shp))
            a = np.fromfile(f, dtype=np.float32, count=n).reshape(shp)  # C-order (ny,nz,nx)=(j,k,i)
            out[v] = a
    return out

w = load_wrf(f"{RUN}/wrf_rk_tend_dump.bin")
p = load_port(f"{RUN}/port_k_slow_dump.bin")

print(f"\n{'var':4} {'WRF shape (i,k,j)':20} {'port shape (j,k,i)':20} {'WRF max|.|':>12} {'port max|.|':>12} {'WRF L2':>12} {'port L2':>12} {'ratio(p/w)':>10}")
for v in VARS:
    wv, pv = w[v], p[v]
    wmax, pmax = np.abs(wv).max(), np.abs(pv).max()
    wl2, pl2 = np.linalg.norm(wv), np.linalg.norm(pv)
    ratio = pmax/wmax if wmax > 0 else float('nan')
    print(f"{v:4} {str(wv.shape):20} {str(pv.shape):20} {wmax:12.5g} {pmax:12.5g} {wl2:12.5g} {pl2:12.5g} {ratio:10.3f}")
