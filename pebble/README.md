# pebble

Native **C** watch app for Health Glance — minimal 4-card deck on Pebble Time 2 (Emery).

**UI:** Steps · Sleep · Heart · Workouts. Big packed-glyph hero digits (the original Alloy fillRect font,
ported to C) + Gothic Bold 24/18. Shared title/hero/unit slots.

Four cards and nothing else: no drill-down screens, no charts, no day-by-day history. Select is
inert on the deck, Up/Down page the cards, Back exits. The Heart card carries a pulsing frame that
beats in lub-dub at the strap's measured rate and stops itself when samples go stale.

Stats come from the Android companion (`../android`) via PebbleKit AppMessages. There is no PKJS.

On open, the watch paints the **last known stats from flash** (so the deck is never empty out of phone range), then asks for a fresh push and replaces them. Phone WorkManager keeps the companion cache warm; there is no watch-side background worker.

This replaced the Alloy/JS build (`src/embeddedjs/*`, Poco, pebbleproxy): XS + Poco shared Emery's 128 KB with the framebuffer, so two-card blits OOM'd. The C build uses 12 KB of code and under 1 KB of static RAM.

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

## Caching

`stats_save_cache()` writes the last pushed snapshot to flash (`persist_write_data`, key 1,
~120 bytes) and `stats_load_cache()` paints it on a cold start. The footer distinguishes the
two states: `cached HH:MM` while showing the restored snapshot, `synced HH:MM` once a live push
lands. Writes are skipped when nothing changed, so a phone that keeps pushing the same numbers
does not wear the flash, and an `HG_DEMO=1` build never writes fixtures into the cache.

## Navigation (Up quick-launch)

Watchface **Up** opens the app on Steps. Keep pressing **Up** to page forward. **Down on Steps** exits.

| Button | On Steps | On other cards |
|--------|----------|----------------|
| Up | → Sleep | Next card (stops on Workout) |
| Down | Exit → watchface | Previous card |
| Back | Exit | Exit |

Deck: Steps → Sleep → Heart → Workout.

## Animation

Motion is deliberately PebbleOS-flat — no bounce, no overshoot.

**Launch:** `launch_anim_start()` slides the deck up from the bottom edge (220 ms `EaseOut`), so
a quick launch reads like an app being pulled up over the watchface. Any press during the slide
cancels it and snaps the card home.

**Page turn:** Full Deck Push + Seam — two full-screen `Layer`s whose `update_proc` paints card
*n* from `g_stats`, sliding 165 ms `EaseOut` with a 4 px black seam riding the join (off-screen at
both ends of the travel). Nothing is snapshotted per frame; the layers composite.

**Numbers warp:** the hero glyph block stretches ~1.29× vertically and eases back to its resting
shape over 240 ms (monotonic ease-out), pivoting on the digits' own centre. Only the numbers
move — the cap, labels and bars stay put, which is what keeps it feeling native rather than
jelly. At rest the warp is 1000/1000, so a settled card is pixel-identical to the Alloy build.

> Do not use `animation_set_custom_curve()` for overshoot: a curve that returns more than
> `ANIMATION_NORMALIZED_MAX` blanks and kills the app on SDK 4.17. Overshoot, if ever wanted,
> has to be a second animation with stock curves.

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
