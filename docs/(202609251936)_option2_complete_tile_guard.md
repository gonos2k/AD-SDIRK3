# Option-2 complete-tile support guard

Timestamp: 2026-09-25 19:36:07 JST

## Change

The stage-local option-2 metric producer applies periodic X wrapping and
symmetric-Y wall slopes to the local geopotential field. This construction is
supported only when one rank owns the complete horizontal domain. Step 9 now
requires `nprocx_ * nprocy_ == 1` and tile bounds covering the complete domain
before selecting the dry isotropic `km_opt=1` fast path. The broader
`option2_native_declared` predicate remains unchanged, so unsupported declared
option-2 configurations fail closed through the existing guard. The diagnostic
now identifies the complete-single-rank ownership requirement.

## Validation

- Production library and `test_scalar_diffusion_contract` rebuilt successfully.
- `Scalar_Diffusion_Contract`: passed, 1/1, including the negative partial-tile
  actual-RHS guard case.
- Packed periodic U-shear alias regression: passed; west face 4, face 1 1.5,
  terminal alias 1.5, maximum error 0.
- Option-2 U-X source-equation transcription: passed; maximum difference
  `7.77656e-11`, seam error 0. The Fortran routine itself was not compiled.
- `git diff --check`: passed. Focused Graphify source copy matches the
  implementation SHA-256 `811c7f8f7ce206c57bda617efa5ce8e4f3994741c4cb21ccb83414799898923a`
  (296 nodes, 5703 edges).

No WRF model run was performed after this guard change. The prior WRF result
therefore does not include this exact source revision; rerun the planned
incremental relink/smoke before making claims about the guarded build. MPI and
partial-tile option-2 execution remain intentionally unsupported by this path.
