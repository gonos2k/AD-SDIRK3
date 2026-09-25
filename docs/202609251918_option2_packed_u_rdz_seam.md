# Packed periodic U-shear metric alias

Timestamp: 2026-09-25 19:18 JST

## Finding and fix

The packed periodic mass grid stores N unique X columns plus a repeated
endpoint. The U grid has N+2 slots, including the periodic seam and one packed
face alias. The first stage-local `rdz` interpolation treated the repeated mass
endpoint as an independent column, which gave the wrong west face and terminal
alias for nonconstant metrics.

`compute_defor13()` now takes the N unique mass columns, forms the N+1
periodic U faces with the adjacent-mass arithmetic average, then appends face 1
to produce the packed terminal alias. The unpacked path keeps its N+1 face
construction unchanged. V symmetric-Y handling is unchanged.

## Validation

- Rebuilt `test_scalar_diffusion_contract` successfully.
- New packed U-shear regression with `rdz=[1,2,3,4,5,6,7,1]` passed: west
  seam value 4, face 1 value 1.5, terminal alias 1.5, maximum error 0.
- `Scalar_Diffusion_Contract`: passed, 1/1.
- Option-2 U-X source-equation comparison still passes: maximum difference
  `7.77656e-11`, seam error 0. The oracle is a source-equation transcription;
  Fortran itself was not compiled.
- `git diff --check`: passed. Focused Graphify was refreshed after production
  edits.

No WRF model run was performed. The packed regression verifies the metric
interpolation primitive; it does not establish full packed U/V/W forecast
parity, MPI behavior, or broad option-2 support.
