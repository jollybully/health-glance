#!/usr/bin/env bash
# Capture every screen by booting directly to it (no buttons / no ping overlay).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PEBBLE="$ROOT/pebble"
MAIN="$PEBBLE/src/embeddedjs/main.js"
OUTDIR="$ROOT"

screens=(
  "0 0 emulator-activity.png"
  "0 1 emulator-act-detail.png"
  "0 2 emulator-hourly.png"
  "0 3 emulator-workout.png"
  "1 0 emulator-sleep.png"
  "1 4 emulator-sleep-detail.png"
  "2 0 emulator-heart.png"
  "2 5 emulator-heart-detail.png"
)

# Ensure healthy emu once
cd "$PEBBLE"
if ! timeout 4 pebble ping --emulator emery >/dev/null 2>&1; then
  pkill -f "qemu-pebble|pypkjs" 2>/dev/null || true
  pebble kill >/dev/null 2>&1 || true
  sleep 1
fi

for entry in "${screens[@]}"; do
  set -- $entry
  br=$1; bs=$2; name=$3
  python3 - <<PY
from pathlib import Path
p = Path("$MAIN")
t = p.read_text()
import re
t2, n = re.subn(
    r"const BOOT_ROOT = \d+, BOOT_STACK = \d+;",
    "const BOOT_ROOT = $br, BOOT_STACK = $bs;",
    t, count=1)
if n != 1:
    raise SystemExit("BOOT constants not found")
p.write_text(t2)
print("boot root=$br stack=$bs -> $name")
PY
  pebble build >/tmp/pebble-build.log 2>&1 || { tail -30 /tmp/pebble-build.log; exit 1; }
  XSA=$(wc -c < build/mods/emery/mc.xsa | tr -d ' ')
  pebble install --emulator emery >/tmp/pebble-install.log 2>&1 || { tail -20 /tmp/pebble-install.log; exit 1; }
  sleep 1.0
  # Prefer official screenshot; fall back to qemu dump
  if ! pebble screenshot --emulator emery "$OUTDIR/$name" >/tmp/pebble-ss.log 2>&1; then
    mon=$(ps aux | rg "qemu-pebble" | rg -v rg | rg -o "monitor tcp::[0-9]+" | head -1 | rg -o "[0-9]+$")
    printf 'screendump /tmp/emu.ppm\n' | nc -w 2 localhost "$mon" >/dev/null
    magick /tmp/emu.ppm "$OUTDIR/$name"
    echo "  qemu dump (screenshot failed)"
  else
    echo "  screenshot ok xsa=$XSA"
  fi
  if ! timeout 4 pebble ping --emulator emery >/dev/null 2>&1; then
    echo "  FAIL ping after $name — memory full?"
    exit 2
  fi
  echo "  ping ok"
done

# Reset boot to root activity for normal use
python3 - <<PY
from pathlib import Path
import re
p = Path("$MAIN")
t = p.read_text()
t2, n = re.subn(r"const BOOT_ROOT = \d+, BOOT_STACK = \d+;", "const BOOT_ROOT = 0, BOOT_STACK = 0;", t, count=1)
p.write_text(t2)
print("reset BOOT to 0,0")
PY

echo "ALL SCREENS CAPTURED"
