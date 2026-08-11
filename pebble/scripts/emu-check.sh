#!/usr/bin/env bash
# Build, install to emery emulator, screenshot (before ping!), then ping.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
OUT="${1:-$ROOT/../emulator-activity.png}"
LABEL="${2:-check}"

pebble build >/tmp/pebble-build.log 2>&1 || { tail -40 /tmp/pebble-build.log; exit 1; }
XSA=$(wc -c < build/mods/emery/mc.xsa | tr -d ' ')
SRC=$(wc -c < src/embeddedjs/main.js | tr -d ' ')
echo "[$LABEL] src=$SRC xsa=$XSA"

if ! timeout 4 pebble ping --emulator emery >/dev/null 2>&1; then
  pkill -f "qemu-pebble|pypkjs" 2>/dev/null || true
  pebble kill >/dev/null 2>&1 || true
  sleep 1
fi

for attempt in 1 2 3; do
  if pebble install --emulator emery >/tmp/pebble-install.log 2>&1; then
    break
  fi
  echo "[$LABEL] install fail attempt $attempt — restart emu"
  pkill -f "qemu-pebble|pypkjs" 2>/dev/null || true
  pebble kill >/dev/null 2>&1 || true
  sleep 2
  if [[ $attempt -eq 3 ]]; then
    tail -20 /tmp/pebble-install.log
    exit 1
  fi
done
sleep 1.2

# IMPORTANT: screenshot before ping — ping overlays the display.
pebble screenshot --emulator emery "$OUT" >/tmp/pebble-ss.log 2>&1 || {
  mon=$(ps aux | rg "qemu-pebble" | rg -v rg | rg -o "monitor tcp::[0-9]+" | head -1 | rg -o "[0-9]+$" || true)
  if [[ -n "${mon:-}" ]]; then
    printf 'screendump /tmp/emu.ppm\n' | nc -w 2 localhost "$mon" >/dev/null || true
    magick /tmp/emu.ppm "$OUT"
    echo "[$LABEL] qemu dump fallback"
  else
    echo "[$LABEL] screenshot failed"; exit 1
  fi
}

if timeout 6 pebble ping --emulator emery >/dev/null 2>&1; then
  echo "[$LABEL] OK -> $OUT"
else
  # Ping can flake while the app is fine; treat tiny white PNGs as real failure.
  sz=$(wc -c < "$OUT" | tr -d ' ')
  if [[ "$sz" -lt 800 ]]; then
    echo "[$LABEL] FAIL: blank screenshot ($sz bytes) — likely memory full"
    exit 2
  fi
  echo "[$LABEL] WARN: ping flake but screenshot looks ok ($sz bytes) -> $OUT"
fi
