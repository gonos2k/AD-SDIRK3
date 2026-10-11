# Red review of final PR297 document

Local timestamp: 2026-10-12 07:29:09 JST (+0900)

Reviewed `/private/tmp/pr297-reference-shape-20261012/docs/(202610120720)_pr297_rk3_reference_shape_and_rhs_remainder.md` read-only. The criteria and numerical claims are consistent with the frozen V3 evaluation and final portable CLI analysis:

- V3 and its PH erratum remain unchanged; h=0.9375 versus h=1.875 fails all 24 epsilon/3 rows.
- The document reports d1/d2 in the same orientation as the CLI, gives the fieldwise RMS ratios/cosines, and makes no formal order, Richardson, exact-truth, or phase/frequency claim.
- The triangle bound is stated conditionally for a common target and norm; `d2 > 2 epsilon` counts 3/5/6/6 fields at 60/120/180/240 s and is not used to select which run is wrong.
- The archived h=1.875 executable/runtime limitation is explicit. The RHS remainder is kept separate from the corrected diffusion terms and no causal attribution or full `F_delta` parity is claimed.
- No native run, rebuild, changed acceptance target, project-wide adoption, or speed superiority is claimed.

The packaging reference is now resolved. The linked `docs/evidence/20261012/pr297_reference_shape_evidence.zip` exists and hashes to `8ab675671d72b790ecc25aebcad26a675f9030444a567bd282fd6166318279cc`. The adjacent [offline verification receipt](../offline_verification_receipt.json) records 85 payloads with CRC/member SHA checks passing, exact 24 metrics/48 delta arrays and RHS arrays from a fresh ZIP extraction, both CLI commands exiting 0, and no model run or rebuild. I checked the receipt and ZIP SHA read-only; I did not repeat extraction or metric analysis.

No repository files were edited and no model runs or tests were performed for this review.
