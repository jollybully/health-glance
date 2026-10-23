#pragma once
#include <pebble.h>
#include "stats.h"

#define CARD_STEPS 0
#define CARD_SLEEP 1
#define CARD_HEART 2
#define CARD_WORKOUT 3
#define CARD_COUNT 4

void cards_init(void);

/* Paints one card, pixel-laid-out like draw() in the old Alloy main.js:
 * cap 36px, hero y=44, unit y=108, bar y=136 h=8, meta y=180, footer y=204. */
void cards_draw(GContext *ctx, GRect bounds, int card);

/* Per-card layer state. A card carries its own squash/stretch factors so the
 * incoming and outgoing cards can deform in opposite directions during a page
 * turn: 1000 is identity, so a settled card is pixel-identical to before. */
typedef struct {
  int card;
} CardLayer;

/* Draws a card into whatever band its layer currently occupies. At full height
 * (200x228) every mapping is the identity; at pin height only the cap colour
 * remains. The compression narrows what it squeezes, so the pin has a shape
 * rather than being a squashed photograph. */
void cards_draw_band(GContext *ctx, GRect bounds, int card);

/* PebbleOS-style warp: the card's layout and cap stretch then squash as it
 * arrives, so a page turn feels rubbery rather than rigid. `target` is redrawn
 * on every frame; at rest every warped coordinate is an identity. */
/* Draw the deck's blocky hero digits at an arbitrary y (used by detail screens so
 * they share the same typographic voice as the cards). */
void cards_hero_text(GContext *ctx, const char *text, int y);

void cards_warp_start(Layer *target);
void cards_warp_cancel(void);
