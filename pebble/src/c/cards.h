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

/* PebbleOS-style warp: the card's layout and cap stretch then squash as it
 * arrives, so a page turn feels rubbery rather than rigid. `target` is redrawn
 * on every frame; at rest every warped coordinate is an identity. */
void cards_warp_start(Layer *target);
void cards_warp_cancel(void);
