#!/usr/bin/env bash
# setup_torch_env.sh (PR 6): install the PINNED CPU torch and derive all
# torch locations from the RUNTIME (never hard-coded paths), exporting them
# via $GITHUB_ENV. Prints the full toolchain fingerprint.
set -euo pipefail

python3 -m pip install --upgrade pip >/dev/null
python3 -m pip install \
    --index-url https://download.pytorch.org/whl/cpu \
    -r "$(dirname "$0")/requirements-core.txt"

TORCH_ROOT="$(python3 - <<'PY'
import pathlib, torch
print(pathlib.Path(torch.__file__).resolve().parent)
PY
)"
TORCH_CMAKE_PREFIX="$(python3 - <<'PY'
import torch
print(torch.utils.cmake_prefix_path)
PY
)"
TORCH_ABI="$(python3 - <<'PY'
import torch
print(int(torch._C._GLIBCXX_USE_CXX11_ABI))
PY
)"
TORCH_LIB_DIR="$TORCH_ROOT/lib"

{
  echo "TORCH_ROOT=$TORCH_ROOT"
  echo "TORCH_CMAKE_PREFIX=$TORCH_CMAKE_PREFIX"
  echo "TORCH_ABI=$TORCH_ABI"
  echo "TORCH_LIB_DIR=$TORCH_LIB_DIR"
} >> "${GITHUB_ENV:-/dev/stdout}"

echo "=== toolchain fingerprint ==="
python3 --version
python3 -c "import torch; print('torch', torch.__version__)"
echo "torch root:   $TORCH_ROOT"
echo "cmake prefix: $TORCH_CMAKE_PREFIX"
echo "torch ABI:    $TORCH_ABI"
${CXX:-g++} --version | head -1
cmake --version | head -1
command -v ninja >/dev/null && ninja --version | head -1 || echo "ninja: absent"
uname -a
