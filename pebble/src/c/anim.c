#include "anim.h"
#include "cards.h"

/* ---------------- unfold ----------------
 *
 * One mechanic for every move in the deck: a card is folded into a pin, and a pin
 * unfolds into a card.
 *
 * That is how PebbleOS thinks about timeline items. A pin is a strip that carries
 * an icon and a colour; opening it grows the strip into a card. No full-screen
 * rectangle ever slides past another one, and the distortion falls out of the
 * geometry rather than being applied on top of it: the pin keeps a floor height
 * while everything below it is squeezed away, and rows that are squeezed narrow as
 * they go (see the band mapping in cards.c).
 *
 * The unfold is anchored at the top edge, so the incoming card's header appears
 * first, exactly where the outgoing card's header was. The header is continuous
 * across the transition; only the body unrolls underneath it.
 *
 *   pinch drag : the band height follows the finger, so the release point is a
 *                position the user chose rather than a decision the animation makes
 *   commit     : the band grows to full height, overshoots slightly, and sets down
 *   abandon    : the band falls back to the pin and is hidden
 */

#ifndef UNPIN_MS_OVERRIDE
#define UNPIN_MS_OVERRIDE 0
#endif
#define PIN_H 7
#ifndef UNPIN_MS_OVERRIDE
#define UNPIN_MS_OVERRIDE 0
#endif
/* Fold the current card away first, then unroll the next one into the same
 * header. Sequential rather than simultaneous: overlapping the two put one card's
 * content behind the other's and read as ghosting, and the pin only registers as
 * a state if it is held for a beat at the handover. */
#define FOLD_MS (UNPIN_MS_OVERRIDE ? UNPIN_MS_OVERRIDE / 2 : 110)
#define UNPIN_MS (UNPIN_MS_OVERRIDE ? UNPIN_MS_OVERRIDE : 170)
#define OVERSHOOT_PM 1018 /* 1.8% past full height, then back */
#define SETTLE_MS 60
#define REFOLD_MS 120

int anim_pin_h(void) { return PIN_H; }

typedef struct {
  Layer *l;        /* the layer being animated this stage */
  Layer *l_in;     /* incoming card (unfolds in stage two) */
  Layer *l_out;    /* current card (folds away in stage one) */
  Animation *stage;
  AnimDone done;
  void *ctx;
  int w, h;
  bool refold;
  bool transition;
  bool cancelled;
} Fold;

static Fold s_fold;
static bool s_busy;

bool anim_busy(void) { return s_busy; }

static Animation *frame_anim(Layer *l, GRect to, int ms, AnimationCurve curve) {
  PropertyAnimation *pa = property_animation_create_layer_frame(l, NULL, &to);
  Animation *a = property_animation_get_animation(pa);
  animation_set_duration(a, ms);
  animation_set_curve(a, curve);
  return a;
}

static int clamp_band(int band, int h) {
  if (band < PIN_H) return PIN_H;
  if (band > h) return h;
  return band;
}

/* Used live by the drag: put the incoming card's band at an arbitrary height. */
void anim_set_band(Layer *incoming, int band, int w, int h) {
  if (!incoming) return;
  band = clamp_band(band, h);
  layer_set_frame(incoming, GRect(0, 0, w, band));
  layer_set_hidden(incoming, false);
  layer_mark_dirty(incoming);
}

static void finish(void) {
  s_fold.stage = NULL;
  s_busy = false;
  if (s_fold.done) s_fold.done(s_fold.ctx);
}

static void settle_stopped(Animation *a, bool finished, void *ctx) {
  (void)a;
  (void)finished;
  (void)ctx;
  if (s_fold.cancelled) return;
  layer_set_frame(s_fold.l, GRect(0, 0, s_fold.w, s_fold.h));
  layer_mark_dirty(s_fold.l);
  finish();
}

static void unpin_stopped(Animation *a, bool finished, void *ctx) {
  (void)a;
  (void)finished;
  (void)ctx;
  if (s_fold.cancelled) return;
  s_fold.stage = NULL;

  if (s_fold.refold) {
    layer_set_hidden(s_fold.l, true);
    layer_set_frame(s_fold.l, GRect(0, 0, s_fold.w, PIN_H));
    finish();
    return;
  }

  /* Overshoot past full height, then set back down: the card arrives rather than
   * simply stopping, which is the same settle a physical card would make. */
  const int over = (s_fold.h * OVERSHOOT_PM) / 1000;
  s_fold.stage = frame_anim(s_fold.l, GRect(0, 0, s_fold.w, over), SETTLE_MS,
                            AnimationCurveEaseInOut);
  animation_set_handlers(s_fold.stage, (AnimationHandlers){.stopped = settle_stopped}, NULL);
  animation_schedule(s_fold.stage);
}

static void fold_stopped(Animation *a, bool finished, void *ctx);

/* The full deck move: the current card folds up into its header, then the next
 * one unrolls out of the header that is left behind. */
void anim_transition(Layer *incoming, Layer *outgoing, int w, int h, AnimDone done, void *ctx) {
  if (!incoming || !outgoing) return;
  s_fold.l_in = incoming;
  s_fold.l_out = outgoing;
  s_fold.w = w;
  s_fold.h = h;
  s_fold.done = done;
  s_fold.ctx = ctx;
  s_fold.refold = false;
  s_fold.transition = true;
  s_fold.cancelled = false;
  s_busy = true;

  /* The next card is already full height underneath: it is what the folding card
   * uncovers. Folding both at once left the glass empty for a few frames, which
   * read as the app breaking rather than as a transition. */
  layer_set_hidden(incoming, false);
  layer_set_frame(incoming, GRect(0, 0, w, h));
  layer_mark_dirty(incoming);
  layer_insert_above_sibling(outgoing, incoming);

  /* The card folds DOWNWARD into a pin at the bottom edge. Folding toward the top
   * shrank the band from its bottom edge, so the next card was uncovered from
   * below and briefly appeared as a body with no header — it looked headless.
   * Tucking the old card downward uncovers the new one from its header down. */
  s_fold.stage = frame_anim(outgoing, GRect(0, h - PIN_H, w, PIN_H), FOLD_MS,
                            AnimationCurveEaseIn);
  animation_set_handlers(s_fold.stage, (AnimationHandlers){.stopped = fold_stopped}, NULL);
  animation_schedule(s_fold.stage);
}

static void fold_stopped(Animation *a, bool finished, void *ctx) {
  (void)a; (void)finished; (void)ctx;
  if (s_fold.cancelled) return;
  s_fold.stage = NULL;
  /* The old card is a strip; the new one is already filling the glass behind it. */
  layer_set_hidden(s_fold.l_out, true);
  layer_set_frame(s_fold.l_out, GRect(0, 0, s_fold.w, s_fold.h));
  layer_insert_above_sibling(s_fold.l_in, s_fold.l_out);
  layer_set_frame(s_fold.l_in, GRect(0, 0, s_fold.w, s_fold.h));
  layer_mark_dirty(s_fold.l_in);
  finish();
}

void anim_unfold(Layer *incoming, int w, int h, AnimDone done, void *ctx) {
  if (!incoming) return;
  s_fold.l = incoming;
  s_fold.w = w;
  s_fold.h = h;
  s_fold.done = done;
  s_fold.ctx = ctx;
  s_fold.refold = false;
  s_fold.cancelled = false;
  s_busy = true;

  layer_set_hidden(incoming, false);
  s_fold.stage = frame_anim(incoming, GRect(0, 0, w, h), UNPIN_MS, AnimationCurveEaseOut);
  animation_set_handlers(s_fold.stage, (AnimationHandlers){.stopped = unpin_stopped}, NULL);
  animation_schedule(s_fold.stage);
}

void anim_refold(Layer *incoming, int w, int h, AnimDone done, void *ctx) {
  if (!incoming) return;
  s_fold.l = incoming;
  s_fold.w = w;
  s_fold.h = h;
  s_fold.done = done;
  s_fold.ctx = ctx;
  s_fold.refold = true;
  s_fold.cancelled = false;
  s_busy = true;

  s_fold.stage = frame_anim(incoming, GRect(0, 0, w, PIN_H), REFOLD_MS, AnimationCurveEaseIn);
  animation_set_handlers(s_fold.stage, (AnimationHandlers){.stopped = unpin_stopped}, NULL);
  animation_schedule(s_fold.stage);
}

void anim_cancel(void) {
  s_fold.cancelled = true;
  if (s_fold.stage) {
    animation_unschedule(s_fold.stage);
    s_fold.stage = NULL;
  }
  if (s_fold.l) {
    layer_set_hidden(s_fold.l, true);
    layer_set_frame(s_fold.l, GRect(0, 0, s_fold.w, PIN_H));
    layer_mark_dirty(s_fold.l);
  }
  s_fold.l = NULL;
  s_busy = false;
}
