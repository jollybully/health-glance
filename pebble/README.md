# pebble

Native **C** watch app for Health Glance — minimal 4-card deck on Pebble Time 2 (Emery).

**UI:** Steps · Sleep · Heart · Workout. Big packed-glyph hero digits (the original Alloy fillRect font, ported to C) + Gothic Bold 24/18. Shared title/hero/unit slots (no side breadcrumbs).

Stats come from the Android companion (`../android`) via PebbleKit AppMessages. There is no PKJS.

On open, the watch paints last-known stats if a companion push already landed, then requests a fresh push from the phone. Phone WorkManager keeps the companion cache warm; there is no watch-side background worker.

This replaced the Alloy/JS build (`src/embeddedjs/*`, Poco, pebbleproxy): XS + Poco shared Emery's 128 KB with the framebuffer, so two-card blits OOM'd. The C build uses 6.7 KB of the 128 KB budget with 124 KB of heap free.

## Building & running

```sh
pebble build                             # live build (waits for the phone)
HG_DEMO=1 pebble build                   # fixture numbers for emulator QA
pebble install --emulator emery
pebble install --phone <ip>               # live Health Connect data via companion
```

`HG_DEMO` is a build-time define (`wscript` → `-DHG_DEMO=1`). Set it to `1` to seed the sample numbers from `stats_init()` and skip the `STATS_REQ` push; leave it unset for real data.

### Emulator QA

`pebble screenshot` is flaky on this SDK, so the scripts screendump the QEMU monitor instead.

```sh
./scripts/emu-check.sh /tmp/check.png            # build + install + flatness check
./scripts/emu-capture-deck.sh ../docs/images     # all four cards, pushes live stats first
./scripts/emu-send-stats.py                      # stand-in for the companion (22 keys)
./scripts/emu-anim-probe.py up                   # rapid frames across a deck push
```

`emu-*-probe/send-stats/launch-app.py` need the pebble-tool python:
`~/.local/share/uv/tools/pebble-tool/bin/python`.

## Navigation (Up quick-launch)

Watchface **Up** opens the app on Steps. Keep pressing **Up** to page forward. **Down on Steps** exits.

| Button | On Steps | On other cards |
|--------|----------|----------------|
| Up | → Sleep | Next card (stops on Workout) |
| Down | Exit → watchface | Previous card |
| Back | Exit | Exit |

Deck: Steps → Sleep → Heart → Workout.

## Animation

Full Deck Push + Seam: two full-screen `Layer`s whose `update_proc` paints card *n* from `g_stats`. On a page turn the incoming layer starts fully off-screen and both slide inside one `PropertyAnimation` (170 ms, ease-in-out), with a 4 px black seam layer riding the join. Nothing is snapshotted per frame — the layers composite.

## Layout

```
src/c/main.c      window, clicks, AppMessage inbox/outbox, deck state
src/c/cards.c     the 4 cards (cap, pips, hero, bar, meta, footer)
src/c/stats.c     Stats struct, fixture numbers, clock/footer formatting
src/c/anim.c      Full Deck Push + Seam (two layers + PropertyAnimation)
scripts/          emulator QA helpers
wscript           build (adds -DHG_DEMO)
package.json      UUID, companionApp package, messageKeys
```

Pixel layout (200×228): cap 36 px; hero y=44 (glyph top 46, 36 px advance), unit y=106, bar y=136 h=8, meta y=178, footer y=202, inset x=10. Cap colours are exact 64-palette matches to the old RGB: JaegerGreen / CobaltBlue / ImperialPurple / Orange.

## Contract with the companion (do not change)

| Field | Value |
|-------|-------|
| UUID | `ced3f81d-aaae-4ee4-a49c-885a247644ec` |
| `messageKeys` | `STATS_REQ`=10000 … `HRV_MS`=10040, **append-only** |
| Inbox / outbox | 512 / 64 |
| Companion package | `com.jollybully.healthglance` |

The key numbers are generated from `package.json` into `build/include/message_keys.auto.h` as `uint32_t` externs — they are **not** compile-time constants, so the inbox resolver uses `if/else` chains, not `switch`.

## Docs

https://developer.repebble.com
