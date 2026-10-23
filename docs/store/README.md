# Pebble appstore submission — Health Glance 1.0.1

> **LIVE:** <https://apps.repebble.com/c74e59961a004ec5ab8c72bd> (app id `c74e59961a004ec5ab8c72bd`,
> UUID `ced3f81d-aaae-4ee4-a49c-885a247644ec`). Published 2026-10-23 with the CLI, listed, emery only.
> Two follow-ups live in the dashboard, not here: the developer display name renders as **Unknown**
> (account profile), and the category cannot be changed once created — it was submitted as `health`.

Everything the listing needs, in the order the Developer Dashboard asks for it.
Source: <https://developer.repebble.com/guides/appstore-publishing/preparing-a-submission/>

## The .pbw

`health-glance-c.pbw` — 22,089 bytes (limit is 4.4 MB).

| Field | Value |
| --- | --- |
| Type | Watchapp (`watchapp.watchface` is false) |
| UUID | `ced3f81d-aaae-4ee4-a49c-885a247644ec` |
| Version | `1.0.1` (numbers and dots only; never reuse a version) |
| Target platforms | `emery` only — the listing will only be offered to Pebble Time 2 |

## Listing fields

- **App name:** Health Glance
- **Category:** Health & Fitness
- **Website URL:** https://github.com/jollybully/health-glance
- **Source code URL:** https://github.com/jollybully/health-glance
- **Visibility:** Listed
- **Companion app:** *Android only, sideloaded — no Play Store listing, so leave this blank.* Naming it
  without a store URL would promise a download that does not exist.

### Description (paste as plain text)

> Four numbers from your phone's Health Connect, on your wrist, at a glance: steps, sleep, heart
> rate and your last workout. Swipe or use the buttons to move between the cards.
>
> Health Glance is deliberately small. There are no charts to scroll, no days to page back
> through and no settings on the watch. Each card carries one number you can read mid-stride,
> with the detail that fits underneath it.
>
> Your step goal is set in the companion app, because Health Connect stores steps but has no
> concept of a target.
>
> If you have a chest strap or a watch that broadcasts the standard Bluetooth heart-rate service,
> the Heart card shows your live pulse, and the edge of the screen beats with it. The reading
> comes from the phone, which is the only device in the chain that can talk to a strap.
>
> Requires the Health Glance companion app for Android (sideloaded from the project's releases
> page). The companion reads Health Connect with read-only permissions: nine READ permissions,
> zero WRITE. Your data goes from your phone to your watch and nowhere else — there is no account,
> no server and no analytics.
>
> Health Connect needs Android 8 or newer; it is built into Android 14 and installs as an app on
> Android 13 and below.
>
> Notes: watch app is native C for the Pebble Time 2 (Emery), 22 KB. Workouts shows the most
> recent session only.

### Release notes (per release)

> The step goal is now yours to set in the companion app, and the ends of the deck answer back.
>
> - Daily step goal is a setting: a number field in the companion, clamped 500-100,000, pushed to
>   the watch on save and cached there.
> - Down on Steps goes home, the way a quick-launched app should be left.
> - Pressing Up on Workouts, or dragging past either end, leans the card and settles it instead of
>   going dead.
>
> Upgrading from 1.0.0: install both halves. The watch cache moved to v2 to hold the goal, so the
> first launch shows "no data" until the companion pushes once.

## Images

| File | Size | Slot |
| --- | --- | --- |
| `shots/emery_steps.png` | 200×228 | Screenshot 1 of 5 |
| `shots/emery_sleep.png` | 200×228 | Screenshot 2 |
| `shots/emery_heart.png` | 200×228 | Screenshot 3 |
| `shots/emery_workouts.png` | 200×228 | Screenshot 4 |
| `icon-small-80.png` | 80×80 | Small icon |
| `icon-large-144.png` | 144×144 | Large icon |
| `banner-720x320.png` | 720×320 | Banner (optional) |

Screenshots are raw emulator frames at the exact emery display size, with no watch outline — the
spec explicitly disallows framing them.

## Two ways to publish

**Dashboard** (only place a listing can be edited afterwards): sign in at
<https://developer.repebble.com>, click **New**, upload the `.pbw`, then fill the fields above and
upload the images. No review queue: it is live on submit.

**Command line** — needs a Pebble account sign-in first, which is why this step is the owner's.
Pass `--category health`, not "Health & Fitness": the API only accepts `daily|tools|notifications|remotes|health|games`
and answers a 500 for anything else, which is how the first six attempts failed.

```bash
cd pebble
pebble login                # opens a browser; Google, GitHub or Apple
pebble publish --non-interactive \
  --name "Health Glance" \
  --description "$(cat ../docs/store/description.txt)" \
  --category health \
  --source https://github.com/jollybully/health-glance \
  --version 1.0.1 \
  --icon-small ../docs/store/icon-small-80.png \
  --icon-large ../docs/store/icon-large-144.png \
  --screenshots ../docs/store/shots/emery_steps.png ../docs/store/shots/emery_sleep.png \
                ../docs/store/shots/emery_heart.png ../docs/store/shots/emery_workouts.png \
  --release-notes "$(cat ../docs/store/release-notes.txt)"
```

For CI, replace the stored session with `PEBBLE_FIREBASE_ID_TOKEN`.

## Deliberately not done

- **Other platforms.** The layout is built around 200×228; adding `basalt`/`diorite` would ship a
  broken layout to 144×168 watches rather than a smaller one.
- **Companion app field.** Without a Play Store listing it can only mislead.
- **Rebble store** (`dev-portal.rebble.io`) — a separate submission with the same assets, worth
  doing if the Core listing goes well.
