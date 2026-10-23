#pragma once
#include <pebble.h>

/* Full Deck Push: two full-screen card layers slide past each other inside one
 * Animation, with a 4px black seam riding the join. dir > 0 pushes up
 * (next card enters from the bottom), dir < 0 pushes down. */

typedef void (*AnimDone)(void *ctx);

bool anim_busy(void);
void anim_push(Layer *incoming, Layer *outgoing, Layer *seam, int dir, int w, int h,
               AnimDone done, void *ctx);
/* Put the incoming card back where a drag pulled it from (drag did not commit). */
void anim_retreat(Layer *incoming, Layer *outgoing, Layer *seam, int dir, int w, int h,
                  AnimDone done, void *ctx);
void anim_cancel(void);

/* End-of-deck spring: the card leans away from the gesture, overshoots slightly
 * back the other way, then settles. Used at both ends of the list, where there is
 * no next card to move to but the press must still feel answered. */
void anim_spring(Layer *l, int w, int h, int overshoot, AnimDone done, void *ctx);

bool anim_spring_busy(void);

/* Leaving the app: the card rises back up the way it arrived, so a quick launch
 * feels like a place you went into rather than a screen that vanished. */
void anim_exit(Layer *l, int w, int h, AnimDone done, void *ctx);
