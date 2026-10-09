#!/usr/bin/env bash
# Build and run the host-native test runner (engine only; no Schwung headers needed).
#
#   scripts/test.sh                 # uses zig cc, falls back to cc/gcc/clang
#   SANITIZE=1 scripts/test.sh      # adds ASan+UBSan (needs a Linux/macOS host toolchain)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/tests"
mkdir -p "$OUT"

if [ -n "${CC:-}" ]; then CCMD=($CC)
elif command -v zig >/dev/null; then CCMD=(zig cc)
elif command -v cc >/dev/null; then CCMD=(cc)
elif command -v gcc >/dev/null; then CCMD=(gcc)
elif command -v clang >/dev/null; then CCMD=(clang)
else echo "no C compiler found (install zig)" >&2; exit 1; fi

EXTRA=()
[ "${SANITIZE:-0}" = 1 ] && EXTRA+=(-fsanitize=address,undefined -fno-omit-frame-pointer)

EXE="$OUT/test_runner"
"${CCMD[@]}" -std=c11 -O1 -g -Wall -Wextra -DVT_TEST_ALLOC "${EXTRA[@]}" \
    -I"$ROOT/src/dsp" \
    "$ROOT/tests/test_main.c" "$ROOT/src/dsp/dsp.c" "$ROOT/src/dsp/cher.c" "$ROOT/src/dsp/daft.c" "$ROOT/src/dsp/imogen.c" "$ROOT/src/dsp/psola.c" "$ROOT/src/dsp/modes.c" "$ROOT/src/dsp/freeze.c" "$ROOT/src/dsp/demo.c" "$ROOT/src/dsp/verb.c" \
    -o "$EXE" -lm

cd "$ROOT"      # tests read module.json from the repo root
"$EXE"

# The OLED widget (src/ui/canvas.js) runs in Schwung's JS host; test it with node if present.
if command -v node >/dev/null; then
    node "$ROOT/tests/canvas_test.mjs"
else
    echo "node not found: skipping tests/canvas_test.mjs"
fi
