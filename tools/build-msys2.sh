#!/usr/bin/env bash
# Builds FNVVR on Windows with MSYS2 and runs the VR unit tests (see docs/building.md).
# Run from Git Bash or any MSYS2 shell, in the repository root:  tools/build-msys2.sh [--setup]
#   --setup  creates the build directory build.msys anew
# Output: build.msys/src/d3d9/d3d9.dll
set -euo pipefail
MSYS=${MSYS2_ROOT:-/c/msys64}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WIN_ROOT=$(cygpath -m "$ROOT")

# UCRT64 for meson, ninja, glslang and the tests' compiler; MINGW32 last, for the i686 compiler's DLLs
run() {
  MSYSTEM=UCRT64 CHERE_INVOKING=1 "$MSYS/usr/bin/bash.exe" -lc \
    "export PATH=/ucrt64/bin:\$PATH:/mingw32/bin; cd '$WIN_ROOT' && $1"
}

if [[ "${1:-}" == "--setup" || ! -f "$ROOT/build.msys/build.ninja" ]]; then
  rm -rf "$ROOT/build.msys"
  run "meson setup --cross-file build-win32-msys2.txt --native-file build-native-msys2.txt --buildtype release build.msys"
fi
run "ninja -C build.msys"
run "meson test -C build.msys --print-errorlogs"
