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
bool detail_down(void);

void detail_redraw(void);
void detail_draw(GContext *ctx, GRect bounds, int card);

/* AppMessage byte-array series, all optional: a screen falls back to the card's
 * own numbers when its series has not arrived. */
void detail_set_series(int card, const uint8_t *data, int len);
void detail_set_workouts(const uint8_t *data, int len);
void detail_set_day(int day_offset);

#endif
