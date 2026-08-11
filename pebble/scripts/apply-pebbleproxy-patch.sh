#!/bin/sh
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/patches/pebbleproxy-proxy.js"
DST="$ROOT/node_modules/@moddable/pebbleproxy/proxy.js"
if [ -f "$SRC" ] && [ -d "$(dirname "$DST")" ]; then
  cp "$SRC" "$DST"
  echo "Applied pebbleproxy patch"
fi
