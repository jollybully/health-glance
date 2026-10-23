#!/usr/bin/env bash
# End-to-end emulator check for Health Glance on both shapes.
#
# Assertions are shape-aware on purpose: the rectangular card identifies itself with a
# colourful cap band, while the round card identifies itself with a ring at the glass.
# Probing cap coordinates on a round screen just lands on white and reports a failure
# that is really the test's fault.
set -uo pipefail
cd "$(dirname "$0")/.."
PB="$HOME/.local/share/uv/tools/pebble-tool/bin/pebble"
PY="$HOME/.local/share/uv/tools/pebble-tool/bin/python"
SHOTS=/tmp/hg-verify
mkdir -p "$SHOTS"
PASS=0; FAIL=0
ok()  { echo "PASS  $1"; PASS=$((PASS+1)); }
bad() { echo "FAIL  $1"; FAIL=$((FAIL+1)); }
shot() { HG_SHOT_WAKE=1 ./scripts/emu-shot.sh "$SHOTS/$1.png" >/dev/null 2>&1; }

# which card is showing: whichever cap colour dominates the frame
which_card() {
  python3 -c "
from PIL import Image
im=Image.open('$SHOTS/$1.png').convert('RGB'); px=im.load()
caps={(0,170,85):'steps',(0,85,170):'sleep',(85,0,85):'heart',(255,85,0):'workouts'}
counts={k:sum(1 for y in range(im.size[1]) for x in range(im.size[0]) if px[x,y]==k) for k in caps}
best=max(counts, key=counts.get)
print(caps[best] if counts[best]>300 else 'none')"
}

# pulse ink: the ring at the glass on round, the border frame on rectangular
# pulse ink: the ring at the glass on round, the border frame on rectangular
pulse() {
  python3 -c "
from PIL import Image
im=Image.open('$SHOTS/$1.png').convert('RGB'); px=im.load()
w,h=im.size
if '$2' == 'round':
    cx,cy=w//2,h//2
    n=sum(1 for y in range(h) for x in range(w)
          if (w//2-10)<=((x-cx)**2+(y-cy)**2)**0.5<=(w//2-1) and px[x,y]==(0,0,0))
else:
    n=sum(1 for y in range(3) for x in range(w) if px[x,y]==(0,0,0))
    n+=sum(1 for y in range(h-3,h) for x in range(w) if px[x,y]==(0,0,0))
    n+=sum(1 for y in range(h) for x in range(3) if px[x,y]==(0,0,0))
    n+=sum(1 for y in range(h) for x in range(w-3,w) if px[x,y]==(0,0,0))
print(n)"; }

# A single frame is not enough: the lub-dub waveform passes through zero thickness
# between beats, so one sample can read 0 on a perfectly healthy pulse. Take a burst
# and keep the largest.
pulse_max() {
  local f=$1 shape=$2 n=${3:-6} best=0 got
  for _ in $(seq 1 "$n"); do
    got=$(pulse "$f" "$shape")
    [[ "$got" -gt "$best" ]] && best=$got
    sleep 0.12
    shot "$f"
  done
  echo "$best"
}

run_platform() {
  local plat=$1 title=$2 shape=$3
  echo "== $title =="
  pkill -9 -f 'qemu[-]pebble' 2>/dev/null; sleep 2
  "$PB" install --emulator "$plat" >/dev/null 2>&1; sleep 3
  "$PY" scripts/emu-send-stats.py >/dev/null 2>&1; sleep 1

  shot "$plat-1"
  [[ "$(which_card "$plat-1")" == "steps" ]] && ok "$plat launches on Steps" \
    || bad "$plat launches on Steps (showing $(which_card "$plat-1"))"

  local i=2
  for want in sleep heart workouts; do
    "$PB" emu-button click up >/dev/null 2>&1; sleep 1.1; shot "$plat-$i"
    [[ "$(which_card "$plat-$i")" == "$want" ]] && ok "$plat up -> $want" \
      || bad "$plat up -> $want (showing $(which_card "$plat-$i"))"
    i=$((i+1))
  done

  "$PB" emu-button click up >/dev/null 2>&1; sleep 1.3; shot "$plat-5"
  [[ "$(which_card "$plat-5")" == "workouts" ]] && ok "$plat springs at the end of the deck" \
    || bad "$plat end-of-deck (showing $(which_card "$plat-5"))"

  "$PB" emu-button click down >/dev/null 2>&1; sleep 1.2; shot "$plat-heart-live"
  [[ "$(which_card "$plat-heart-live")" == "heart" ]] && ok "$plat down -> heart" \
    || bad "$plat down -> heart (showing $(which_card "$plat-heart-live"))"
  # push a fresh sample while the Heart card is up: this is the real condition, and it
  # avoids failing on a 20s freshness window that expired during paging
  "$PY" scripts/emu-send-stats.py >/dev/null 2>&1; sleep 0.8
  local r; r=$(pulse_max "$plat-heart-live" "$shape" 6)
  [[ "$r" -gt 100 ]] && ok "$plat heart pulse visible while live (ink $r)" \
    || bad "$plat heart pulse while live (ink $r)"

  "$PB" emu-button click up >/dev/null 2>&1; sleep 1.1; shot "$plat-nopulse"
  r=$(pulse "$plat-nopulse" "$shape")
  [[ "$r" -lt 60 ]] && ok "$plat no pulse on the Workouts card (ink $r)" \
    || bad "$plat pulse leaked onto Workouts (ink $r)"

  echo "      waiting 25s for the live sample to go stale..."
  sleep 25
  "$PB" emu-button click down >/dev/null 2>&1; sleep 1.3; shot "$plat-stale"
  r=$(pulse "$plat-stale" "$shape")
  [[ "$r" -lt 60 ]] && ok "$plat pulse stops on stale data (ink $r)" \
    || bad "$plat pulse kept beating on stale data (ink $r)"

  pkill -9 -f 'qemu[-]pebble' 2>/dev/null; sleep 2
  "$PB" install --emulator "$plat" >/dev/null 2>&1; sleep 3   # no fixture push: must use cache
  shot "$plat-cold"
  [[ "$(which_card "$plat-cold")" == "steps" ]] && ok "$plat cold start shows cached Steps" \
    || bad "$plat cold start (showing $(which_card "$plat-cold"))"

  "$PB" emu-button click back >/dev/null 2>&1; sleep 1.5; shot "$plat-exit"
  [[ "$(which_card "$plat-exit")" == "none" ]] && ok "$plat Back leaves the app" \
    || bad "$plat Back left the card up (showing $(which_card "$plat-exit"))"
}

run_platform gabbro "Round 2 (gabbro, 260x260 round)" round
run_platform emery  "Time 2 (emery, 200x228 rect)"    rect

echo
echo "pbw platforms: $(unzip -l build/pebble.pbw 2>/dev/null | grep -oE '(emery|gabbro|basalt|chalk|diorite|flint)' | sort -u | tr '\n' ' ')"
echo "pbw bytes: $(wc -c < build/pebble.pbw | tr -d ' ')"
echo "TOTAL: $PASS passed, $FAIL failed"
