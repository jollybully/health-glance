#!/usr/bin/env bash
# Build, install to the emery emulator, push stats, then capture all four cards.
#
#   ./scripts/emu-capture-deck.sh [outdir]
#
# HG_DEMO=1 seeds the built-in fixture numbers (no phone needed at all);
# HG_DEMO=0 (default) starts empty, so this script pushes a live stats
# AppMessage first — the same integer keys the Android companion sends.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
OUT="${1:-$ROOT/../screenshots}"
DEMO="${HG_DEMO:-0}"
PY="${PEBBLE_PY:-$HOME/.local/share/uv/tools/pebble-tool/bin/python}"
mkdir -p "$OUT"

HG_DEMO="$DEMO" pebble build >/tmp/hg-build.log 2>&1 || { tail -30 /tmp/hg-build.log; exit 1; }
echo "built (HG_DEMO=$DEMO)"

if ! timeout 5 pebble ping --emulator emery >/dev/null 2>&1; then
  pkill -f "qemu-pebble" 2>/dev/null || true
  sleep 2
fi
timeout 180 pebble install --emulator emery >/tmp/hg-install.log 2>&1 || {
  pkill -f "qemu-pebble" 2>/dev/null || true
  sleep 2
  timeout 180 pebble install --emulator emery >/tmp/hg-install.log 2>&1
}
sleep 3

if [[ "$DEMO" == "0" ]]; then
  "$PY" scripts/emu-send-stats.py || { echo "stats push failed"; exit 1; }
  sleep 1
fi

names=(steps sleep heart workout)
for i in 0 1 2 3; do
  if [[ "$i" -gt 0 ]]; then
    timeout 30 pebble emu-button click up >/dev/null 2>&1 || true
    sleep 1
  fi
  ./scripts/emu-shot.sh "$OUT/c-$DEMO-${names[$i]}.png" >/dev/null
done

# Return to the first card so the emulator is left in a known state.
for _ in 1 2 3; do
  timeout 30 pebble emu-button click down >/dev/null 2>&1 || true
  sleep 0.4
done
echo "captured 4 cards -> $OUT"
