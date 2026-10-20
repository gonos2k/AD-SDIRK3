#!/usr/bin/env python3
"""Offline spatial and amplitude/time-shift diagnostics for pinned PR310 W trajectories."""
from __future__ import annotations
import argparse
import csv
import hashlib
import json
from fractions import Fraction
from pathlib import Path
import numpy as np

TIMES = (81.5625, 170.625, 180.0, 240.0)
DT_FINE = Fraction(15, 32)
EXPECTED_DATA_SHA = "4fe480ad79bc9957488f8b02498fde4bb9a45bff7f10d246bd1826490778070e"
EXPECTED_PROTOCOL_SHA = "2c7ac76fef65e917bf02dc08a2dba15ee5531ac49b2756aa4e628c3dd48df94e"


def sha(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def stat(x: np.ndarray) -> dict:
    x = np.asarray(x, dtype=np.float64)
    return {"count": int(x.size), "rms": float(np.sqrt(np.mean(x * x))),
            "max_abs": float(np.max(np.abs(x))), "mean": float(np.mean(x)),
            "squared_norm": float(np.sum(x * x))}


def process(npz_path: Path, protocol_path: Path, out_dir: Path) -> dict:
    if sha(npz_path) != EXPECTED_DATA_SHA:
        raise ValueError("published_fields_and_W.npz SHA does not match preregistration")
    if sha(protocol_path) != EXPECTED_PROTOCOL_SHA:
        raise ValueError("spatial structure protocol SHA does not match preregistration")
    protocol = json.loads(protocol_path.read_text())
    with np.load(npz_path, allow_pickle=False) as z:
        coarse = np.asarray(z["coarse_W_internal_projected_kji"], dtype=np.float64)
        fine = np.asarray(z["fine_W_internal_projected_kji"], dtype=np.float64)
        m0 = np.asarray(z["fixed_M0_kg"], dtype=np.float64)
        e1 = np.asarray(z["fixed_e1_dimensionless"], dtype=np.float64)
        owner = np.asarray(z["owner_mask_W"], dtype=bool)
    if coarse.shape != (257, 16, 16, 16) or fine.shape != (513, 16, 16, 16):
        raise ValueError(f"unexpected W endpoint shapes: coarse={coarse.shape}, fine={fine.shape}")
    if m0.shape != (16, 16, 16) or e1.shape != m0.shape or owner.shape != m0.shape or not owner.all():
        raise ValueError("frozen M0/e1/owner arrays must cover the same 4096 W owners")
    if not (np.isfinite(coarse).all() and np.isfinite(fine).all() and
            np.isfinite(m0).all() and np.isfinite(e1).all()):
        raise ValueError("nonfinite input in compared W/M0/e1 arrays")
    if np.any(m0 <= 0):
        raise ValueError("frozen W dual mass must be positive on all owners")

    total_m = float(np.sum(m0))
    e1_rms_m0 = float(np.sqrt(np.sum(m0 * e1 * e1) / total_m))
    if not np.isclose(e1_rms_m0, 1.0, rtol=2e-12, atol=2e-12):
        raise ValueError(f"frozen e1 weighted RMS is not 1: {e1_rms_m0}")

    # Disjoint horizontal cells: y-wall rows first; x-seam-adjacent columns
    # only among remaining y-interior rows; then strict horizontal interior.
    wall = np.zeros((16, 16), dtype=bool); wall[[0, 15], :] = True
    seam = np.zeros((16, 16), dtype=bool); seam[:, [0, 15]] = True
    disjoint = {
        "y_wall_adjacent_rows_in_W_mass_grid": np.broadcast_to(wall, (16, 16, 16)),
        "x_periodic_seam_adjacent_after_wall_rows": np.broadcast_to((~wall & seam), (16, 16, 16)),
        "horizontal_interior": np.broadcast_to((~wall & ~seam), (16, 16, 16)),
    }
    expected_counts = {"y_wall_adjacent_rows_in_W_mass_grid":512,
                       "x_periodic_seam_adjacent_after_wall_rows":448,
                       "horizontal_interior":3136}
    if {k:int(v.sum()) for k,v in disjoint.items()} != expected_counts:
        raise AssertionError("disjoint horizontal region counts changed")

    rows=[]; fit_records=[]; diff_arrays={}
    m0_norm=float(np.sum(m0))
    max_j_indices=[]
    for t in TIMES:
        coarse_j = Fraction(str(t)) / DT_FINE * Fraction(1, 2)  # t/(15/16)
        if coarse_j.denominator != 1:
            raise ValueError(f"{t}s is not on the coarse/fine common grid")
        j=int(coarse_j); n=2*j
        if not (0 <= j < len(coarse) and 0 <= n < len(fine)):
            raise ValueError(f"endpoint index out of range at {t}s")
        wc=coarse[j]; wf=fine[n]; d=wf-wc
        if not (np.isfinite(wc).all() and np.isfinite(wf).all() and np.isfinite(d).all()):
            raise ValueError(f"nonfinite W at {t}s")
        overall=stat(d); sq=float(np.sum(d*d))
        layer_sq=np.sum(d*d,axis=(1,2))
        layer_rows=[{"k_zero_based":k,"is_dynamic_top_level":k==15,"owner_count":256,
                     "squared_norm":float(x),"squared_norm_share":float(x/sq) if sq else None,
                     "share_denominator_owner_count":4096,"share_denominator_squared_norm":sq}
                    for k,x in enumerate(layer_sq)]
        region_rows={}
        for name,mask in disjoint.items():
            vals=d[mask]
            region_rows[name]={**stat(vals),"squared_norm_share":float(np.sum(vals*vals)/sq) if sq else None,
              "share_denominator_owner_count":4096,"share_denominator_squared_norm":sq}
        ywall_vals=d[:,[0,15],:]; xseam_vals=d[:,:,[0,15]]
        overlapping={
            "y_wall_adjacent_rows_in_W_mass_grid_marginal":{"count":int(ywall_vals.size),"squared_norm_share":float(np.sum(ywall_vals*ywall_vals)/sq) if sq else None,
              "share_denominator_owner_count":4096,"share_denominator_squared_norm":sq},
            "x_seam_adjacent_columns_marginal":{"count":int(xseam_vals.size),"squared_norm_share":float(np.sum(xseam_vals*xseam_vals)/sq) if sq else None,
              "share_denominator_owner_count":4096,"share_denominator_squared_norm":sq}}
        imax=np.unravel_index(int(np.argmax(np.abs(d))),d.shape)
        maxloc={"k_zero_based":int(imax[0]),"j_zero_based":int(imax[1]),"i_zero_based":int(imax[2]),"signed_delta_m_s":float(d[imax])}
        weighted_rms=float(np.sqrt(np.sum(m0*d*d)/m0_norm))
        projection=float(np.sum(m0*d*e1)/m0_norm)
        rows.append({"time_seconds":t,"coarse_common_index":j,"fine_common_index":n,
          "delta_definition":"fine - coarse","unit":"m/s","owner_count":4096,
          "whole_field":overall,"vertical_layers":layer_rows,"dynamic_top_k15":{"owner_count":256,
            "squared_norm_share":layer_rows[15]["squared_norm_share"],
            "share_denominator_owner_count":4096,"share_denominator_squared_norm":sq},
          "horizontal_disjoint_regions":region_rows,"horizontal_overlapping_marginals":overlapping,
          "horizontal_region_denominator":{"owner_count":4096,"squared_norm":sq},
          "max_absolute_index_kji":maxloc,"fixed_M0_weighted_RMS_m_s":weighted_rms,
          "fixed_e1_M0_projection_m_s":projection})
        diff_arrays[f"delta_t{str(t).replace('.','p')}_kji"]=d
        diff_arrays[f"coarse_t{str(t).replace('.','p')}_kji"]=wc
        diff_arrays[f"fine_t{str(t).replace('.','p')}_kji"]=wf
        max_j_indices.append(j)

        if n > 0 and n < len(fine)-1:
            derivative=(fine[n+1]-fine[n-1])/(2*float(DT_FINE))
            x1=wf.ravel(); x2=derivative.ravel(); y=d.ravel()
            norms=np.array([np.linalg.norm(x1),np.linalg.norm(x2)],dtype=np.float64)
            if np.any(norms == 0):
                raise ValueError(f"zero amplitude/time-derivative basis at {t}s")
            X=np.column_stack((x1/norms[0],x2/norms[1]))
            U,svals,Vh=np.linalg.svd(X,full_matrices=False)
            tol=np.finfo(np.float64).eps*max(X.shape)*svals[0]
            rank=int(np.count_nonzero(svals > tol))
            cond=float(svals[0]/svals[-1]) if rank==2 and svals[-1]>0 else None
            gamma,_,_,_=np.linalg.lstsq(X,y,rcond=None)
            alpha=float(gamma[0]/norms[0]); beta=float(gamma[1]/norms[1])
            residual=y-(alpha*x1+beta*x2)
            fit_records.append({"time_seconds":t,"coarse_common_index":j,"fine_common_index":n,
              "basis":"[W_f, centered_d_t_W_f]","derivative_dt_seconds":float(DT_FINE),
              "column_euclidean_norms":[float(x) for x in norms],
              "normalized_column_gram":(X.T@X).tolist(),"singular_values":svals.tolist(),
              "svd_rank_tolerance":float(tol),"svd_rank":rank,"condition_number":cond,
              "alpha_amplitude_dimensionless":alpha,"beta_time_shift_seconds":beta,
              "orthogonal_residual":{"rms_m_s":float(np.sqrt(np.mean(residual*residual))),
                "max_abs_m_s":float(np.max(np.abs(residual))),
                "fraction_of_delta_l2":float(np.linalg.norm(residual)/np.linalg.norm(y)) if np.linalg.norm(y)>0 else None},
              "fit_interpretation":"descriptive least-squares projection; not a physical or causal correction"})
            diff_arrays[f"centered_dt_fine_t{str(t).replace('.','p')}_kji"]=derivative
        else:
            fit_records.append({"time_seconds":t,"coarse_common_index":j,"fine_common_index":n,
              "status":"not_estimable","reason":"terminal 240 s has no centered fine-grid derivative because 240+15/32 s is outside the stored trajectory"})

    out_dir.mkdir(parents=True,exist_ok=True)
    npz_out=out_dir/"pr310_w_spatial_differences.npz"
    np.savez_compressed(npz_out,**diff_arrays)
    summary={"schema":"pr310_w_spatial_structure_results_v1",
      "protocol_path":str(protocol_path),"protocol_sha256":sha(protocol_path),
      "input_npz_path":str(npz_path),"input_npz_sha256":sha(npz_path),
      "coarse_internal_W_endpoints":257,"fine_internal_W_endpoints":513,
      "coarse_dt_seconds":"15/16","fine_dt_seconds":"15/32",
      "owner_contract":{"shape_kji":[16,16,16],"owner_count":4096,"fixed_bottom_excluded":True,
        "dynamic_top_included":True,"owner_mask_all_true":True,"geometry_coordinates":"not reported; no coordinates are present in pinned NPZ"},
      "horizontal_grid_interpretation":{"j0_j15":"rows adjacent to the physical Y walls in the W mass grid; these are not V boundary-face values",
        "i0_i15":"periodic-X seam-adjacent W mass-grid columns; these are not duplicate owners",
        "disjoint_region_counts":{"y_wall_adjacent_rows_in_W_mass_grid":512,
          "x_periodic_seam_adjacent_after_wall_rows":448,"horizontal_interior":3136},
        "overlapping_marginal_counts":{"y_wall_adjacent_rows":512,"x_seam_adjacent_columns":512,
          "y_wall_and_x_seam_intersection":64,"dynamic_top_and_y_wall_intersection":32,
          "dynamic_top_and_x_seam_intersection":32,"dynamic_top_y_wall_x_seam_intersection":4},
        "all_squared_norm_share_denominators":"whole-field squared norm over 4096 W owners; disjoint categories sum to 4096, marginals intentionally overlap"},
      "fixed_basis_check":{"M0_positive_all_owners":True,"e1_M0_weighted_RMS":e1_rms_m0},
      "times":rows,"amplitude_time_shift_fits":fit_records,
      "outputs":{"difference_npz_path":str(npz_out),"difference_npz_sha256":sha(npz_out),
        "difference_npz_size_bytes":npz_out.stat().st_size},
      "interpretation_limits":["Spatial and fit metrics are descriptive diagnostics, not an automatic acceptance test.",
        "No cause, error bound, Richardson order, or physical time correction is established.",
        "The 240 s time-shift fit is unavailable because the preregistered centered derivative cannot be formed at the endpoint."]}
    js=out_dir/"pr310_w_spatial_structure_results.json"
    # Keep the summary free of a misleading self-hash; a sibling receipt binds
    # the final JSON bytes after serialization.
    js.write_text(json.dumps(summary,indent=2,sort_keys=True)+"\n")
    return summary


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--npz',type=Path,required=True)
    p.add_argument('--protocol',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args()
    d=process(a.npz,a.protocol,a.out)
    print(json.dumps({'summary_json':str(a.out/'pr310_w_spatial_structure_results.json'),
      'difference_npz':str(a.out/'pr310_w_spatial_differences.npz'),'times':len(d['times'])},indent=2))

if __name__=='__main__':
    main()
