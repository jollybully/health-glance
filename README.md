# Health Glance

Health data from **Health Connect** → Android companion → **Pebble Time 2** (Emery).

Any Health Connect writer works (Fitbit, Samsung Health, Google Fit, etc.). The companion reads on-device and pushes stats to the watch over PebbleKit 2.

**Personal reference only:** `worker/` is a Cloudflare Google Health OAuth experiment. The shareable companion path does not use it.

## Downloads

Latest release assets:

- [health-glance.pbw](https://github.com/jollybully/health-glance/releases/latest/download/health-glance.pbw) — watch app (Emery)
- [health-glance-debug.apk](https://github.com/jollybully/health-glance/releases/latest/download/health-glance-debug.apk) — Android companion (sideload / debug)

> If those links 404, open [Releases](https://github.com/jollybully/health-glance/releases) and grab the newest build.

## Install (community)

1. Install **Core** on your phone and pair your Pebble Time 2 (`coredevices.coreapp` ≥ 1.0.7.7).
2. Sideload `health-glance-debug.apk` (enable install from unknown sources if prompted).
3. Open **Health Glance** → grant Health Connect **read** permissions (and background read).
4. Ensure your fitness app is sharing the metrics you care about into Health Connect.
5. Install `health-glance.pbw` on the watch (Core / developer sideload, or `pebble install --phone <PHONE_IP>` if you have the Pebble toolchain).
6. Allow the companion when Core prompts. Open the watch app — it paints last-known stats immediately, then requests a fresh push.

## Repo layout

```
android/   Kotlin companion (Health Connect + PebbleKit Android 2)
pebble/    Alloy watch app (Steps / Sleep / Heart / Workout)
worker/    Personal Google Health OAuth Worker (reference only)
```

## Build from source (optional)

### Android

- Android Studio / SDK 36
- JDK via Studio JBR (or set `JAVA_HOME`)

```bash
source android/scripts/env.sh
cd android
./gradlew :app:assembleDebug    # or :app:installDebug
```

Debug builds include a Health Connect seeder for emulator QA. Release builds are read-only and omit the seeder.

### Watch

Requires [Pebble Tool](https://developer.repebble.com) + SDK 4.17 (Moddable / Emery).

```bash
cd pebble
pebble build
pebble install --emulator emery          # set DEMO=1 in main.js for sample numbers
pebble install --phone <PHONE_IP>        # live companion path
```

| Button | On Steps | On other cards |
|--------|----------|----------------|
| Up | → Sleep | Next card (stops on Workout) |
| Down | Exit → watchface | Previous card |
| Back | Exit | Exit |

Deck: Steps → Sleep → Heart → Workout. See `pebble/README.md`.

## Sync behaviour

- Watch paints last-known stats from Alloy `localStorage` on open
- Push on watch app open (`STATS_REQ`) and when companion syncs
- Manual **Sync & push to watch** in the companion
- Periodic WorkManager refresh every **30 minutes** (phone-side cache; AppMessage delivery still needs Core + open watch app)

## Stats contract (companion → watch)

Flat AppMessage keys (see `pebble/package.json` `messageKeys`). Live UI uses root scalars + workout:

- Warm push: Steps / Sleep / Heart (`toRootDictionary`)
- Full push: same + workout keys (`WO_DUR_S`, `WO_CAL`, `WO_AVG_HR`, `WO_DIST_M`, `WO_START`, `WO_END`, `WO_TITLE`)

## Security

- Health data stays on-device; companion reads Health Connect and pushes to the paired watch only
- No Google refresh token in the companion path
- Play Store Health declarations required before a public listing (out of scope for sideload builds)

## License

MIT — see [LICENSE](LICENSE).
