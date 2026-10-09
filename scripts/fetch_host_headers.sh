#!/usr/bin/env bash
# Download the Schwung host ABI headers into src/host/ so plugin.c builds
# against the real definitions (not a hand-copied approximation).
# Override SCHWUNG_REF to pin a tag/commit.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REF="${SCHWUNG_REF:-main}"
BASE="https://raw.githubusercontent.com/charlesvestal/schwung/${REF}/src/host"
DEST="$ROOT/src/host"

mkdir -p "$DEST"
for f in plugin_api_v1.h audio_fx_api_v2.h; do
    echo "fetch $f ($REF)"
    curl -fsSL "$BASE/$f" -o "$DEST/$f"
done
echo "$REF" > "$DEST/.ref"
