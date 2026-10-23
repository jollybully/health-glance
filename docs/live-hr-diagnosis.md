# Live HR diagnosis (Android companion)

## Symptom (2026-10-23 field report)

## Parent hardware verification (23 October 2026, BST)

- Re-ran `:app:testDebugUnitTest :app:assembleDebug --rerun-tasks`: BUILD SUCCESSFUL, 43 tasks executed. Installed the debug APK with ADB; package lastUpdateTime read back as 16:07:46. No public release, commit or push was made.
- Real connection at 16:08:15: GATT connect/discovery succeeded; HR characteristic advertises notifications; local notification registration returned true; CCCD enqueue and completion both returned success.
- At 16:09:01 the instrumented first-sample timeout recorded raw=0, parseReject=0, contactReject=0, accepted=0. Subsequent reconnects reproduced zero raw callbacks.
- Verified in the Google Health/Fitbit app's accessibility tree that Always visible is checked. Its Share heart rate page simultaneously says Waiting to connect. Toggling Always visible off/on and restoring ON did not restore samples.
- Cleared ONLY the companion's remembered monitor preference after backing it up to app-private `files/live_hr_before_scan.xml`; fresh BLE discovery selected the same Google Fitbit Air and reproduced the failure. No pairing, Health Connect records or other app data were cleared.
- Watch delivery during these tests reports FailedDifferentAppOpen: this is a separate downstream condition, not an explanation for zero incoming BLE callbacks.
- Initial verified failure boundary: Fitbit-to-phone notification stream, before parsing. Phone Bluetooth reset and fresh discovery did not restore packets.
- After user physically restarted Fitbit Air, its sharing screen recognized Mi 9T Pro and incoming HR notifications resumed (first packets reported zero BPM while tracker was off-wrist). Once the user put it back on, 16:28:38 onward logs showed accepted nonzero samples, service delivery and successful Pebble transmission repeatedly. Live source-to-watch transport is now hardware-verified recovered. A stuck peripheral sharing session is consistent with the recovery; its exact firmware cause and any link to the gym remain unproven. No public release was changed.

- Steps accurate; live HR stalled on phone UI (“Connected, waiting for a pulse”).
- Fitbit **broadcast heart rate** reported ON by user (do not infer OFF from silence).
- Foreground `HeartRateService` running; GATT connect + service discovery + `setCharacteristicNotification` succeed.
- Prior build called `onStreaming` after CCCD write and pushed `sendLiveHr bpm=0 state=Streaming` **without any** `onCharacteristicChanged` / valid sample afterward.
- ADB at 16:04 on Mi 9T Pro (API 36): `onSearchComplete` → `setCharacteristicNotification` → ~850 ms later `sendLiveHr bpm=0 state=Streaming`; **no** HR notification callbacks in logcat. Watch send failed with `FailedDifferentAppOpen` (separate Pebble app focus issue).

**Root cause after instrumentation:** Still **unknown** until a debug build is installed and logcat shows whether `rawHrNotifications` stays 0 (no GATT notifications) vs parse/contact rejects.

A successful CCCD / descriptor write is **not** evidence that pulse notifications are flowing.

## What changed (minimal, diagnostic-led)

| Path | Change |
|------|--------|
| `android/.../ble/HeartRateClient.kt` | Session generation, `HealthGlance/HrBle` lifecycle logs, bounded HR hex logs (≤10/connection), parse/contact counters, first-sample timeout (45s), battery read **after** CCCD write, API 33 `writeDescriptor` overload, GATT status checks |
| `android/.../ble/HeartRateService.kt` | `onSubscribed` vs `onStreaming` (watch/UI streaming only after first **accepted** sample), drop notifies watch with `Retry` + `bpm=0`, reconnect backoff via `HrBleReconnect` |
| `android/.../ble/HeartRateMeasurement.kt` | `parseRejectReason()` for diagnostics |
| `android/.../ble/HrBleReconnect.kt` | Pure backoff helper |
| Tests | `HrBleReconnectTest`, `parseRejectReason` in `HeartRateMeasurementTest` |

No watch app, worker, backend, or Health Connect write changes.

## Log tags and counters

Filter logcat:

```bash
adb -s <serial> logcat -s HealthGlance/HrBle HealthGlance/HrSvc HealthGlance
```

| Tag | Meaning |
|-----|---------|
| `HealthGlance/HrBle` | GATT lifecycle, raw HR notifications (hex, bounded), parse/contact rejects, `session_summary` counters |
| `HealthGlance/HrSvc` | `onSample` after BLE accepted a sample |
| `HealthGlance` | `sendLiveHr`, `live hr bpm=…`, `live hr drop_notify`, Pebble transmit |

**Session summary fields** (on drop / timeout): `gen`, `raw`, `parseReject`, `contactReject`, `accepted`, `subscribedMs`, `firstRawMs`, `firstAcceptedMs`, `callbackId`.

**Interpretation:**

| Pattern | Likely layer |
|---------|----------------|
| `raw=0`, subscribed, timeout | No GATT notifications (phone stack, coexistence with Fitbit app, or peripheral not emitting on this link) — **not** parser |
| `raw>0`, `parseReject` high | Malformed or zero-BPM packets |
| `raw>0`, `contactReject` high | Packets with sensor-contact bit = no contact |
| `hr_accepted` + `HealthGlance/HrSvc onSample` but no `sendLiveHr` | Service → Pebble path |
| `sendLiveHr` with `FailedDifferentAppOpen` | PebbleKit / watch app focus (not BLE) |

## Manual verification (parent)

1. Install debug APK from build output (below).
2. Start live HR; leave Fitbit broadcast ON (no BT reset, no data clear).
3. Capture 60–90s of logcat with tag filter above.
4. Confirm whether `hr_raw #1` appears within 45s.
5. If timeout: notification/detail should show the failure reason; watch should get `HR_LIVE_STATE=Retry`, `bpm=0`.

## Unit tests

```bash
source android/scripts/env.sh
cd android
./gradlew :app:testDebugUnitTest :app:assembleDebug
```

## APK output

After a successful `./gradlew :app:assembleDebug`, the APK is:

`android/app/build/outputs/apk/debug/app-debug.apk`

(Green unit tests do **not** prove live HR is fixed on hardware.)
