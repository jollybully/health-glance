# Health Glance: the journey, and the button language

Written after the watch and phone builds worked. This is the review I owed: every
screen, every state, then the button grammar distilled into one rule set.

## 1. The journey, screen by screen

### Watch — deck (the four cards)

| Where | What you see | Up | Down | Select | Back |
| --- | --- | --- | --- | --- | --- |
| Steps card | hero steps, "steps today", progress bar, "7.1 km · 2508 cal", "synced 09:41" | next card | previous card; **on Steps = exits the app** | open Steps detail | exit app |
| Sleep card | hero asleep, stage bar, "23:47 → 06:29" | next card | previous card | open Sleep detail | exit app |
| Heart card | hero resting bpm, position bar, min/max; with a strap: hero live bpm, "live bpm", "live 131 · min · max" | next card | previous card | open Heart detail | exit app |
| Workouts card | hero duration, zone bar, "4.3 km · 137 avg · 297 cal" | next card | previous card | open Workouts detail | exit app |

Deck state additions: a purple pulse frame around the edge while a strap streams;
pips in the cap showing which of the four cards you are on; footer reads `synced`
after a push, `cached` after a cold start.

### Watch — detail screens

Three cues say "you are one level in": a `<` before the title, a 2px black rule
under the cap, and the progress bar divided into slots.

| Screen | Cap right | Hero | Sub-labels | Chart | Meta | Up | Down | Select |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Steps | today / yesterday / N days ago | day total (past) or peak hour (today) | "steps by hour" / "0-23h" | 24 hourly bars, peak black, average rule | "7.1 km · 2508 cal" (today) or "peak 500 at 12-13" (past) | newer day | older day | nothing |
| Sleep | last night / night before / N nights ago | asleep total (past) or deep minutes (last night) | "sleep stages" / dominant stage | 24 stage bars | window · rem, or "deep 1:01 · rem 1:28" (past) | newer night | older night | nothing |
| Heart | live / today / yesterday / N days ago | live bpm, or peak bpm | "live bpm" + battery, or "peak bpm" + hour | hourly bars (graph) or a min-max gauge (live) | min/max | newer day | older day (leaves live) | toggle live ↔ graph |
| Workouts | N recent | session duration | type · date, and "2 of 7" | one bar per session, selected black with a block on the rail | "6.1 km · 148 avg hr" | newer session | older session | nothing |

States: `loading` while a day request is in flight (retried twice), `no data` +
`phone sent nothing` when the phone answers with nothing, `no series` when a
chart never arrived.

### Phone — one scrolling page

Header → privacy blurb → Health Connect availability → read/write permissions →
`Grant Health Connect permissions` → `Sync & push to watch` → `Remove demo data
this app wrote` → *Live heart rate* section: state line, strap instructions,
`Connect heart-rate strap` / `Stop live heart rate` → today's numbers → **Last
crash** panel → **Live HR trail** panel → `Charts:` line → `Day cycling:` line.

## 2. What the buttons mean today, and where it breaks

The instinct behind the current mapping is sound: Up/Down move through whatever
list you are looking at, Select acts, Back goes up a level. Four things break it.

1. **Down on the first card exits the app.** Everywhere else Down means "further
   along the list". On Steps it silently leaves. That is the single most
   inconsistent control in the app.
2. **Select does nothing on three of the four detail screens.** It is the only
   "act" button, so a dead Select reads as a broken app rather than a screen with
   no action.
3. **`live ^` uses the Up arrow to mean "Select".** After the direction swap, `^`
   means "newer", so the hint now points the wrong way.
4. **The sub-label row means something different on each screen**: an axis
   legend (Steps), a stage name (Sleep), an hour (Heart), a counter (Workouts).
   Same position, four meanings — this is why the screens take a beat to read.

## 3. The distilled language

> **Up moves towards the top of the current list. Down moves towards the bottom.
> Select is the action for what is on screen. Back goes up exactly one level, and
> is the only way to leave.**

Applied:

- Deck is a list of four cards, newest-first convention irrelevant: Up = previous
  card, Down = next card. **Remove the Down-to-exit**; Back exits.
- A detail screen is a list of days (or sessions) with today at the top: **Up =
  newer, Down = older**. Already true after the swap; now it is a rule rather than
  a preference.
- Select has one job per screen and there is only ever one job:
  - deck: open the detail
  - Heart detail: switch between the strap and today's graph
  - Steps / Sleep / Workouts detail: nothing, and nothing is advertised
- Back: detail → deck → exit. Never a surprise.

## 4. Making the language visible

Rules nobody can see are not rules. Cheap signals, in order of value:

1. **A hint where Select does something unusual.** On the Heart detail, replace
   `live ^` with `SEL live` (or `SEL graph` when live is showing) so the button is
   named, not implied.
2. **Give the sub-label row one meaning: "what you are looking at".**
   - Steps: `steps by hour` / `0-23h` (done)
   - Sleep: `stages over the night` / `00:12-06:45`
   - Heart: `bpm by hour` / `0-23h`
   - Workouts: `length by session` / `1-7`, with the date moved to the meta line
3. **Say when you are at an edge.** At today, Up does nothing; at the oldest day,
   Down does nothing. The cap already states the day, which is enough, provided
   the hero does not change underneath it.
4. **Keep the three "you are inside" cues** (chevron, page rule, divided rail).
   They are the only thing distinguishing depth without words.

## 5. Order of work

1. Remove Down-to-exit; Back becomes the only exit. *(one function, no risk)*
2. Rename `live ^` to `SEL live` / `SEL graph`. *(one string)*
3. Unify the sub-label row across all four details. *(four screens, mostly strings)*
4. Move the phone's diagnostics (Last crash, Live HR trail, Charts, Day cycling)
   behind a "Diagnostics" disclosure so a Reddit user sees a health app, not a
   debug console.
5. Consider moving the strap instructions into a first-run card rather than a
   permanent paragraph above the button.

Not proposed, deliberately: gestures, animations on the detail screens, or more
buttons. The deck already carries the personality; the detail screens should stay
quiet and predictable.
