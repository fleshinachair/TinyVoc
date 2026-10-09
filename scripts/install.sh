#!/usr/bin/env bash
# Deploy the built module to a Move over SSH (dev sideload).
#
#   scripts/install.sh [--build]
#
# Env overrides: MOVE_HOST (default move.local), MOVE_USER (default ableton), DEST
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MODULE_ID=tinyvoc
MOVE_HOST="${MOVE_HOST:-move.local}"
MOVE_USER="${MOVE_USER:-ableton}"
DEST="${DEST:-/data/UserData/schwung/modules/audio_fx/$MODULE_ID}"
SRC="$ROOT/build/$MODULE_ID"

[ "${1:-}" = "--build" ] && "$ROOT/scripts/build.sh"
[ -f "$SRC/$MODULE_ID.so" ] || { echo "nothing built; run scripts/build.sh (or pass --build)" >&2; exit 1; }

echo "installing to $MOVE_USER@$MOVE_HOST:$DEST"
# tar over ssh: no rsync/scp dependency, works from Git Bash on Windows.
tar -C "$SRC" -cf - . | ssh "$MOVE_USER@$MOVE_HOST" "mkdir -p '$DEST' && rm -f '$DEST/dsp.so' '$DEST/ui.js' '$DEST/ui_chain.js' && tar -C '$DEST' -xf - && chmod 755 '$DEST'/*.so"

# The module was called VocalTron (id "vocaltron") until it became TinyVoc.
# Remove that old install so the browser doesn't list both.
LEGACY_DIR="$(dirname "$DEST")/vocaltron"
ssh "$MOVE_USER@$MOVE_HOST" "if [ -d '$LEGACY_DIR' ]; then rm -rf '$LEGACY_DIR' && echo 'removed the old $LEGACY_DIR'; fi"

echo "done. Reload the module (or restart Schwung) to pick up the new $MODULE_ID.so."
