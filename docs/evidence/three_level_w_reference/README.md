# Three-level WRF reference evidence

This folder contains the single h=0.234375 public-history run, compared with the retained h=0.46875 middle run and the archived h=0.9375 coarse run. The root-final protocol is included unchanged (`root_final_protocol.json`; SHA-256 `7a3055189a01233837a67e8c7e7044ad34b165a42218c5f8160076657a9f83e0`). The conditional candidate is the existing h=15 s archive, and it is evaluated only after the h=0.234375 versus h=0.46875 reference screen passes all 24 fixed epsilon/3 rows.

`h3_published_physical_fields.npz` and `archived_h15_published_physical_fields.npz` each hold 24 public physical-owner arrays: six transformed fields at 60, 120, 180, and 240 seconds. Values are the FP32 NetCDF fields promoted to FP64 after the V3 physical-owner crop and T/MU transforms. `run_provenance.json` binds them to the h3 receipt and archived h15 provenance. `portable_recomputed_results.json` and the three CSVs report the six-field official reference rows, conditional full-epsilon h15 rows, and descriptive D1/D2 RMS ratios and spatial cosines.

The 24.9 MB h=.9375/h=.46875 checkpoint source NPZ is not duplicated. It is read from the adjacent `../pr310_strict_reference_240s/published_fields_and_W.npz`, whose SHA-256 is pinned in `manifest.json`. From this folder, run:

```sh
python3.10 recompute_three_level_numpy.py
```

The recomputation uses NumPy and Python’s standard library only. It regenerates the screen/shape JSON and CSVs from the three checkpoint NPZ sources. D1/D2 ratios and cosines are descriptive; they are not order estimates or acceptance gates.

A fresh-folder recomputation used the recompute script and manifest plus four data inputs: the root-final protocol, the two bundled checkpoint NPZs, and the adjacent legacy NPZ. The test copied the 24.9 MB legacy NPZ only into a temporary sibling directory, verified all recomputed rows against the packaged outputs, and removed that temporary copy. See `fresh_folder_recompute_receipt.json` for the exact command and hashes. The validated result is 24/24 reference rows and 20/24 archived h15 candidate rows.

The `old_pair_w_spatial_*` files preserve the separate pre-run spatial audit. The direct-NetCDF/Red/source-audit files bind the original local inputs and analysis; they are provenance helpers with archived absolute paths, not additional requirements for the portable NumPy recomputation. `independent_agreement.json` compares all 48 official rows across Root, Green and Red.
