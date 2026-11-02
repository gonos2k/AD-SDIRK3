# Newton used-direction and returned-state residual contracts

R1: the implemented matvec residual for the postprocessed direction now drives
Newton quality and entry exemption. Raw GMRES result, diagnostic statistics and
warm-start direction/quality stay paired and are not relabeled. The exact trust
prediction already used the residual vector. Direct U cannot replace the
NanRetryExhausted zero safety direction. These checks are not a proof that the
implemented JVP equals the full nonlinear residual derivative.

R3: the common failure exit retains an observed base/accepted-trial K/R pair,
reusing its masked fixed-metric RMS only if its state equals returned K. No RHS
replay is introduced. Current updates are functional. Numerical equality treats
signed zeros as equal; arbitrary opaque/sign-sensitive callbacks and future
in-place writes are not universally covered. Unmatched/zero-budget receipts
remain NaN/measured=false; legacy continuation effects and separate early
returns are open checklist R4/R5 items.

The existing production Trust contract passed101/101 after adding Direct-U,
S=4I, actual multi-tile mask and warm-entry tests. An initial100-vs96 inventory
failure is preserved, then100/100 preceded the final halo case101/101. The
standalone returned-residual test covers adaptive off/on and independently
computes0.215316 at returned K instead of initial0.507358. These small numerical
fixtures are correctness tests, not practical timestep/performance references.

One standard100km/40×80×64/h600 native run completed under the archived A9/P0
research-feature build (not a default-native certification). All3 stages met
1e-9, wrapper calls3/0, finite and positive saved-state checks passed, and174
endpoint arrays matched old A9 bytes. Archived RK3 field distances are unchanged.
The original runner remains pending_numerical_comparisons; separate pinned
native_validation is PASSED. No RK3, long-run or performance acceptance occurred.

Native core compilation used ef81e4f7. Subsequent e0fed059 changed only the test
fixture; core source and header hashes match. Thirty baseline object members
were verified and one Newton TU rebuilt in31-object archive; __.SYMDEF is a
separate macOS archive index. Public receipts contain exact arguments and source
hashes. Test-only source updates are independently pinned, not misreported as
core recompilation. CI default paths and this research-flag native path are
separate evidence scopes.

Scripts retain their executed filenames and absolute roots
`/private/tmp/newton-residual-consistency-20261102` and
`/Users/yhlee/AD-SDIRK3-review-evidence/newton-residual-consistency-20261102`.
Reproduction additionally needs the recorded prior objects, Fortran/MPI/SDK27.2,
compiler/netCDF/LibTorch, raw inputs/control/RK3 archives. This public folder
alone is not a full native replay bundle. validate_native.py is saved-output
analysis, not a model launcher; the native script is single-attempt guarded.

Large duplicate files were SHA-verified and hardlinked to already-readonly
archives. Unique code, logs and outcomes remain. No physical APFS-savings or
all-configuration/native-convergence claim is made. Follow the linked project
convergence checklist; R2/R4/R5 and O/E/T/A/F remain open as stated.
