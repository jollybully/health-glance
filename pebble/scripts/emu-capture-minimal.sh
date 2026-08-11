#!/usr/bin/env bash
# Capture minimal 4-card deck via BOOT_ROOT (no button presses).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PEBBLE="$ROOT/pebble"
MAIN="$PEBBLE/src/embeddedjs/main.js"
OUTDIR="$ROOT"
cd "$PEBBLE"

screens=(
  "0 emu-minimal-steps.png"
  "1 emu-minimal-sleep.png"
  "2 emu-minimal-heart.png"
  "3 emu-minimal-workout.png"
)

if ! timeout 4 pebble ping --emulator emery >/dev/null 2>&1; then
  pkill -f "qemu-pebble|pypkjs" 2>/dev/null || true
  pebble kill >/dev/null 2>&1 || true
  sleep 1
fi

for entry in "${screens[@]}"; do
  set -- $entry
  br=$1; name=$2
  python3 - <<PY
from pathlib import Path
import re
p = Path("$MAIN")
t = p.read_text()
t2, n = re.subn(r"const BOOT_ROOT = \d+;", "const BOOT_ROOT = $br;", t, count=1)
if n != 1:
    raise SystemExit("BOOT_ROOT not found")
p.write_text(t2)
print("boot root=$br -> $name")
PY
  ./scripts/emu-check.sh "$OUTDIR/$name" "root$br" || exit $?
done

python3 - <<PY
from pathlib import Path
import re
p = Path("$MAIN")
t = p.read_text()
t2, n = re.subn(r"const BOOT_ROOT = \d+;", "const BOOT_ROOT = 0;", t, count=1)
p.write_text(t2)
print("reset BOOT_ROOT to 0")
PY

echo "MINIMAL DECK CAPTURED"
