#!/usr/bin/env bash
# Build + install to the emery emulator, then sanity-check the screenshot.
#
#   ./scripts/emu-check.sh [screenshot.png] [label]
#
# Fails when the screenshot is flat (crash or nothing painted).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
OUT="${1:-/tmp/hg-check.png}"
LABEL="${2:-check}"

pebble build >/tmp/hg-build.log 2>&1 || { tail -40 /tmp/hg-build.log; exit 1; }
echo "[$LABEL] built (HG_DEMO=${HG_DEMO:-0})"

if ! timeout 5 pebble ping --emulator emery >/dev/null 2>&1; then
  pkill -f "qemu-pebble" 2>/dev/null || true
  sleep 2
fi
timeout 180 pebble install --emulator emery >/tmp/hg-install.log 2>&1 || {
  tail -20 /tmp/hg-install.log
  exit 1
}
sleep 3
./scripts/emu-shot.sh "$OUT" >/dev/null

python3 - "$OUT" "$LABEL" <<'PY'
import sys
from PIL import Image
path, label = sys.argv[1], sys.argv[2]
im = Image.open(path).convert("RGB")
colours = len(set(im.getdata()))
print("[%s] %s  %dx%d  colours=%d" % (label, path, im.size[0], im.size[1], colours))
if colours < 4:
    print("[%s] FAIL: flat screenshot - app likely crashed or painted nothing" % label)
    sys.exit(2)
print("[%s] OK" % label)
PY
