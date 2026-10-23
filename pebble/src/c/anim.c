#include "anim.h"
#include "cards.h"

/* Page movement, PebbleOS-flat: one animation per layer with a plain ease-out.
 *  - anim_push():    commit a page turn (button, or a drag that travelled far enough)
 *  - anim_retreat(): the drag was too short, so put the incoming card back where
 *                    it came from without changing the current page.
 * Both animate from the layer's CURRENT frame, so a drag hands off straight into
 * the settle animation with no jump. */

#ifndef ANIM_MS
#define ANIM_MS 190 /* classic Pebble pace: quick enough to feel instant, long
                     * enough for the eye to read the deformation */
#endif
#define TRAVEL_MS ANIM_MS
#define SETTLE_MS 200

/* The card's contents trail its frame by `lag` px, distributed down the card by
 * the warp field in cards.c. Positive lag drags the content down, negative pulls
 * it up; zero is at rest. This is the timeline's trick — the contents move on
 * their own curve rather than being scaled with the card. */
static void lag_apply(Layer *l, int lag) {
  if (!l) return;
  CardLayer *d = (CardLayer *)layer_get_data(l);
  if (!d) return;
  d->lag = lag;
  layer_mark_dirty(l);
}

static void lag_reset(Layer *l) {
  lag_apply(l, 0);
}

typedef struct {
  Layer *l_in, *l_out, *l_seam;
  Animation *a_in, *a_out, *a_seam; /* current stage only */
  Animation *a_squash;              /* deformation, runs alongside the travel */
  AnimDone done;
  void *ctx;
  int w, h, dir;
  bool cancelled;
  bool retreat;
} Push;

static Push s_push;
static bool s_busy;

bool anim_busy(void) { return s_busy; }

static Animation *frame_anim(Layer *l, GRect to, int duration_ms, AnimationCurve curve) {
  PropertyAnimation *pa = property_animation_create_layer_frame(l, NULL, &to);
  Animation *a = property_animation_get_animation(pa);
  animation_set_duration(a, duration_ms);
  animation_set_curve(a, curve);
  return a;
}

static void finish(void) {
  /* Do NOT unschedule the siblings of a finished animation: Pebble auto-destroys
   * them, and touching the freed pointers left anim_busy() stuck true, which
   * silently swallowed every later button press. */
  layer_set_hidden(s_push.l_seam, true);
  layer_set_frame(s_push.l_seam, GRect(0, -4, s_push.w, 4));

  if (s_push.retreat) {
    /* incoming goes back off-screen; the current card never moved out of place */
    layer_set_frame(s_push.l_in, GRect(0, s_push.dir > 0 ? s_push.h : -s_push.h, s_push.w,
                                      s_push.h));
    layer_set_hidden(s_push.l_in, true);
    layer_set_frame(s_push.l_out, GRect(0, 0, s_push.w, s_push.h));
    layer_set_hidden(s_push.l_out, false);
  } else {
    layer_set_hidden(s_push.l_out, true);
    layer_set_frame(s_push.l_out, GRect(0, 0, s_push.w, s_push.h));
  }

  s_push.a_in = s_push.a_out = s_push.a_seam = NULL;
  s_busy = false;
  if (s_push.done) s_push.done(s_push.ctx);
}

static void travel_stopped(Animation *a, bool finished_travel, void *ctx) {
  (void)a;
  (void)finished_travel;
  (void)ctx;
  if (s_push.cancelled) return;
  s_push.a_in = s_push.a_out = s_push.a_seam = NULL;
  finish();
}

/* How far the contents trail, as a fraction of LAG_PX, over the life of a page
 * turn (p in 0..1000). It snaps out to full lag almost immediately — the card
 * leaves its content behind — then the content catches up, overshoots past rest
 * in the other direction, and settles. The overshoot is what makes it read as
 * elastic instead of as a slide. */
#define LAG_PX 34

static int lag_shape(int p) {
  int f; /* per mille of LAG_PX, may be negative */
  if (p < 200) {
    f = (p * 1000) / 200;
  } else if (p < 600) {
    f = 1000 - ((p - 200) * 500) / 400;
  } else if (p < 800) {
    f = 500 - ((p - 600) * 750) / 200; /* crosses zero: the overshoot */
  } else {
    f = -250 + ((p - 800) * 250) / 200;
  }
  return (LAG_PX * f) / 1000;
}

static void squash_update(Animation *a, const AnimationProgress progress) {
  (void)a;
  const int p = (int)(((int64_t)progress * 1000) / ANIMATION_NORMALIZED_MAX);
  /* Both cards trail the same way: one continuous sheet being pulled through the
   * frame, rather than two independent objects. */
  const int sign = s_push.dir > 0 ? 1 : -1;
  const int lag = sign * lag_shape(p);
  if (s_push.retreat) {
    /* the drag did not commit, so the content unfurls back with the card */
    lag_apply(s_push.l_in, (lag * (1000 - p)) / 1000);
    lag_apply(s_push.l_out, 0);
    return;
  }
  lag_apply(s_push.l_in, lag);
  lag_apply(s_push.l_out, (lag * 3) / 4); /* the departing card trails a little less */
}

static void squash_stopped(Animation *a, bool finished, void *ctx) {
  (void)a; (void)finished; (void)ctx;
  s_push.a_squash = NULL;
  lag_reset(s_push.l_in);
  lag_reset(s_push.l_out);
}

static void start_stage(void) {
  const int w = s_push.w, h = s_push.h, dir = s_push.dir;
  GRect in_to, out_to, seam_to;

  if (s_push.retreat) {
    in_to = GRect(0, dir > 0 ? h : -h, w, h);
    out_to = GRect(0, 0, w, h);
    seam_to = GRect(0, dir > 0 ? h : -4, w, 4);
  } else {
    in_to = GRect(0, 0, w, h);
    out_to = GRect(0, dir > 0 ? -h : h, w, h);
    /* Seam sits exactly on the join: incoming.y (pushing up) or incoming.y + h
     * (pushing down), so it is off-screen at both ends of the travel. */
    seam_to = GRect(0, dir > 0 ? 0 : h - 4, w, 4);
  }

  s_push.a_in = frame_anim(s_push.l_in, in_to, TRAVEL_MS, AnimationCurveEaseOut);
  s_push.a_out = frame_anim(s_push.l_out, out_to, TRAVEL_MS, AnimationCurveEaseOut);
  s_push.a_seam = frame_anim(s_push.l_seam, seam_to, TRAVEL_MS, AnimationCurveEaseOut);
  animation_set_handlers(s_push.a_in, (AnimationHandlers){.stopped = travel_stopped}, NULL);

  static const AnimationImplementation squash_impl = {.update = squash_update};
  lag_apply(s_push.l_in, 0);
  lag_apply(s_push.l_out, 0);
  s_push.a_squash = animation_create();
  if (s_push.a_squash) {
    animation_set_implementation(s_push.a_squash, &squash_impl);
    animation_set_duration(s_push.a_squash, TRAVEL_MS);
    animation_set_curve(s_push.a_squash, AnimationCurveLinear); /* curve is in the maths */
    animation_set_handlers(s_push.a_squash, (AnimationHandlers){.stopped = squash_stopped}, NULL);
    animation_schedule(s_push.a_squash);
  }

  animation_schedule(s_push.a_in);
  animation_schedule(s_push.a_out);
  animation_schedule(s_push.a_seam);
}

static void begin(Layer *incoming, Layer *outgoing, Layer *seam, int dir, int w, int h,
                  AnimDone done, void *ctx, bool retreat) {
  s_push.l_in = incoming;
  s_push.l_out = outgoing;
  s_push.l_seam = seam;
  s_push.done = done;
  s_push.ctx = ctx;
  s_push.w = w;
  s_push.h = h;
  s_push.dir = dir;
  s_push.cancelled = false;
  s_push.retreat = retreat;
  s_busy = true;
  start_stage();
}

void anim_push(Layer *incoming, Layer *outgoing, Layer *seam, int dir, int w, int h,
               AnimDone done, void *ctx) {
  begin(incoming, outgoing, seam, dir, w, h, done, ctx, false);
}

void anim_retreat(Layer *incoming, Layer *outgoing, Layer *seam, int dir, int w, int h,
                  AnimDone done, void *ctx) {
  begin(incoming, outgoing, seam, dir, w, h, done, ctx, true);
}

void anim_squash_drag(Layer *incoming, Layer *outgoing, int dy, int h) {
  /* The content trails the finger, which is what makes the card feel attached to
   * the glass rather than to the pointer. Capped at LAG_PX so a long pull cannot
   * smear the layout. */
  int lag = (dy * 34) / 100;
  if (lag > LAG_PX) lag = LAG_PX;
  if (lag < -LAG_PX) lag = -LAG_PX;
  (void)h;
  lag_apply(incoming, lag);
  lag_apply(outgoing, (lag * 3) / 4);
}

/* ---------------- landing squash ---------------- */

typedef struct {
  Layer *l;
  Animation *a;
  AnimDone done;
  void *ctx;
} Settle;

static Settle s_settle;

static void settle_update(Animation *a, const AnimationProgress progress) {
  (void)a;
  const int p = (int)(((int64_t)progress * 1000) / ANIMATION_NORMALIZED_MAX);
  lag_apply(s_settle.l, lag_shape(p));
}

static void settle_stopped(Animation *a, bool finished, void *ctx) {
  (void)a; (void)finished; (void)ctx;
  s_settle.a = NULL;
  lag_reset(s_settle.l);
  if (s_settle.done) s_settle.done(s_settle.ctx);
}

void anim_squash_settle(Layer *l, AnimDone done, void *ctx) {
  if (!l) return;
  s_settle.l = l;
  s_settle.done = done;
  s_settle.ctx = ctx;
  static const AnimationImplementation impl = {.update = settle_update};
  s_settle.a = animation_create();
  if (!s_settle.a) {
    lag_reset(l);
    if (done) done(ctx);
    return;
  }
  lag_apply(l, 0);
  animation_set_implementation(s_settle.a, &impl);
  animation_set_duration(s_settle.a, SETTLE_MS);
  animation_set_curve(s_settle.a, AnimationCurveLinear);
  animation_set_handlers(s_settle.a, (AnimationHandlers){.stopped = settle_stopped}, NULL);
  animation_schedule(s_settle.a);
}

void anim_cancel(void) {
  s_push.cancelled = true;
  if (s_push.a_squash) animation_unschedule(s_push.a_squash);
  s_push.a_squash = NULL;
  lag_reset(s_push.l_in);
  lag_reset(s_push.l_out);
  if (s_push.a_in) animation_unschedule(s_push.a_in);
  if (s_push.a_out) animation_unschedule(s_push.a_out);
  if (s_push.a_seam) animation_unschedule(s_push.a_seam);
  s_push.a_in = s_push.a_out = s_push.a_seam = NULL;
  if (s_push.l_seam) {
    layer_set_hidden(s_push.l_seam, true);
    layer_set_frame(s_push.l_seam, GRect(0, -4, s_push.w, 4));
  }
  if (s_push.retreat && s_push.l_in) layer_set_hidden(s_push.l_in, true);
  if (s_push.l_out) {
    layer_set_hidden(s_push.l_out, true);
    layer_set_frame(s_push.l_out, GRect(0, 0, s_push.w, s_push.h));
  }
  /* Drop layer references: the caller may destroy them next. */
  s_push.l_in = s_push.l_out = s_push.l_seam = NULL;
  s_busy = false;
}
