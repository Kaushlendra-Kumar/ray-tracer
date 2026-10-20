#!/usr/bin/env bash
# Builds the WebAssembly renderer in two variants into web/frontend/public/wasm:
#   rt-mt  pthreads + SharedArrayBuffer (page must be cross-origin isolated)
#   rt-st  single-threaded fallback
# Needs the Emscripten SDK environment (emcmake on PATH) plus CMake and Ninja.
# Usage: tools/build_wasm.sh
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=web/frontend/public/wasm
mkdir -p "$OUT"
for variant in mt st; do
  threads=OFF
  [ "$variant" = mt ] && threads=ON
  emcmake cmake -S . -B "build-wasm-$variant" -G Ninja -DCMAKE_BUILD_TYPE=Release -DRT_WASM_THREADS=$threads >/dev/null
  cmake --build "build-wasm-$variant" --target rt_wasm
  cp "build-wasm-$variant/rt-$variant.mjs" "build-wasm-$variant/rt-$variant.wasm" "$OUT/"
done
ls -l "$OUT"
