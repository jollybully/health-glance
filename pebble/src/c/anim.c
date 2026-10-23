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

/* Squash and stretch, volume preserved: a card that compresses vertically gets
 * wider by the same ratio, which is what stops deformation reading as a zoom.
 * pm_y < 1000 is a squash, > 1000 a stretch. */
static void squash_apply(Layer *l, int pm_y) {
  if (!l) return;
  CardLayer *d = (CardLayer *)layer_get_data(l);
  if (!d) return;
  d->pm_y = pm_y;
  d->pm_x = (1000 * 1000) / (pm_y > 0 ? pm_y : 1000);
  layer_mark_dirty(l);
}

static void squash_reset(Layer *l) {
  if (!l) return;
  CardLayer *d = (CardLayer *)layer_get_data(l);
  if (!d) return;
  d->pm_x = 1000;
  d->pm_y = 1000;
  layer_mark_dirty(l);
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

/* 0 -> 600: the arriving card is still compressed at the seam edge and stretches
 * as it fills the screen. 600 -> 1000: it overshoots slightly tall, then settles.
 * The departing card does the opposite, so the pair reads as one rubber sheet
 * being pulled through the frame. */
static int incoming_pm_y(int p) {
  if (p < 600) return 880 + ((1120 - 880) * p) / 600;
  return 1120 - (120 * (p - 600)) / 400;
}

static void squash_update(Animation *a, const AnimationProgress progress) {
  (void)a;
  int p = (int)(((int64_t)progress * 1000) / ANIMATION_NORMALIZED_MAX);
  if (s_push.retreat) {
    /* the drag did not commit: unfurl the incoming card as it leaves */
    squash_apply(s_push.l_in, incoming_pm_y(p));
    squash_apply(s_push.l_out, 1000);
    return;
  }
  squash_apply(s_push.l_in, incoming_pm_y(p));
  squash_apply(s_push.l_out, 1000 - (120 * p) / 1000);
}

static void squash_stopped(Animation *a, bool finished, void *ctx) {
  (void)a; (void)finished; (void)ctx;
  s_push.a_squash = NULL;
  squash_reset(s_push.l_in);
  squash_reset(s_push.l_out);
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
  squash_apply(s_push.l_in, incoming_pm_y(0));
  squash_apply(s_push.l_out, 1000);
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
  int travel = dy < 0 ? -dy : dy;
  if (travel > h) travel = h;
  /* up to ~9% compression at a full-screen pull */
  int pm_y = 1000 - (travel * 90) / (h > 0 ? h : 1);
  squash_apply(incoming, pm_y);
  squash_apply(outgoing, 1000 - (travel * 45) / (h > 0 ? h : 1));
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
  int p = (int)(((int64_t)progress * 1000) / ANIMATION_NORMALIZED_MAX);
  squash_apply(s_settle.l, incoming_pm_y(p));
}

static void settle_stopped(Animation *a, bool finished, void *ctx) {
  (void)a; (void)finished; (void)ctx;
  s_settle.a = NULL;
  squash_reset(s_settle.l);
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
    squash_reset(l);
    if (done) done(ctx);
    return;
  }
  squash_apply(l, incoming_pm_y(0));
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
  squash_reset(s_push.l_in);
  squash_reset(s_push.l_out);
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
