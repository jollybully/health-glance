# pebble

Alloy watch app for Health Glance — minimal 4-card deck on Pebble Time 2 (Emery).

**UI:** Steps · Sleep · Heart · Workout. Bitham Black 30 heroes + Gothic Bold 24. Shared title/hero/unit slots (no side breadcrumbs).

Stats come from the Android companion (`../android`) via PebbleKit AppMessages. There is no PKJS.

On open, the watch paints last-known stats if a companion push already landed, then requests a fresh push from the phone. Phone WorkManager keeps the companion cache warm; there is no watch-side background worker.

## Building & running

```sh
pebble build
pebble install --emulator emery       # set DEMO=1 in main.js for sample numbers
pebble install --phone <ip>           # live Health Connect data via companion
```

### DEMO vs phone

In `src/embeddedjs/main.js`:

- `DEMO = 1` — seed sample data for emery QA (skips the phone request)
- `DEMO = 0` — wait for the companion, then paint live stats

### Emulator QA

```sh
./scripts/emu-check.sh <screenshot.png>
./scripts/emu-capture-minimal.sh
```

## Navigation (Up quick-launch)

Watchface **Up** opens the app on Steps. Keep pressing **Up** to page forward. **Down on Steps** exits.

| Button | On Steps | On other cards |
|--------|----------|----------------|
| Up | → Sleep | Next card (stops on Workout) |
| Down | Exit → watchface | Previous card |
| Back | Exit | Exit |

Deck: Steps → Sleep → Heart → Workout.

## Layout

```
src/c/mdbl.c                   C glue around the Moddable runtime
src/embeddedjs/main.js         Watch UI + AppMessage inbox/outbox
src/embeddedjs/fixture.js      Sample data reference
src/embeddedjs/manifest.json   Moddable manifest
package.json                   UUID, companionApp package, messageKeys
```

Companion package: `com.jollybully.healthglance`.

## Docs

https://developer.repebble.com
