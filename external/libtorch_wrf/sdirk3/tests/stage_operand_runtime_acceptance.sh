#!/usr/bin/env bash
# =============================================================================
# PR 9F.1 — Stage-operand diagnostic: full-WRF runtime acceptance (fail-closed)
# =============================================================================
# Every gate below is one the PREVIOUS version CLAIMED in comments but did not
# enforce. Each defect was verified against merged main before being fixed:
#
#  1. `common_records` grepped 'SDIRK3_STAGE ' (trailing space) but the real record
#     is SDIRK3_STAGE_DIAG -> the pattern NEVER matched, so "NEWTON/FGMRES/STAGE
#     byte-identical" compared only NEWTON/FGMRES and a differing stage outcome
#     passed silently.
#  2. rc_off / rc_on were printed and never gated.
#  3. The OFF run never unset WRF_SDIRK3_STAGE_OPERAND_DIAG, so an exported parent
#     value made OFF run as ON; and OFF was never checked for zero operand markers.
#  4. "exactly one stage-2/3 record set" was really ">=1 occurrence of the substring
#     target_stage=N" -- which failure markers also contain (same tag), and which
#     counts 4 (stage 2) / 7 (stage 3) success lines per set. n_s2/s2_sets were
#     computed and discarded.
#  5. The failure denylist omitted DEFECT_INCOMPLETE (really emitted), and was
#     bracket-anchored while DEFECT_UNOBSERVED / _NONFINITE / _INCOHERENT are never
#     bracketed -- they surface only via SDIRK3_C_ABI_EXCEPTION.
#  6. "0 extra RHS calls" was a comment; no counter was read or compared.
#  7. No binary/source provenance: a stale wrf.exe passed. ideal.exe's exit code was
#     unchecked and a stale wrfinput_d01 was never removed, so an ideal failure could
#     look like a pass.
#  8. The negative only counted `inventory=OK` lines, ignoring the other success-form
#     markers (HISTORY_DIAG, APPLIED_DELTA).
# 10. (found by running it) The RHS counter was sampled ONLY at ark_sweep_start --
#     before any RHS evaluation -- so both runs emitted `total=0` and the "0 extra RHS
#     evaluations" diff passed trivially, unable to detect an extra call. The gate was
#     green and vacuous. Fixed by adding an ark_sweep_end sample (so the compared
#     interval is the sweep) plus a non-degeneracy gate that fails if no record has
#     total>0. This is the same false-green class as defect 1: two degenerate values
#     compared for equality.
# 10b.(found by Codex reviewing the fix for 10) That first fix was ITSELF vacuous-
#     capable: the counter is cumulative and process-global, so once an earlier sweep
#     advances it, a sweep that evaluates NOTHING still reports total>0 at both ends
#     and "some record has total>0" passes. Only a strict end>start DELTA across one
#     sweep proves the measured interval contains RHS activity. Lesson: a
#     non-degeneracy check must be stated over the measured INTERVAL, not over the
#     absolute value of a cumulative counter.
#  9. np=2 was recorded as proving the same gate as numtiles=2. It does not: the gate
#     is (single_rank && tile_covers_patch); numtiles=2 yields single_rank=1,
#     tile_covers_patch=0 -- only the TILE predicate is exercised.
#
# Usage (from the repo root of a tree whose wrf.exe was built WITH PR 9F.1):
#   bash external/libtorch_wrf/sdirk3/tests/stage_operand_runtime_acceptance.sh
#   bash external/libtorch_wrf/sdirk3/tests/stage_operand_runtime_acceptance.sh --self-test
# =============================================================================
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
RUN="$ROOT/test/em_b_wave"
OUT="${STAGE_OPERAND_ACCEPT_OUT:-$RUN/stage_operand_accept}"
DT="${STAGE_OPERAND_DT:-600}"

fails=0
note() { printf '%s\n' "$*"; }
gate() { if [ "$2" = "1" ]; then note "  PASS: $1"; else note "  FAIL: $1"; fails=$((fails+1)); fi; }
skip() { note "  SKIP: $1"; }

# ---- marker taxonomy (read off the emitter, not guessed) --------------------
# Success-form: the only markers a PASSING record stage may leave behind.
SUCCESS_FORM='SDIRK3_STAGE_(HISTORY_DIAG|HISTORY_SUMMARY|APPLIED_DELTA)'
# Failure family, matched by MARKER NAME rather than a leading bracket, because
# DEFECT_UNOBSERVED / _NONFINITE / _INCOHERENT are returned (not bracket-emitted)
# and reach the log only via SDIRK3_C_ABI_EXCEPTION.
FAIL_FAMILY='SDIRK3_STAGE_OPERAND_[A-Z_]*(FAILED|INCOMPLETE|UNOBSERVED|INCOHERENT|NONFINITE|MISMATCH|UNSUPPORTED)'
closure_re() { printf '\[SDIRK3_STAGE_HISTORY_SUMMARY\].*target_stage=%s .*inventory=OK defect=OK' "$1"; }

# Count matches WITHOUT the `grep -c ... || echo 0` double-emit bug (grep -c prints
# 0 and exits 1 on no match, so `|| echo 0` produced "0\n0" and broke -eq tests).
# The `|| true` matters even though this script sets only -uo pipefail: grep -c exits
# 1 on a zero match, so a caller running us under `set -e` (a CI wrapper, or `bash -e`)
# would terminate on the first legitimately-zero count. grep -c still PRINTS 0, so the
# value is unaffected.
cnt() { local n; n=$(grep -Ec "$1" "$2" 2>/dev/null || true); printf '%s' "${n:-0}"; }

gate_common_records() { grep -E 'SDIRK3_(NEWTON|FGMRES|STAGE)_DIAG\b' "$1" 2>/dev/null || true; }
gate_rhs_records()    { grep -E '^SDIRK3_RHS_CALLS ' "$1" 2>/dev/null || true; }
# PR 9F.3: WHOLE-RUN authority. The sweep table can only see calls made between a
# begin and its end. RHS evaluations also occur before the first sweep opens, after
# the last one closes, and on threads that never open one at all -- so an extra call
# in any of those places leaves the sweep records byte-identical while the run really
# did evaluate the RHS once more. These records close that gap.
gate_run_records()    { grep -E '^SDIRK3_RHS_RUN_TOTAL ' "$1" 2>/dev/null || true; }
run_total_final() {  # <log> -> the closing whole-run total, or empty if absent
  grep -E '^SDIRK3_RHS_RUN_TOTAL phase=end ' "$1" 2>/dev/null | tail -1 |
    sed -n 's/.*total=\([0-9][0-9]*\).*/\1/p'
}
# 1 iff exactly one begin and one end exist and the end is >= the begin. A missing
# end is the signature of an aborted run, and must FAIL rather than be ignored:
# a run that died cannot have proven that it added no RHS evaluations.
run_total_well_formed() { # <log>
  local nb ne fin kind
  nb=$(cnt '^SDIRK3_RHS_RUN_TOTAL phase=begin ' "$1")
  ne=$(cnt '^SDIRK3_RHS_RUN_TOTAL phase=end ' "$1")
  fin=$(run_total_final "$1")
  kind=$(run_exit_kind "$1")
  if [ "$nb" -eq 1 ] && [ "$ne" -eq 1 ] && [ -n "$fin" ] && [ -n "$kind" ]; then
    printf 1
  else
    printf 0
  fi
}
# How the run ended: clean|fatal, or empty if the record is absent or unlabelled.
# Making the closing record reachable on the abort path removed the natural
# distinction between a completed run and one that died early -- both now emit a
# well-formed total. Without this field the harness would compare two numbers from
# two aborted runs and report non-interference PROVEN, when the property was never
# exercised over a complete run. An unlabelled record is treated as absent so a
# pre-`exit=` binary cannot pass this gate silently.
run_exit_kind() { # <log>
  grep -E '^SDIRK3_RHS_RUN_TOTAL phase=end ' "$1" 2>/dev/null | tail -1 |
    sed -n 's/.*exit=\([a-z][a-z]*\).*/\1/p'
}
gate_success_count()  { cnt "$SUCCESS_FORM" "$1"; }
gate_fail_count()     { cnt "$FAIL_FAMILY" "$1"; }
gate_closure_count()  { cnt "$(closure_re "$1")" "$2"; }

# The RHS counter is CUMULATIVE and process-global, so "some record has total>0" is
# NOT evidence that the measured interval contains any RHS activity: on sweep 2 the
# begin sample already inherits sweep 1's total, so a sweep that evaluated nothing
# still reports total>0 at both ends. The property that gives the OFF-vs-ON diff
# teeth is a POSITIVE DELTA across a sweep.
#
# PR 9F.2 P1-1 additionally requires that the record stream be COMPLETE and PAIRED.
# Reducing each sweep to `sweep_seq -> begin_total end_total delta` lets every
# required property be gated on the reduction rather than on ad-hoc greps:
#   - every begin has a matching end (an extra call after the LAST begin, in a run
#     that then exits, is otherwise invisible: it lands in no emitted record)
#   - the emitted delta equals end-begin (the emitter is not free-typing the number)
#   - sweep_seq values are unique
# Output: one line per sweep, "seq begin end delta status", status in
# {ok, missing_end, missing_begin, delta_mismatch}.
rhs_sweep_table() { # <log>
  awk '
    /SDIRK3_RHS_CALLS/ {
      phase=""; seq=-1; total=0; delta=-1; has_delta=0; conc=0
      for (i = 1; i <= NF; i++) {
        if ($i ~ /^phase=/)     phase = substr($i, 7)
        if ($i ~ /^sweep_seq=/) seq   = substr($i, 11) + 0
        if ($i ~ /^total=/)     total = substr($i, 7) + 0
        if ($i ~ /^delta=/)   { delta = substr($i, 7) + 0; has_delta = 1 }
        if ($i == "concurrent=1") conc = 1
      }
      if (seq < 0) next
      if (phase != "begin" && phase != "end") { badphase[seq] = 1; next }
      if (total < 0) badval[seq] = 1
      # A repeated sweep_seq must FAIL rather than silently overwrite: without this,
      # two begins sharing a seq collapse to one table entry and emit two identical
      # "ok" rows, so a duplicated or replayed sweep reads as well-formed.
      # Line ordinals are recorded because the table is keyed on seq, not on arrival
      # order: without them an end record arriving BEFORE its begin is stored and
      # later completed into a well-formed pair. The emitter always writes begin
      # first, so that order is evidence of a corrupted or re-assembled log.
      if (phase == "begin") {
        if (seen_b[seq]) dup_b[seq] = 1
        if (has_delta) baddelta_on_begin[seq] = 1
        b[seq] = total; seen_b[seq] = 1; bline[seq] = NR; order[++n] = seq
      } else if (phase == "end") {
        if (seen_e[seq]) dup_e[seq] = 1
        if (!has_delta) missing_delta_on_end[seq] = 1
        if (has_delta && delta < 0) badval[seq] = 1
        e[seq] = total; seen_e[seq] = 1; d[seq] = delta; hd[seq] = has_delta
        eline[seq] = NR
      }
      if (conc) tainted[seq] = 1
    }
    END {
      for (i = 1; i <= n; i++) {
        s = order[i]
        if (emitted[s]) continue          # one row per seq; duplicates flagged below
        emitted[s] = 1
        if (badphase[s] || badval[s]) { print s, b[s], (seen_e[s] ? e[s] : "-"), "-", "malformed_record"; continue }
        if (baddelta_on_begin[s]) { print s, b[s], (seen_e[s] ? e[s] : "-"), "-", "delta_on_begin"; continue }
        if (seen_e[s] && missing_delta_on_end[s]) { print s, b[s], e[s], "-", "delta_missing_on_end"; continue }
        if (dup_b[s] || dup_e[s]) { print s, b[s], (seen_e[s] ? e[s] : "-"), "-", "duplicate_seq"; continue }
        if (seen_e[s] && eline[s] < bline[s]) { print s, b[s], e[s], "-", "record_order_invalid"; continue }
        if (seen_e[s] && e[s] < b[s]) { print s, b[s], e[s], "-", "total_decreased"; continue }
        # Overlapping sweeps share the global counter, so this delta absorbed RHS
        # calls made by another sweep and is not attributable. Never use as evidence.
        # (No apostrophes in this awk block: it is single-quoted in the shell.)
        if (tainted[s]) { print s, b[s], (seen_e[s] ? e[s] : "-"), "-", "concurrent_sweeps"; continue }
        if (!seen_e[s]) { print s, b[s], "-", "-", "missing_end"; continue }
        st = "ok"
        if (!hd[s] || d[s] != e[s] - b[s]) st = "delta_mismatch"
        print s, b[s], e[s], d[s], st
      }
      for (s in seen_e) if (!seen_b[s]) print s, "-", e[s], "-", "missing_begin"
    }
  ' "$1" 2>/dev/null
}
# 1 iff the table is non-empty, every sweep is status=ok, and some sweep moved.
rhs_sweeps_well_formed() { # <log>
  local t; t="$(rhs_sweep_table "$1")"
  [ -n "$t" ] || { printf 0; return; }
  printf '%s\n' "$t" | awk '{ if ($5 != "ok") bad=1; if ($4+0 > 0) moved=1 }
                            END { print (bad != 1 && moved == 1) ? 1 : 0 }'
}
rhs_positive_delta() { rhs_sweeps_well_formed "$1"; }

# =============================================================================
if [ "${1:-}" = "--self-test" ]; then
  note "== SELF-TEST: prove each gate fires on a bad fixture =="
  TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
  TAG3='dt=6.000000000e+02 step_seq=1 checkpoint_timestep=0 target_stage=3 iter=0'
  TAG2='dt=6.000000000e+02 step_seq=1 checkpoint_timestep=0 target_stage=2 iter=0'
  { echo "SDIRK3_NEWTON_DIAG ts=0 stage=3 iter=0 event=residual"
    echo "SDIRK3_STAGE_DIAG stage=3 outcome=ok"
    echo "[SDIRK3_STAGE_HISTORY_SUMMARY] $TAG2 hist_rel=1e-8 inventory=OK defect=OK"
    echo "[SDIRK3_STAGE_HISTORY_SUMMARY] $TAG3 hist_rel=1e-8 inventory=OK defect=OK"
    echo "[SDIRK3_STAGE_APPLIED_DELTA] $TAG3 src_stage=2 applied_norm=1.0"
    echo "SDIRK3_RHS_CALLS phase=begin sweep_seq=0 total=0"
    echo "SDIRK3_RHS_CALLS phase=end sweep_seq=0 total=42 delta=42"; } > "$TMP/good.log"
  sed 's/outcome=ok/outcome=ABORT/' "$TMP/good.log" > "$TMP/stagediff.log"
  cp "$TMP/good.log" "$TMP/dupclosure.log"
  echo "[SDIRK3_STAGE_HISTORY_SUMMARY] $TAG3 hist_rel=1e-8 inventory=OK defect=OK" >> "$TMP/dupclosure.log"
  cp "$TMP/good.log" "$TMP/incomplete.log"
  echo "[SDIRK3_STAGE_OPERAND_DEFECT_INCOMPLETE] $TAG3 defect=tensor_missing:s2" >> "$TMP/incomplete.log"
  cp "$TMP/good.log" "$TMP/unobserved.log"
  echo "SDIRK3_C_ABI_EXCEPTION: stage_operand_defect_tensor: SDIRK3_STAGE_OPERAND_DEFECT_UNOBSERVED: captured K != returned K (s2)" >> "$TMP/unobserved.log"
  sed 's/total=42/total=43/' "$TMP/good.log" > "$TMP/rhsdiff.log"
  # The historical vacuous case: counter sampled only at sweep start, so every run
  # reports total=0 and the OFF/ON diff passes while proving nothing.
  grep -v 'phase=end' "$TMP/good.log" > "$TMP/rhsvacuous.log"
  # PR 9F.2 P1-1, the false-green the reviewer named: the ON run makes one extra RHS
  # call AFTER the last begin and the run then exits, so no end record ever carries
  # it. begin records are byte-identical OFF vs ON, so a diff-only gate PASSES while
  # an extra call really happened. Only "every begin has a matching end" rejects it.
  { echo "SDIRK3_RHS_CALLS phase=begin sweep_seq=0 total=0"
    echo "SDIRK3_RHS_CALLS phase=end sweep_seq=0 total=42 delta=42"
    echo "SDIRK3_RHS_CALLS phase=begin sweep_seq=1 total=42"; } > "$TMP/rhsdangling.log"
  # The emitter free-typing a delta that disagrees with end-begin.
  { echo "SDIRK3_RHS_CALLS phase=begin sweep_seq=0 total=10"
    echo "SDIRK3_RHS_CALLS phase=end sweep_seq=0 total=42 delta=7"; } > "$TMP/rhsbaddelta.log"
  # A REPEATED sweep_seq (duplicated/replayed sweep). Each pair is individually
  # well-formed, so only a uniqueness check rejects it.
  { echo "SDIRK3_RHS_CALLS phase=begin sweep_seq=0 total=0"
    echo "SDIRK3_RHS_CALLS phase=end sweep_seq=0 total=42 delta=42"
    echo "SDIRK3_RHS_CALLS phase=begin sweep_seq=0 total=42"
    echo "SDIRK3_RHS_CALLS phase=end sweep_seq=0 total=84 delta=42"; } > "$TMP/rhsdupseq.log"
  # Overlapping sweeps (multi-tile OpenMP): each pair is complete and delta-consistent
  # in isolation, but the deltas absorbed each other's calls via the global counter.
  { echo "SDIRK3_RHS_CALLS phase=begin sweep_seq=0 total=0"
    echo "SDIRK3_RHS_CALLS phase=begin sweep_seq=1 total=5 concurrent=1"
    echo "SDIRK3_RHS_CALLS phase=end sweep_seq=1 total=30 delta=25 concurrent=1"
    echo "SDIRK3_RHS_CALLS phase=end sweep_seq=0 total=40 delta=40 concurrent=1"; } > "$TMP/rhsconcurrent.log"
  # The subtler vacuous case (found by Codex on the first fix): the counter is
  # CUMULATIVE, so a sweep that evaluated nothing still reports total>0 at both ends
  # once an earlier sweep has advanced it. A "some record has total>0" check passes
  # here; only an end>start delta rejects it.
  { echo "SDIRK3_RHS_CALLS phase=begin sweep_seq=0 total=7"
    echo "SDIRK3_RHS_CALLS phase=end sweep_seq=0 total=7 delta=0"; } > "$TMP/rhsnomove.log"

  gate "self: good fixture has 0 failure-family markers" \
       "$([ "$(gate_fail_count "$TMP/good.log")" -eq 0 ] && echo 1 || echo 0)"
  gate "self: good fixture has exactly one stage-3 closure" \
       "$([ "$(gate_closure_count 3 "$TMP/good.log")" -eq 1 ] && echo 1 || echo 0)"
  gate "self: STAGE_DIAG difference IS detected (defect 1)" \
       "$(diff -q <(gate_common_records "$TMP/good.log") <(gate_common_records "$TMP/stagediff.log") >/dev/null 2>&1 && echo 0 || echo 1)"
  gate "self: duplicate closure set is rejected (defect 4)" \
       "$([ "$(gate_closure_count 3 "$TMP/dupclosure.log")" -eq 2 ] && echo 1 || echo 0)"
  gate "self: DEFECT_INCOMPLETE is in the denylist (defect 5a)" \
       "$([ "$(gate_fail_count "$TMP/incomplete.log")" -ge 1 ] && echo 1 || echo 0)"
  gate "self: non-bracketed DEFECT_UNOBSERVED via C_ABI_EXCEPTION is caught (defect 5b)" \
       "$([ "$(gate_fail_count "$TMP/unobserved.log")" -ge 1 ] && echo 1 || echo 0)"
  gate "self: RHS-count difference IS detected (defect 6)" \
       "$(diff -q <(gate_rhs_records "$TMP/good.log") <(gate_rhs_records "$TMP/rhsdiff.log") >/dev/null 2>&1 && echo 0 || echo 1)"
  gate "self: success-form counter sees all three marker kinds (defect 8)" \
       "$([ "$(gate_success_count "$TMP/good.log")" -ge 3 ] && echo 1 || echo 0)"
  gate "self: good fixture HAS a positive in-sweep RHS delta (defect 10)" \
       "$([ "$(rhs_positive_delta "$TMP/good.log")" -eq 1 ] && echo 1 || echo 0)"
  gate "self: vacuous RHS counter (start-only, all total=0) IS rejected (defect 10)" \
       "$([ "$(rhs_positive_delta "$TMP/rhsvacuous.log")" -eq 0 ] && echo 1 || echo 0)"
  gate "self: CUMULATIVE no-movement sweep (start=end=7) IS rejected (defect 10b)" \
       "$([ "$(rhs_positive_delta "$TMP/rhsnomove.log")" -eq 0 ] && echo 1 || echo 0)"
  gate "self: DANGLING begin (extra call after last begin, no end) IS rejected (P1-1)" \
       "$([ "$(rhs_sweeps_well_formed "$TMP/rhsdangling.log")" -eq 0 ] && echo 1 || echo 0)"
  gate "self: emitted delta disagreeing with end-begin IS rejected (P1-1)" \
       "$([ "$(rhs_sweeps_well_formed "$TMP/rhsbaddelta.log")" -eq 0 ] && echo 1 || echo 0)"
  gate "self: DUPLICATE sweep_seq IS rejected (P1-1 uniqueness, found by Codex)" \
       "$([ "$(rhs_sweeps_well_formed "$TMP/rhsdupseq.log")" -eq 0 ] && echo 1 || echo 0)"
  gate "self: CONCURRENT (overlapping) sweeps ARE rejected -- delta not attributable" \
       "$([ "$(rhs_sweeps_well_formed "$TMP/rhsconcurrent.log")" -eq 0 ] && echo 1 || echo 0)"
  # PR 9F.3: an end record arriving BEFORE its begin. The table is keyed on seq, not
  # arrival order, so without line ordinals this completes into a well-formed pair.
  { echo "SDIRK3_RHS_CALLS phase=end sweep_seq=0 total=42 delta=42"
    echo "SDIRK3_RHS_CALLS phase=begin sweep_seq=0 total=0"; } > "$TMP/rhsreorder.log"
  # Structural violations: delta on a begin, and a missing delta on an end.
  { echo "SDIRK3_RHS_CALLS phase=begin sweep_seq=0 total=0 delta=5"
    echo "SDIRK3_RHS_CALLS phase=end sweep_seq=0 total=42 delta=42"; } > "$TMP/rhsdeltabegin.log"
  { echo "SDIRK3_RHS_CALLS phase=begin sweep_seq=0 total=0"
    echo "SDIRK3_RHS_CALLS phase=end sweep_seq=0 total=42"; } > "$TMP/rhsnodelta.log"
  # A cumulative total that went BACKWARDS.
  { echo "SDIRK3_RHS_CALLS phase=begin sweep_seq=0 total=50"
    echo "SDIRK3_RHS_CALLS phase=end sweep_seq=0 total=42 delta=-8"; } > "$TMP/rhsdecrease.log"
  gate "self: end-before-begin record order IS rejected (P2, temporal ordering)" \
       "$([ "$(rhs_sweeps_well_formed "$TMP/rhsreorder.log")" -eq 0 ] && echo 1 || echo 0)"
  gate "self: delta on a BEGIN record IS rejected (structural)" \
       "$([ "$(rhs_sweeps_well_formed "$TMP/rhsdeltabegin.log")" -eq 0 ] && echo 1 || echo 0)"
  gate "self: missing delta on an END record IS rejected (structural)" \
       "$([ "$(rhs_sweeps_well_formed "$TMP/rhsnodelta.log")" -eq 0 ] && echo 1 || echo 0)"
  gate "self: a cumulative total that DECREASED IS rejected" \
       "$([ "$(rhs_sweeps_well_formed "$TMP/rhsdecrease.log")" -eq 0 ] && echo 1 || echo 0)"

  # ---- whole-run total fixtures (PR 9F.3) ----------------------------------
  { echo "SDIRK3_RHS_RUN_TOTAL phase=begin total=0"
    echo "SDIRK3_RHS_RUN_TOTAL phase=end total=42 exit=clean"; } > "$TMP/runok.log"
  { echo "SDIRK3_RHS_RUN_TOTAL phase=begin total=0"
    echo "SDIRK3_RHS_RUN_TOTAL phase=end total=42 exit=fatal"; } > "$TMP/runfatal.log"
  # A pre-`exit=` emitter: well-formed in every other respect, and must NOT pass.
  { echo "SDIRK3_RHS_RUN_TOTAL phase=begin total=0"
    echo "SDIRK3_RHS_RUN_TOTAL phase=end total=42"; } > "$TMP/runnokind.log"
  grep -v 'phase=end' "$TMP/runok.log" > "$TMP/runaborted.log"
  gate "self: a well-formed whole-run total is accepted" \
       "$([ "$(run_total_well_formed "$TMP/runok.log")" -eq 1 ] && echo 1 || echo 0)"
  gate "self: a run with NO closing total (aborted) IS rejected" \
       "$([ "$(run_total_well_formed "$TMP/runaborted.log")" -eq 0 ] && echo 1 || echo 0)"
  gate "self: the closing whole-run total is extracted correctly" \
       "$([ "$(run_total_final "$TMP/runok.log")" = "42" ] && echo 1 || echo 0)"
  gate "self: an UNLABELLED closing total (no exit=) IS rejected" \
       "$([ "$(run_total_well_formed "$TMP/runnokind.log")" -eq 0 ] && echo 1 || echo 0)"
  gate "self: exit kind is read as clean" \
       "$([ "$(run_exit_kind "$TMP/runok.log")" = "clean" ] && echo 1 || echo 0)"
  gate "self: exit kind is read as fatal (an aborted run cannot pose as complete)" \
       "$([ "$(run_exit_kind "$TMP/runfatal.log")" = "fatal" ] && echo 1 || echo 0)"

  # ---- LIVE emitter -> LIVE parser (PR 9F.3) --------------------------------
  # Every fixture above is hand-written, so the shell parser has only ever been
  # proven against text this script authored. If the C++ emitter changed its schema
  # the parser would keep passing. When the contract binary is available, judge its
  # REAL output with the real parser.
  CBIN="${STAGE_OPERAND_CONTRACT_BIN:-$ROOT/external/libtorch_wrf/sdirk3/test_stage_operand_decomposition_contract}"
  if [ -x "$CBIN" ]; then
    WRF_SDIRK3_RHS_COUNT=1 "$CBIN" --child one-sweep > "$TMP/live_one.log" 2>&1 || true
    WRF_SDIRK3_RHS_COUNT=1 "$CBIN" --child nested    > "$TMP/live_nested.log" 2>&1 || true
    gate "self: LIVE C++ one-sweep output is well-formed to the real parser" \
         "$([ "$(rhs_sweeps_well_formed "$TMP/live_one.log")" -eq 1 ] && echo 1 || echo 0)"
    gate "self: LIVE C++ one-sweep run total is well-formed" \
         "$([ "$(run_total_well_formed "$TMP/live_one.log")" -eq 1 ] && echo 1 || echo 0)"
    gate "self: LIVE C++ NESTED output is REJECTED by the real parser" \
         "$([ "$(rhs_sweeps_well_formed "$TMP/live_nested.log")" -eq 0 ] && echo 1 || echo 0)"
  else
    skip "contract binary not built -- live emitter/parser contract UNPROVEN here"
    note "        (set STAGE_OPERAND_CONTRACT_BIN, or build the target, to enable)"
  fi
  gate "self: good fixture has BOTH begin and end phase records (defect 10)" \
       "$([ "$(cnt 'phase=begin' "$TMP/good.log")" -ge 1 ] && \
          [ "$(cnt 'phase=end' "$TMP/good.log")" -ge 1 ] && echo 1 || echo 0)"
  note "============================================================"
  [ "$fails" -eq 0 ] && { note "SELF-TEST: ALL PASS"; exit 0; }
  note "SELF-TEST: $fails FAILURE(S)"; exit 1
fi

# =============================================================================
mkdir -p "$OUT"
cd "$RUN" || { echo "FATAL: no $RUN"; exit 2; }
[ -x ./wrf.exe ]   || { echo "FATAL: wrf.exe not built";   exit 2; }
[ -x ./ideal.exe ] || { echo "FATAL: ideal.exe not built"; exit 2; }

# ---- (7) BINARY / SOURCE PROVENANCE ----------------------------------------
note "== provenance =="
GIT_HEAD="$(git -C "$ROOT" rev-parse HEAD 2>/dev/null || echo UNKNOWN)"
# sha256sum (coreutils) first, shasum (perl) as fallback: the hosted core-linux job
# may have only one of the two.
sha256_of() { { sha256sum "$1" || shasum -a 256 "$1"; } 2>/dev/null | cut -d' ' -f1; }
WRF_SHA="$(sha256_of ./wrf.exe)"
NML_SHA="$(sha256_of ./namelist.input)"
note "  git HEAD      : $GIT_HEAD"
note "  wrf.exe sha256: $WRF_SHA"
note "  namelist sha  : $NML_SHA"
{ echo "git_head=$GIT_HEAD"; echo "wrf_sha256=$WRF_SHA"; echo "namelist_sha256=$NML_SHA"; } > "$OUT/provenance.txt"
STALE_SRC="$(find "$ROOT/external/libtorch_wrf/sdirk3" -type f \( -name '*.cpp' -o -name '*.h' \) \
             -newer ./wrf.exe 2>/dev/null | head -1)"
gate "wrf.exe is newer than every sdirk3 source (binary not stale)" \
     "$([ -z "$STALE_SRC" ] && echo 1 || echo 0)"
[ -n "$STALE_SRC" ] && note "        newer than binary: $STALE_SRC"

# ---- run driver -------------------------------------------------------------
mk_namelist() { # <dst> <numtiles>
  cp namelist.input "$1"
  perl -0pi -e "s/(\btime_step\s*=\s*)\d+/\${1}$DT/" "$1"
  if grep -q 'numtiles' "$1"; then perl -0pi -e "s/(\bnumtiles\s*=\s*)\d+/\${1}$2/" "$1"
  else perl -0pi -e "s/(&domains\s*\n)/\${1} numtiles = $2,\n/" "$1"; fi
}

run_wrf() { # <logfile> <numtiles> <omp> <diag 0|1> [np]
  local log="$1" nt="$2" omp="$3" diag="$4" np="${5:-1}"
  mk_namelist namelist.input.accept "$nt"
  cp namelist.input namelist.input.bak 2>/dev/null || true
  cp namelist.input.accept namelist.input
  : > "$log"
  # WRF (DMPARALLEL) routes per-rank stdout/stderr -- and thus the diagnostic
  # std::cerr lines -- into rsl.out./rsl.error.NNNN, not the pipe below.
  rm -f rsl.error.* rsl.out.*
  # (3) OFF must be FORCED off: an exported parent value would otherwise make the
  # OFF run behave as ON. WRF_SDIRK3_RHS_COUNT is set for BOTH runs so the RHS
  # totals are directly comparable.
  local rc=0
  if [ "$np" -gt 1 ] && command -v mpirun >/dev/null 2>&1; then
    if [ "$diag" = "1" ]; then
      env OMP_NUM_THREADS="$omp" WRF_SDIRK3_STAGE_DIAG=1 WRF_SDIRK3_RHS_COUNT=1 \
          WRF_SDIRK3_STAGE_OPERAND_DIAG=1 mpirun -np "$np" ./wrf.exe >>"$log" 2>&1
    else
      env -u WRF_SDIRK3_STAGE_OPERAND_DIAG OMP_NUM_THREADS="$omp" \
          WRF_SDIRK3_STAGE_DIAG=1 WRF_SDIRK3_RHS_COUNT=1 \
          mpirun -np "$np" ./wrf.exe >>"$log" 2>&1
    fi
    rc=$?
  else
    if [ "$diag" = "1" ]; then
      env OMP_NUM_THREADS="$omp" WRF_SDIRK3_STAGE_DIAG=1 WRF_SDIRK3_RHS_COUNT=1 \
          WRF_SDIRK3_STAGE_OPERAND_DIAG=1 ./wrf.exe >>"$log" 2>&1
    else
      env -u WRF_SDIRK3_STAGE_OPERAND_DIAG OMP_NUM_THREADS="$omp" \
          WRF_SDIRK3_STAGE_DIAG=1 WRF_SDIRK3_RHS_COUNT=1 ./wrf.exe >>"$log" 2>&1
    fi
    rc=$?
  fi
  cat rsl.error.* >> "$log" 2>/dev/null
  cat rsl.out.*   >> "$log" 2>/dev/null
  [ -f namelist.input.bak ] && mv namelist.input.bak namelist.input
  return $rc
}

# ---- (7) ideal.exe: fresh input, checked exit -------------------------------
note "== ideal.exe (fresh wrfinput) =="
rm -f wrfinput_d01 rsl.error.* rsl.out.*
OMP_NUM_THREADS=1 ./ideal.exe > "$OUT/ideal.log" 2>&1; ideal_rc=$?
cat rsl.error.* >> "$OUT/ideal.log" 2>/dev/null
gate "ideal.exe exited 0" "$([ "$ideal_rc" -eq 0 ] && echo 1 || echo 0)"
gate "ideal.exe produced a FRESH wrfinput_d01" "$([ -f wrfinput_d01 ] && echo 1 || echo 0)"

# =============================================================================
note "== POSITIVE: dt=$DT single-rank single-tile OMP=2, OFF vs ON =="
run_wrf "$OUT/pos_off.log" 1 2 0; rc_off=$?
run_wrf "$OUT/pos_on.log"  1 2 1; rc_on=$?
note "  exit OFF=$rc_off ON=$rc_on"

# (2) the exit CONTRACT must match. dt=600 is expected to end nonzero; what must
# hold is that enabling the diagnostic does not change the outcome.
gate "OFF and ON exit identically (rc_off == rc_on)" \
     "$([ "$rc_off" -eq "$rc_on" ] && echo 1 || echo 0)"

# (1) STAGE_DIAG now genuinely participates.
gate_common_records "$OUT/pos_off.log" > "$OUT/pos_off.common"
gate_common_records "$OUT/pos_on.log"  > "$OUT/pos_on.common"
gate "common NEWTON/FGMRES/STAGE_DIAG records byte-identical OFF vs ON" \
     "$(diff -q "$OUT/pos_off.common" "$OUT/pos_on.common" >/dev/null 2>&1 && echo 1 || echo 0)"
diff "$OUT/pos_off.common" "$OUT/pos_on.common" > "$OUT/pos_common.diff" 2>&1
gate "STAGE_DIAG records are actually PRESENT (the pattern really matches)" \
     "$([ "$(cnt 'SDIRK3_STAGE_DIAG' "$OUT/pos_on.log")" -ge 1 ] && echo 1 || echo 0)"

# (6) 0 extra RHS evaluations, proven by the emitted counter sequence.
gate_rhs_records "$OUT/pos_off.log" > "$OUT/pos_off.rhs"
gate_rhs_records "$OUT/pos_on.log"  > "$OUT/pos_on.rhs"
gate "RHS-call records present (counter actually emitted)" \
     "$([ -s "$OUT/pos_on.rhs" ] && echo 1 || echo 0)"
# NON-DEGENERACY: the equality below is only evidence if the counter actually moved.
# The first version of this gate sampled the counter ONLY at ark_sweep_start -- i.e.
# before any RHS evaluation -- so both runs reported total=0 and the diff passed
# trivially while being incapable of detecting an extra call. A sweep_end sample now
# exists; this gate fails if the compared interval is empty again.
# PR 9F.2 P1-1: every sweep begin/end paired, emitted delta == end-begin, and at
# least one sweep actually moved. A dangling final begin (the extra-call-then-exit
# false-green) fails here even though the begin records still diff clean.
gate "RHS sweeps well-formed ON (every begin paired, delta consistent, some movement)" \
     "$(rhs_sweeps_well_formed "$OUT/pos_on.log")"
gate "RHS sweeps well-formed OFF (same completeness contract)" \
     "$(rhs_sweeps_well_formed "$OUT/pos_off.log")"
rhs_sweep_table "$OUT/pos_off.log" > "$OUT/pos_off.sweeps"
rhs_sweep_table "$OUT/pos_on.log"  > "$OUT/pos_on.sweeps"
gate "per-sweep RHS deltas identical OFF vs ON (sweep-resolved, not just cumulative)" \
     "$(diff -q "$OUT/pos_off.sweeps" "$OUT/pos_on.sweeps" >/dev/null 2>&1 && echo 1 || echo 0)"
gate "same NUMBER of sweeps OFF vs ON" \
     "$([ "$(wc -l < "$OUT/pos_off.sweeps")" -eq "$(wc -l < "$OUT/pos_on.sweeps")" ] && echo 1 || echo 0)"
gate "0 extra RHS evaluations: RHS-call sequence identical OFF vs ON" \
     "$(diff -q "$OUT/pos_off.rhs" "$OUT/pos_on.rhs" >/dev/null 2>&1 && echo 1 || echo 0)"

# PR 9F.3: the authoritative statement of "0 extra RHS evaluations". Everything above
# compares SWEEP-LOCAL intervals; this compares the WHOLE RUN, so an extra call made
# outside every sweep -- before the first, after the last, or on a thread that opens
# none -- is caught. Both runs must also have closed their run total: an aborted run
# leaves no end record and must not be read as having proven anything.
gate_run_records "$OUT/pos_off.log" > "$OUT/pos_off.runtotal"
gate_run_records "$OUT/pos_on.log"  > "$OUT/pos_on.runtotal"
gate "whole-run total is well-formed OFF (one begin, one end)" \
     "$(run_total_well_formed "$OUT/pos_off.log")"
gate "whole-run total is well-formed ON (one begin, one end)" \
     "$(run_total_well_formed "$OUT/pos_on.log")"
RT_OFF="$(run_total_final "$OUT/pos_off.log")"
RT_ON="$(run_total_final "$OUT/pos_on.log")"
note "  whole-run RHS totals: OFF=${RT_OFF:-<none>} ON=${RT_ON:-<none>}"
EK_OFF="$(run_exit_kind "$OUT/pos_off.log")"
EK_ON="$(run_exit_kind "$OUT/pos_on.log")"
note "  run exit kinds: OFF=${EK_OFF:-<none>} ON=${EK_ON:-<none>}"
# Comparing totals across two DIFFERENT exit kinds is not a comparison: one run got
# further than the other, so equal totals would be coincidence and unequal totals
# would not indicate interference.
gate "OFF and ON ended the SAME way (both clean, or both fatal)" \
     "$([ -n "$EK_OFF" ] && [ "$EK_OFF" = "$EK_ON" ] && echo 1 || echo 0)"
gate "0 extra RHS evaluations OVER THE WHOLE RUN (final totals equal, non-empty)" \
     "$([ -n "$RT_OFF" ] && [ -n "$RT_ON" ] && [ "$RT_OFF" = "$RT_ON" ] && echo 1 || echo 0)"
# dt=600 is EXPECTED to end fatal, so this is reported rather than gated -- but it is
# reported, because "both runs aborted identically" is a weaker statement than "a
# complete run added no RHS evaluations" and the reader must be able to tell which
# one this evidence supports.
if [ "$EK_ON" = "fatal" ]; then
  note "  NOTE: both runs ended in a CONTROLLED FATAL. The whole-run equality above"
  note "        therefore proves non-interference UP TO the abort point, not over a"
  note "        completed integration."
fi
gate "whole-run total is NON-DEGENERATE (the run really evaluated the RHS)" \
     "$([ -n "$RT_ON" ] && [ "$RT_ON" -gt 0 ] && echo 1 || echo 0)"

# (3) OFF must emit NO operand evidence; ON must emit some.
off_succ=$(gate_success_count "$OUT/pos_off.log")
on_succ=$(gate_success_count "$OUT/pos_on.log")
note "  success-form markers: OFF=$off_succ ON=$on_succ"
gate "OFF run emits ZERO operand success markers (diag really off)" \
     "$([ "$off_succ" -eq 0 ] && echo 1 || echo 0)"
gate "ON run emits operand success markers" "$([ "$on_succ" -gt 0 ] && echo 1 || echo 0)"

# (4) EXACTLY one closure record per record stage.
s2=$(gate_closure_count 2 "$OUT/pos_on.log")
s3=$(gate_closure_count 3 "$OUT/pos_on.log")
note "  closure summaries: target_stage=2 -> $s2   target_stage=3 -> $s3"
gate "exactly ONE stage-2 closure record set" "$([ "$s2" -eq 1 ] && echo 1 || echo 0)"
gate "exactly ONE stage-3 closure record set" "$([ "$s3" -eq 1 ] && echo 1 || echo 0)"

# (5) the WHOLE failure family, bracketed or not.
on_fail=$(gate_fail_count "$OUT/pos_on.log")
gate "no operand failure-family marker in the positive run" \
     "$([ "$on_fail" -eq 0 ] && echo 1 || echo 0)"
[ "$on_fail" -ne 0 ] && grep -E "$FAIL_FAMILY" "$OUT/pos_on.log" | head -10 > "$OUT/pos_on.failmarkers"

# PR 9F.1: the published numbers must be the TENSOR authority.
gate "summary publishes tensor-derived defect (defect_eval=tensor)" \
     "$([ "$(cnt 'defect_eval=tensor' "$OUT/pos_on.log")" -ge 1 ] && echo 1 || echo 0)"
gate "summary states the pre/post-postprocess evaluation point" \
     "$([ "$(cnt 'defect_eval_point=pre_postprocess' "$OUT/pos_on.log")" -ge 1 ] && echo 1 || echo 0)"

# =============================================================================
note "== NEGATIVE: numtiles=2 controlled-fatal, ON, OMP {1,2,4} =="
note "   (exercises tile_covers_patch=false; single_rank stays TRUE in these runs)"
for omp in 1 2 4; do
  log="$OUT/neg_nt2_omp${omp}.log"
  run_wrf "$log" 2 "$omp" 1; rc=$?
  topo=$(cnt 'SDIRK3_STAGE_OPERAND_DIAG_UNSUPPORTED_TOPOLOGY' "$log")
  ctrl=$(cnt 'controlled.fatal|abort_c_abi_exception|stage_operand_diag_topology|SDIRK3_C_ABI_EXCEPTION' "$log")
  succ=$(gate_success_count "$log")            # (8) ALL success-form markers
  scomplete=$(cnt 'SUCCESS COMPLETE' "$log")
  uncaught=$(cnt 'terminate called|libc\+\+abi|what\(\):' "$log")
  note "  OMP=$omp rc=$rc topo=$topo ctrl=$ctrl success_markers=$succ SUCCESS=$scomplete uncaught=$uncaught"
  gate "OMP=$omp: nonzero exit"                            "$([ "$rc" -ne 0 ] && echo 1 || echo 0)"
  gate "OMP=$omp: UNSUPPORTED_TOPOLOGY marker"             "$([ "$topo" -ge 1 ] && echo 1 || echo 0)"
  gate "OMP=$omp: controlled-fatal marker"                 "$([ "$ctrl" -ge 1 ] && echo 1 || echo 0)"
  gate "OMP=$omp: ZERO success-form markers (all 3 kinds)"  "$([ "$succ" -eq 0 ] && echo 1 || echo 0)"
  gate "OMP=$omp: 0 SUCCESS COMPLETE"                      "$([ "$scomplete" -eq 0 ] && echo 1 || echo 0)"
  gate "OMP=$omp: 0 uncaught-exception signature"          "$([ "$uncaught" -eq 0 ] && echo 1 || echo 0)"
done

# =============================================================================
# (9) np=2 exercises a DIFFERENT predicate. The gate is
# (single_rank && tile_covers_patch). numtiles=2 gives single_rank=1,
# tile_covers_patch=0, so only the TILE predicate is exercised; only np>1 can drive
# single_rank=0. If the 2-rank launch never reaches the solve, that predicate stays
# UNPROVEN and must NOT be recorded as covered by the numtiles negatives.
if command -v mpirun >/dev/null 2>&1; then
  log="$OUT/neg_np2.log"; run_wrf "$log" 1 1 1 2; rc=$?
  topo=$(cnt 'UNSUPPORTED_TOPOLOGY' "$log")
  sr0=$(cnt 'single_rank=0' "$log")
  note "  np=2 rc=$rc topo=$topo single_rank=0_seen=$sr0"
  if [ "$rc" -ne 0 ] && [ "$topo" -ge 1 ] && [ "$sr0" -ge 1 ]; then
    gate "np=2: nonzero exit AND UNSUPPORTED_TOPOLOGY with single_rank=0" 1
  else
    skip "np=2 did not reach the solve on this node -- the single_rank predicate is"
    note "        UNPROVEN by runtime evidence. numtiles=2 does NOT cover it: those"
    note "        runs report single_rank=1 tile_covers_patch=0."
  fi
else
  skip "mpirun unavailable -- single_rank predicate UNPROVEN (numtiles=2 does not cover it)"
fi

# =============================================================================
note "============================================================"
if [ "$fails" -eq 0 ]; then
  note "STAGE-OPERAND RUNTIME ACCEPTANCE: ALL PASS"; exit 0
else
  note "STAGE-OPERAND RUNTIME ACCEPTANCE: $fails FAILURE(S) (see $OUT/*.log)"; exit 1
fi
