# Research forward setting: dry em_b_wave 240 s

`research_forward_map.json` registers the archived h=15/16 s, Newton absolute 1e-9 setting for this named forward research case. `namelist.input` is the exact archived namelist, not a changed production default. Source/executable/input and auxiliary/operator settings are tied to the existing PR310 receipts. No new WRF integration, rebuild, or derivative/observation-window test was run.

The direct h=.234375 minus h=.9375 comparison passes **24/24 full-epsilon rows**. Maximum direct RMS/epsilon is 0.5025718510754528; the maximum triangle upper bound/epsilon is 0.5029398053871652, both W at 180 s. The old .9375/.46875 epsilon/3 screen remains 22/24; the qualified .46875/.234375 screen remains 24/24. The archived h15 candidate remains 20/24, W failing at all four checkpoints.

From this directory, run:

```sh
python3 check_existing_coarse_candidate.py
```

Only Python/NumPy is required. Six inputs are hash-pinned: the adjacent PR310 NPZ, the h3 checkpoint NPZ and root-final protocol, plus the original coarse/middle/h3 run receipts. These files already exist in `../pr310_strict_reference_240s` and `../three_level_w_reference`; none is duplicated here. The arrays already contain V3 owner crops, FP32-to-FP64 promotion and T+300/MU+MUB transforms. Do not apply them again. Fixed-bottom exclusion and dynamic-top/physical-V-wall inclusion are unchanged.

JSON/CSV report the 24 direct rows, their limits/pass flags, and descriptive triangle bounds/cosines. Exit 0 means successful recomputation; `pass_count`/`all_24_pass` state acceptance. A fresh-folder test copied the same relative six dependencies and the script, reproduced all 24 numeric rows, then removed the temporary tree. JSON provenance paths change with the test location, so whole-result JSON bytes are not claimed identical. Root/Green/Red numeric agreement is recorded separately.

Forward acceptance does not certify continuous truth, all intermediate times, convergence order, or a general observation-window adjoint. Observation H/times/R/controls and derivative budgets are not invented here; forecast epsilon is not an observation covariance. The existing fixed-trajectory entrypoints and actual integration gaps are documented in `source_graph_receipt.json` and the case report. Large-timestep accuracy/efficiency is a separate target.
