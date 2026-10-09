#!/usr/bin/env bash
# Cross-compile tests/device_smoke.c and run it on the Move against the
# installed module. Catches dlopen/dlsym/ABI/runtime faults without needing
# to select the module in the UI.
#
#   scripts/smoke_on_device.sh [module.so path on device]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ZIG="${ZIG:-zig}"
TARGET="${TARGET:-aarch64-linux-gnu.2.35}"
MOVE_HOST="${MOVE_HOST:-move.local}"
MOVE_USER="${MOVE_USER:-ableton}"
SO="${1:-/data/UserData/schwung/modules/audio_fx/tinyvoc/tinyvoc.so}"

[ -f "$ROOT/src/host/audio_fx_api_v2.h" ] || "$ROOT/scripts/fetch_host_headers.sh"
mkdir -p "$ROOT/build"
"$ZIG" cc -target "$TARGET" -O1 -I"$ROOT/src/host" "$ROOT/tests/device_smoke.c" \
    -o "$ROOT/build/device_smoke" -lm -ldl

REMOTE=/tmp/tinyvoc_device_smoke
tar -C "$ROOT/build" -cf - device_smoke | ssh "$MOVE_USER@$MOVE_HOST" "cat > $REMOTE.tar && tar -C /tmp -xf $REMOTE.tar && mv /tmp/device_smoke $REMOTE && chmod +x $REMOTE && rm -f $REMOTE.tar"
ssh "$MOVE_USER@$MOVE_HOST" "$REMOTE '$SO'; rc=\$?; rm -f $REMOTE; exit \$rc"
