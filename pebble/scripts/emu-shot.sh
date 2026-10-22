#!/usr/bin/env bash
# Capture the emulator framebuffer to a PNG.
# `pebble screenshot` is flaky on this SDK, so we screendump over the QEMU
# monitor socket instead. Usage: ./scripts/emu-shot.sh out.png
set -euo pipefail
OUT="${1:?out.png required}"
MON=$(ps aux | grep "[q]emu-pebble" | grep -o "monitor tcp::[0-9]*" | head -1 | grep -o "[0-9]*$")
if [[ -z "${MON:-}" ]]; then
  echo "emu-shot: no qemu-pebble monitor socket" >&2
  exit 1
fi
TMP="/tmp/hg-emu-shot.ppm"
printf 'screendump %s\n' "$TMP" | nc -w 3 localhost "$MON" >/dev/null 2>&1 || true
if [[ ! -s "$TMP" ]]; then
  echo "emu-shot: screendump failed on port $MON" >&2
  exit 1
fi
sips -s format png "$TMP" --out "$OUT" >/dev/null
echo "$OUT"
