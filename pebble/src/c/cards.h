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
  int lag; /* content displacement in px: how far the card's contents trail its
            * own frame, pinned at the cap. 0 is at rest. */
} CardLayer;

/* Draw a card whose contents are displaced by `lag` pixels, applied
 * proportionally to distance from the cap: the header stays bolted to the card
 * and the bottom trails furthest, which stretches the sheet instead of scaling
 * it. Rows that are pulled apart narrow by the same ratio. */
void cards_draw_warp(GContext *ctx, GRect bounds, int card, int lag);

/* PebbleOS-style warp: the card's layout and cap stretch then squash as it
 * arrives, so a page turn feels rubbery rather than rigid. `target` is redrawn
 * on every frame; at rest every warped coordinate is an identity. */
/* Draw the deck's blocky hero digits at an arbitrary y (used by detail screens so
 * they share the same typographic voice as the cards). */
void cards_hero_text(GContext *ctx, const char *text, int y);

void cards_warp_start(Layer *target);
void cards_warp_cancel(void);
