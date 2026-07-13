#!/usr/bin/env bash
# run_negative_contracts.sh (PR 6): build-contract negative matrix.
# All mutations happen in a DISPOSABLE COPY under $RUNNER_TEMP (or mktemp);
# every case asserts the EXPECTED MARKER, not just a nonzero exit — a build
# that died for an unrelated reason (bad torch path, missing compiler) is NOT
# a passing negative.
#
# Env: TORCH_ROOT, TORCH_CMAKE_PREFIX, TORCH_ABI (from setup_torch_env.sh)
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
WORK="${RUNNER_TEMP:-$(mktemp -d)}/sdirk3-neg"
SRC="$WORK/src"
fail=0

expect_fail() {
  # expect_fail <label> <marker-regex> <logfile> -- <cmd...>
  local label="$1" marker="$2" log="$3"; shift 3
  [ "$1" = "--" ] && shift
  if "$@" > "$log" 2>&1; then
    echo "FAIL [$label]: command unexpectedly SUCCEEDED"; fail=1; return
  fi
  if ! grep -qE "$marker" "$log"; then
    echo "FAIL [$label]: expected marker '$marker' not found; got:"; tail -5 "$log"; fail=1; return
  fi
  echo "PASS [$label]"
}

fresh_copy() {
  rm -rf "$SRC"; mkdir -p "$SRC"
  cp -R "$REPO_ROOT/external/libtorch_wrf/sdirk3/." "$SRC/"
  # start from a clean object state in the copy
  ( cd "$SRC" && rm -f ./*.o ./*.d ./libwrf_sdirk3_libtorch.a ./libwrf_sdirk3_libtorch.a.tmp )
}

MAKE_ARGS=(CXX="${CXX:-g++}" LIBTORCH_ROOT="$TORCH_ROOT" LIBTORCH_ABI="$TORCH_ABI")
cmake_configure() {  # $1 = source dir, $2 = build dir (output captured by caller)
  cmake -S "$1" -B "$2" -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="$TORCH_CMAKE_PREFIX"
}

M=wrf_sdirk3_core_sources.txt
mkdir -p "$WORK"

# ---------- manifest parser negatives (Make + CMake each) ----------
# spec format: label @@ marker-regex @@ mutation   (@@ never appears in any field,
# so markers may contain regex alternation and mutations may contain shell ||)
declare -a CASES=(
  "missing-manifest @@ missing @@ rm -f $M"
  "empty-manifest @@ is empty @@ sed -n '/^#/p' $M > $M.new && mv $M.new $M"
  "duplicate-source @@ duplicate @@ printf 'wrf_sdirk3_globals.cpp\n' >> $M"
  "nonexistent-source @@ nonexistent @@ printf 'no_such_file.cpp\n' >> $M"
  "non-cpp-entry @@ non-.cpp|not a .cpp @@ printf 'wrf_sdirk3_config.h\n' >> $M"
  "parent-path @@ forbidden|absolute @@ printf '../evil.cpp\n' >> $M"
  "dormant-dir @@ forbidden|dormant @@ printf 'tests/evil.cpp\n' >> $M"
)
for spec in "${CASES[@]}"; do
  label="${spec%% @@ *}"; rest="${spec#* @@ }"
  marker="${rest%% @@ *}"; mutation="${rest#* @@ }"
  # Make side
  fresh_copy
  ( cd "$SRC" && eval "$mutation" )
  expect_fail "make-$label" "$marker" "$WORK/make-$label.log" -- \
    make -C "$SRC" -n all "${MAKE_ARGS[@]}"
  # CMake side (configure-time)
  expect_fail "cmake-$label" "$marker" "$WORK/cmake-$label.log" -- \
    cmake_configure "$SRC" "$WORK/b-$label"
done

# non-ASCII / semicolon comment line must not shred into fake entries:
# a semicolon-bearing NON-comment line must be rejected as not-a-.cpp/invalid.
fresh_copy
( cd "$SRC" && printf 'foo.cpp;bar.cpp\n' >> $M )
expect_fail "cmake-semicolon-line" "not a .cpp|nonexistent|duplicate" "$WORK/cmake-semi.log" -- \
  cmake_configure "$SRC" "$WORK/b-semi"

# ---------- archive exact-set: stale member injected as a REAL object ----------
fresh_copy
( cd "$SRC" \
  && make -j 2 "${MAKE_ARGS[@]}" > "$WORK/pos-build.log" 2>&1 \
  && printf 'namespace { int neg_stale_anchor = 0; }\nint* neg_stale() { return &::neg_stale_anchor; }\n' > stale_member.cpp \
  && "${CXX:-g++}" -std=c++17 -c stale_member.cpp -o stale_member.o \
  && ar r libwrf_sdirk3_libtorch.a stale_member.o \
  && ar t libwrf_sdirk3_libtorch.a | grep -q '^stale_member.o$' \
  && sleep 1.2 && touch wrf_sdirk3_globals.cpp \
  && make -j 2 "${MAKE_ARGS[@]}" > "$WORK/stale-rebuild.log" 2>&1 )
if ar t "$SRC/libwrf_sdirk3_libtorch.a" | grep -q '^stale_member.o$'; then
  echo "FAIL [stale-member]: injected member survived the rebuild"; fail=1
else
  echo "PASS [stale-member]: atomic reconstruction dropped the injected member"
fi
( cd "$SRC" && make check-core-archive "${MAKE_ARGS[@]}" > "$WORK/stale-check.log" 2>&1 ) \
  && echo "PASS [stale-member-check]" || { echo "FAIL [stale-member-check]"; fail=1; }

# ---------- source removal without clean ----------
fresh_copy
( cd "$SRC" \
  && printf 'namespace { int neg_tmp_anchor = 0; }\nint* neg_tmp() { return &::neg_tmp_anchor; }\n' > wrf_sdirk3_neg_tmp.cpp \
  && printf 'wrf_sdirk3_neg_tmp.cpp\n' >> $M \
  && make -j 2 "${MAKE_ARGS[@]}" > "$WORK/tmp-add.log" 2>&1 \
  && ar t libwrf_sdirk3_libtorch.a | grep -q neg_tmp \
  && sleep 1.2 \
  && grep -v neg_tmp $M > $M.new && mv $M.new $M \
  && make -j 2 "${MAKE_ARGS[@]}" > "$WORK/tmp-del.log" 2>&1 )
if ar t "$SRC/libwrf_sdirk3_libtorch.a" | grep -q neg_tmp; then
  echo "FAIL [removed-source]: retired member survived dirty rebuild"; fail=1
else
  echo "PASS [removed-source]: retired member gone without clean"
fi

# ---------- compile-fail atomicity ----------
fresh_copy
( cd "$SRC" && make -j 2 "${MAKE_ARGS[@]}" > "$WORK/atom-pos.log" 2>&1 )
sha_before="$(shasum -a 256 "$SRC/libwrf_sdirk3_libtorch.a" 2>/dev/null || sha256sum "$SRC/libwrf_sdirk3_libtorch.a")"
( cd "$SRC" && sleep 1.2 && printf '\n#error pr6 deliberate\n' >> wrf_sdirk3_globals.cpp )
expect_fail "compile-fail-make" "pr6 deliberate" "$WORK/atom-make.log" -- \
  make -C "$SRC" -j 2 all "${MAKE_ARGS[@]}"
sha_after="$(shasum -a 256 "$SRC/libwrf_sdirk3_libtorch.a" 2>/dev/null || sha256sum "$SRC/libwrf_sdirk3_libtorch.a")"
if [ "${sha_before%% *}" != "${sha_after%% *}" ]; then
  echo "FAIL [atomicity]: archive changed after failed build"; fail=1
else
  echo "PASS [atomicity]: archive SHA-256 unchanged after failed build"
fi
[ -e "$SRC/libwrf_sdirk3_libtorch.a.tmp" ] \
  && { echo "FAIL [atomicity-tmp]: .tmp residue"; fail=1; } \
  || echo "PASS [atomicity-tmp]: no .tmp residue"
cmake_configure "$SRC" "$WORK/b-atom" || true   # configure may pass; the BUILD must fail
expect_fail "compile-fail-cmake" "pr6 deliberate" "$WORK/atom-cmake.log" -- \
  cmake --build "$WORK/b-atom" --parallel 2

if [ "$fail" -eq 0 ]; then echo "ALL negative build contracts PASSED"; else echo "NEGATIVE CONTRACT FAILURES"; fi
exit "$fail"
