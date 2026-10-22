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
