#!/usr/bin/env bash
# Cross-compile TinyVoice for Move (aarch64 Linux) with zig cc and package it.
#
#   scripts/build.sh            -> build/tinyvoice/ + build/TinyVoice.tar.gz
#
# Env overrides: ZIG, TARGET, OPT
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ZIG="${ZIG:-zig}"
# The host dlopen()s us into a glibc process (Move ships glibc 2.35), so we
# target glibc, not musl: a musl-linked .so would carry a second libc into it.
TARGET="${TARGET:-aarch64-linux-gnu.2.35}"
OPT="${OPT:--O2}"

MODULE_ID=tinyvoice
OUT="$ROOT/build/$MODULE_ID"

command -v "$ZIG" >/dev/null || { echo "zig not found (https://ziglang.org/download/)" >&2; exit 1; }
[ -f "$ROOT/src/host/audio_fx_api_v2.h" ] || "$ROOT/scripts/fetch_host_headers.sh"

rm -rf "$OUT"
mkdir -p "$OUT"

# No -ffast-math: it lets the compiler pull in libmvec, which the device lacks.
"$ZIG" cc -target "$TARGET" $OPT -shared -fPIC -fvisibility=hidden \
    -std=c11 -Wall -Wextra -Werror -pedantic \
    -I"$ROOT/src/host" -I"$ROOT/src/dsp" \
    "$ROOT/src/dsp/dsp.c" "$ROOT/src/dsp/cher.c" "$ROOT/src/dsp/daft.c" "$ROOT/src/dsp/imogen.c" "$ROOT/src/dsp/psola.c" "$ROOT/src/dsp/modes.c" "$ROOT/src/dsp/freeze.c" "$ROOT/src/dsp/demo.c" "$ROOT/src/dsp/voice.c" "$ROOT/src/dsp/verb.c" "$ROOT/src/dsp/plugin.c" \
    -o "$OUT/$MODULE_ID.so" -lm
chmod 755 "$OUT/$MODULE_ID.so"

cp "$ROOT/module.json"   "$OUT/module.json"
cp "$ROOT/src/ui/canvas.js" "$OUT/canvas.js"     # OLED widget (custom viz kind)

# Sanity: both entry points must be exported. The chain host dlsym()s
# move_audio_fx_on_midi to decide whether the FX receives any MIDI at all.
for sym in move_audio_fx_init_v2 move_audio_fx_on_midi; do
    grep -aq "$sym" "$OUT/$MODULE_ID.so" || { echo "error: $sym not exported" >&2; exit 1; }
done
if command -v nm >/dev/null; then
    for sym in move_audio_fx_init_v2 move_audio_fx_on_midi; do
        nm -D "$OUT/$MODULE_ID.so" | grep -q " T $sym\$" || { echo "error: $sym is not a defined export" >&2; exit 1; }
    done
    if nm -D "$OUT/$MODULE_ID.so" | grep -q 'libmvec'; then
        echo "error: links libmvec" >&2; exit 1
    fi
fi

# The archive is named for the module; inside it the folder is the module id (the host needs folder == id).
ARCHIVE=TinyVoice.tar.gz
rm -f "$ROOT/build/$MODULE_ID-module.tar.gz"
tar -C "$ROOT/build" -czf "$ROOT/build/$ARCHIVE" "$MODULE_ID"
echo "built $OUT/$MODULE_ID.so ($TARGET)"
echo "package build/$ARCHIVE"
