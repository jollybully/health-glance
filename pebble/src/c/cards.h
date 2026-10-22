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

/* Content animates in: bars/stages grow from the left and the active pip
 * blooms, so a card lands filled rather than snapping. `target` is redrawn. */
void cards_reveal(Layer *target);
void cards_cancel_reveal(void);
