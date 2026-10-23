#pragma once
#include <pebble.h>

/* The deck's only transition: a pin unfolds into a card, and a card folds back
 * into a pin. See anim.c for why it is shaped that way. */

typedef void (*AnimDone)(void *ctx);

bool anim_busy(void);
int anim_pin_h(void);

/* Live drag: place the incoming card's band at an arbitrary height (pin..full). */
void anim_set_band(Layer *incoming, int band, int w, int h);

/* The deck move: fold the current card up into its header, then unroll the next
 * one out of that header. Nothing overlaps, and the pin is a visible handover. */
void anim_transition(Layer *incoming, Layer *outgoing, int w, int h, AnimDone done, void *ctx);

/* Grow the incoming card from its current band to a full card, overshoot, settle. */
void anim_unfold(Layer *incoming, int w, int h, AnimDone done, void *ctx);

/* Collapse the incoming card back into its pin and hide it. */
void anim_refold(Layer *incoming, int w, int h, AnimDone done, void *ctx);

void anim_cancel(void);
