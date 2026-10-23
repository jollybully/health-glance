#!/usr/bin/env bash
# Capture the emulator framebuffer to a PNG.
# `pebble screenshot` is flaky on this SDK, so we screendump over the QEMU
# monitor socket instead. Usage: ./scripts/emu-shot.sh out.png
#
# The emulator dims the backlight after a few seconds of inactivity, which halves
# every colour in the capture, so the first shot after a launch comes out greyed.
# We nudge Select first: it is inert on the deck, so it wakes the screen without
# changing what is being photographed. HG_SHOT_WAKE=0 skips the nudge.
set -euo pipefail
OUT="${1:?out.png required}"
MON=$(ps aux | grep "[q]emu-pebble" | grep -o "monitor tcp::[0-9]*" | head -1 | grep -o "[0-9]*$")
if [[ -z "${MON:-}" ]]; then
  echo "emu-shot: no qemu-pebble monitor socket" >&2
  exit 1
fi
TMP="/tmp/hg-emu-shot.ppm"
if [[ "${HG_SHOT_WAKE:-1}" == "1" ]]; then
  PEBBLE_BIN="${PEBBLE_BIN:-$(command -v pebble || echo "$HOME/.local/share/uv/tools/pebble-tool/bin/pebble")}"
  timeout 20 "$PEBBLE_BIN" emu-button click select >/dev/null 2>&1 || true
  sleep 0.35
fi
printf 'screendump %s\n' "$TMP" | nc -w 3 localhost "$MON" >/dev/null 2>&1 || true
if [[ ! -s "$TMP" ]]; then
  echo "emu-shot: screendump failed on port $MON" >&2
  exit 1
fi
sips -s format png "$TMP" --out "$OUT" >/dev/null
echo "$OUT"
