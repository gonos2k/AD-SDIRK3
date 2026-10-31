# Bounded native pressure P0 numerical evidence

The changed-source candidate completed one h600 step. Only the first FGMRES used P0.
Its 21 Arnoldi iterations compare with29 in the preserved baseline; whole-step logged JVP counts
were231 versus240. Original saved native residual and the six-owned-field screen passed.
This is not a replayable full operator fixture or independently reevaluated residual.

The original timing block failed the1-second clock gate. The first numerical candidate failed
setup; a source-level boundary-contract fix was tested in a separately registered new attempt.
All original outcomes remain unchanged. `cost_eligible=false`; no speedup, Gate B or forecast
adoption is authorized. See the manifest, selected raw records and timestamped progress report.

`w_yz.png` compares actual archived RK3, baseline ARK and one-first-FGMRES P0 at10min,
with each run's native heights and shared column color scales. It does not validate late noise.
The public CSV is LF-normalized; the manifest retains its original CRLF SHA256.
Full native fields, executable/build archives and full raw logs remain in local evidence paths.

`geometry_compare.py` is the exact one-off plot producer, retaining the pinned local input paths.
It reuses the existing benchmark helper; it is not a portable general runner.
