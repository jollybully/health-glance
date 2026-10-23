#ifndef HG_DETAIL_H
#define HG_DETAIL_H

#include <pebble.h>

/* Drill-down screens. One is shown at a time over the deck, opened with Select
 * from a card and closed with Back.
 *
 * Up/Down cycle inside a screen where there is more than one thing to see:
 *   Workouts  through the last seven sessions
 *   Heart     between today's graph and the live strap value (when fresh)
 * Steps and Sleep cycle a day once the companion sends a per-day series. */

void detail_init(Layer *parent, GRect bounds);
void detail_deinit(void);

bool detail_is_open(void);
int detail_card(void);
void detail_open(int card);
void detail_close(void);

/* true when the press was consumed (so the deck must not act on it) */
bool detail_up(void);
/* Select inside an open detail (Heart: switch live/graph). */
void detail_select(void);
bool detail_down(void);

void detail_redraw(void);
void detail_draw(GContext *ctx, GRect bounds, int card);

/* AppMessage byte-array series, all optional: a screen falls back to the card's
 * own numbers when its series has not arrived. */
void detail_set_series(int card, const uint8_t *data, int len);
void detail_set_workouts(const uint8_t *data, int len);
void detail_set_day(int day_offset);
/* 0 steps, 1 asleep, 2 deep, 3 rem, 4 hr_min, 5 hr_max for the day on screen. */
void detail_set_day_total(int which, int value);
void detail_set_request_day(void (*fn)(int day));
bool detail_day_pending(void);
int detail_day(void);

/* Do we hold any chart data, and does it match what was last written to flash? */
bool detail_has_series(void);
bool detail_series_dirty(void);
void detail_series_mark_saved(void);

/* The charts are five byte arrays, so they persist as their own blob rather than
 * riding along with the scalars. */
typedef struct {
  uint8_t magic_ok;
  uint8_t have[4];
  uint8_t steps[24], sleep[24], heart[24], week[7];
  uint8_t wo_len;
  uint8_t wo[42];
} SeriesBlob;

void detail_export_series(SeriesBlob *out);
void detail_import_series(const SeriesBlob *in);

#endif
