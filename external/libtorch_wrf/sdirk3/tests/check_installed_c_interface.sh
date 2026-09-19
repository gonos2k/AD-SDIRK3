#!/usr/bin/env bash
# Compile as C from the installed header and link against the installed ABI.
# The source tree is deliberately absent from the include and link paths.
set -euo pipefail

install_root="${1:?usage: $0 <install-root> [torch-lib-dir]}"
torch_lib_dir="${2:-${TORCH_LIB_DIR:?set TORCH_LIB_DIR or pass it as the second argument}}"
archive="$install_root/lib/libwrf_sdirk3_libtorch.a"
header="$install_root/include/wrf_sdirk3/wrf_sdirk3_interface.h"
probe_source="$(dirname "$0")/installed_c_interface_smoke.c"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
cc_bin="${CC:-cc}"

test -f "$archive" || { echo "FAIL: installed archive not found: $archive"; exit 1; }
test -f "$header" || { echo "FAIL: installed C ABI header not found: $header"; exit 1; }

"$cc_bin" -std=c11 -Wall -Wextra -Werror \
   -I"$install_root/include" -c "$probe_source" -o "$work/probe.o"

"${CXX:-c++}" "$work/probe.o" "$archive" \
    -L"$torch_lib_dir" -ltorch -ltorch_cpu -lc10 \
    -Wl,-rpath,"$torch_lib_dir" -o "$work/probe"

echo "installed C ABI compile/link smoke OK"
