#!/bin/zsh
# PR 9C.3 (P0-EH) commit 1: the exact WRF mixed-toolchain exception-link
# discriminator. Builds ONE set of objects, links them under two final-link
# shapes, and runs the seven probes one process at a time:
#
#   A  the current configure.wrf shape: mpif90 (gfortran driver) final link
#   B  the candidate shape: clang++ final link + the Fortran/MPI runtime
#      libraries extracted from `mpif90 -show`, made explicit
#
# Under the currently-measured macOS toolchain, shape A FAILS every throwing
# probe (uncaught -> terminate; this is the standing reproduction) and shape
# B must pass 7/7. Artifacts (link commands, compiler versions, otool -L)
# are saved under the output directory.
#
# Usage: run_eh_link_contract.sh <output-dir> [torch-root]
set -u
OUT="${1:?usage: run_eh_link_contract.sh <output-dir> [torch-root]}"
TORCH="${2:-/opt/homebrew/opt/pytorch/libexec/lib/python3.14/site-packages/torch}"
HERE="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$OUT"
cd "$OUT"

MPI_SHOW=$(mpif90 -show)
MPI_LIBS=$(echo "$MPI_SHOW" | tr ' ' '\n' | grep -E '^-(L|l)' | tr '\n' ' ')
GFLIB=$(cd "$(dirname "$(gfortran -print-file-name=libgfortran.dylib)")" && pwd)

{
  echo "== toolchain =="
  gfortran --version | head -1
  mpif90 --version | head -1
  g++ --version | head -1
  echo "== mpif90 -show =="
  echo "$MPI_SHOW"
  echo "== gfortran runtime dir =="
  echo "$GFLIB"
} > toolchain.txt

# ---- one object set for both shapes
g++ -std=c++17 -O1 -c "$HERE/eh_probes.cpp"     -o eh_probes.o
g++ -std=c++17 -O1 -c "$HERE/eh_probe_tu2.cpp"  -o eh_probe_tu2.o
g++ -std=c++17 -O1 -c "$HERE/eh_probe_arch.cpp" -o eh_probe_arch.o
ar rcs libeh_arch.a eh_probe_arch.o
gfortran -c "$HERE/eh_fmain.f90" -o eh_fmain.o
g++ -std=c++17 -O1 -c "$HERE/eh_probe_torch.cpp" -o eh_probe_torch.o \
    -I"$TORCH/include" -I"$TORCH/include/torch/csrc/api/include" \
    -D_GLIBCXX_USE_CXX11_ABI=0
gfortran -c "$HERE/eh_fmain_torch.f90" -o eh_fmain_torch.o

# ---- shape A: the current WRF final link (mpif90 driver, -lc++ as
#      configure.wrf CPLUSPLUSLIB does)
A_CMD=(mpif90 eh_fmain.o eh_probes.o eh_probe_tu2.o libeh_arch.a -lc++ -o eh_shape_a)
echo "${A_CMD[@]}" > link_a.cmd
"${A_CMD[@]}" >> link_a.cmd 2>&1
AT_CMD=(mpif90 eh_fmain_torch.o eh_probe_torch.o
        -L"$TORCH/lib" -ltorch -ltorch_cpu -lc10 -Wl,-rpath,"$TORCH/lib"
        -lc++ -o eh_shape_a_torch)
echo "${AT_CMD[@]}" > link_a_torch.cmd
"${AT_CMD[@]}" >> link_a_torch.cmd 2>&1

# ---- shape B: clang++ final link + explicit Fortran/MPI runtimes
B_CMD=(g++ eh_fmain.o eh_probes.o eh_probe_tu2.o libeh_arch.a
       -L"$GFLIB" -lgfortran -lquadmath ${=MPI_LIBS}
       -Wl,-rpath,"$GFLIB" -o eh_shape_b)
echo "${B_CMD[@]}" > link_b.cmd
"${B_CMD[@]}" >> link_b.cmd 2>&1
BT_CMD=(g++ eh_fmain_torch.o eh_probe_torch.o
        -L"$TORCH/lib" -ltorch -ltorch_cpu -lc10 -Wl,-rpath,"$TORCH/lib"
        -L"$GFLIB" -lgfortran -lquadmath ${=MPI_LIBS}
        -Wl,-rpath,"$GFLIB" -o eh_shape_b_torch)
echo "${BT_CMD[@]}" > link_b_torch.cmd
"${BT_CMD[@]}" >> link_b_torch.cmd 2>&1

for exe in eh_shape_a eh_shape_a_torch eh_shape_b eh_shape_b_torch; do
  [ -x "$exe" ] && otool -L "$exe" > "otool_$exe.txt"
done

# ---- run: one probe per process; per-shape verdicts
run_shape() {  # $1 exe-base  $2 label
  local exe="$1" label="$2"
  local pass=0 fail=0
  for p in 1 2 3 4 5 7; do
    ./"$exe" $p > "run_${label}_p$p.out" 2>&1
    local rc=$?
    if [ $rc -eq 0 ] && grep -q "EH_PROBE_PASS" "run_${label}_p$p.out"; then
      echo "  [$label probe $p] PASS"
      pass=$((pass+1))
    else
      local sig=""
      grep -q "libc++abi: terminating\|terminate called\|uncaught exception" \
          "run_${label}_p$p.out" && sig=" (uncaught-terminate)"
      echo "  [$label probe $p] FAIL exit=$rc$sig"
      fail=$((fail+1))
    fi
  done
  ./"${exe}_torch" > "run_${label}_p6.out" 2>&1
  local rc=$?
  if [ $rc -eq 0 ] && grep -q "EH_PROBE_PASS" "run_${label}_p6.out"; then
    echo "  [$label probe 6-torch] PASS"; pass=$((pass+1))
  else
    echo "  [$label probe 6-torch] FAIL exit=$rc"; fail=$((fail+1))
  fi
  echo "$label: $pass/7 pass, $fail/7 fail"
  eval "${label}_fail=$fail"
}

echo "=== shape A (current mpif90 final link) ==="
run_shape eh_shape_a A
echo "=== shape B (clang++ final link + explicit runtimes) ==="
run_shape eh_shape_b B

# Contract: B must be 7/7. A's failures are the standing reproduction of
# the measured breakage (report, don't gate — a future toolchain may fix A).
echo "=== VERDICT ==="
if [ "${B_fail}" -eq 0 ]; then
  echo "shape B: 7/7 PASS (contract holds)"
  if [ "${A_fail}" -gt 0 ]; then
    echo "shape A: ${A_fail}/7 FAIL — the measured mixed-toolchain breakage reproduces"
  else
    echo "shape A: 7/7 PASS — the historical breakage no longer reproduces on this toolchain"
  fi
  exit 0
else
  echo "shape B: ${B_fail}/7 FAIL — candidate link does NOT restore EH; contract FAILED"
  exit 1
fi
