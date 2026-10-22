#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck disable=SC1091
source "$ROOT/scripts/env.sh"

AVD="${HEALTH_GLANCE_AVD}"
PACKAGE="${HEALTH_GLANCE_PACKAGE}"
SCREENSHOT_DIR="$ROOT/screenshots"
mkdir -p "$SCREENSHOT_DIR"

stamp="$(date +%Y%m%d-%H%M%S)"
shot="$SCREENSHOT_DIR/companion-$stamp.png"
log="$SCREENSHOT_DIR/logcat-$stamp.txt"

ensure_device() {
  if adb devices | awk 'NR>1 && $2=="device" {print $1}' | grep -q .; then
    echo "Using existing adb device(s)"
    return 0
  fi
  echo "Starting emulator $AVD…"
  nohup emulator -avd "$AVD" -no-audio -no-boot-anim >/tmp/health-glance-emulator.log 2>&1 &
  adb wait-for-device
  echo "Waiting for boot…"
  until adb shell getprop sys.boot_completed 2>/dev/null | grep -q 1; do
    sleep 2
  done
  sleep 3
}

ensure_device

echo "Building + installing debug APK…"
(
  cd "$ROOT"
  ./gradlew :app:installDebug
)

echo "Launching companion…"
adb shell am start -n "$PACKAGE/.MainActivity" >/dev/null

echo
if [[ "${AGENT_NONINTERACTIVE:-}" == "1" ]]; then
  echo "AGENT_NONINTERACTIVE=1 — skipping permission prompt (ensure HC perms already granted)"
else
  echo "=== HUMAN STEP (first run / wiped AVD) ==="
  echo "On the emulator: open Health Glance → Grant Health Connect permissions"
  echo "(read + write so the seeder can insert sample records)."
  echo "Press Enter once permissions are granted (or if already granted)…"
  read -r _
fi

echo "Seeding Health Connect sample data…"
adb shell am start -a com.jollybully.healthglance.action.SEED_HEALTH_DATA \
  -n "$PACKAGE/.debug.SeedHealthDataActivity" >/dev/null
sleep 3

adb shell am start -n "$PACKAGE/.MainActivity" >/dev/null
sleep 2

echo "Screenshot → $shot"
adb exec-out screencap -p >"$shot"

echo "Logcat → $log"
adb logcat -d -s HealthGlance:D AndroidRuntime:E >"$log" || true

echo
echo "Done."
echo "  screenshot: $shot"
echo "  logcat:     $log"
echo "Last HealthGlance lines:"
rg "HealthGlance" "$log" | tail -20 || true
