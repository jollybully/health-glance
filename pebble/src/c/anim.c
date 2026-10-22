#include "anim.h"

/* Full Deck Push + Seam, in two stages so it lands like a PebbleOS transition
 * instead of stopping dead:
 *
 *   stage 1  130 ms, EaseOut   travel to 8 px PAST the resting position
 *   stage 2   75 ms, EaseInOut settle back onto it
 *
 * A custom easing curve would express this in one animation, but
 * animation_set_custom_curve() with an overshooting curve (> ANIMATION_NORMALIZED_MAX)
 * kills the app on SDK 4.17, so the overshoot is done with a second animation. */

#define TRAVEL_MS 130
#define SETTLE_MS 75
#define OVERSHOOT_PX 8

typedef struct {
  Layer *l_in, *l_out, *l_seam;
  Animation *a_in, *a_out, *a_seam; /* current stage only */
  AnimDone done;
  void *ctx;
  int w, h, dir;
  bool settling;
  bool cancelled;
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
   * silently swallowed every later button press. New animations are created for
   * each stage instead. */
  layer_set_hidden(s_push.l_seam, true);
  layer_set_frame(s_push.l_seam, GRect(0, -4, s_push.w, 4));

  layer_set_hidden(s_push.l_out, true);
  layer_set_frame(s_push.l_out, GRect(0, 0, s_push.w, s_push.h));

  s_push.a_in = s_push.a_out = s_push.a_seam = NULL;
  s_busy = false;
  if (s_push.done) s_push.done(s_push.ctx);
}

static void stage2_stopped(Animation *a, bool finished_stage, void *ctx) {
  (void)a;
  (void)finished_stage;
  (void)ctx;
  if (s_push.cancelled) return;
  finish();
}

static void start_stage(int stage);

static void stage1_stopped(Animation *a, bool finished_stage, void *ctx) {
  (void)a;
  (void)finished_stage;
  (void)ctx;
  if (s_push.cancelled) return;
  s_push.a_in = s_push.a_out = s_push.a_seam = NULL;
  start_stage(2);
}

static void start_stage(int stage) {
  const int w = s_push.w, h = s_push.h, dir = s_push.dir;
  const int over = -OVERSHOOT_PX * dir; /* +8/-8 past the resting position */
  const int in_rest = 0;
  const int in_past = in_rest + over;
  const int out_rest = dir > 0 ? -h : h;
  const int out_past = out_rest + over;
  const int seam_rest = dir > 0 ? 0 : h - 4;
  const int seam_past = seam_rest + over;

  if (stage == 1) {
    s_push.a_in = frame_anim(s_push.l_in, GRect(0, in_past, w, h), TRAVEL_MS,
                             AnimationCurveEaseOut);
    s_push.a_out = frame_anim(s_push.l_out, GRect(0, out_past, w, h), TRAVEL_MS,
                              AnimationCurveEaseOut);
    s_push.a_seam = frame_anim(s_push.l_seam, GRect(0, seam_past, w, 4), TRAVEL_MS,
                               AnimationCurveEaseOut);
    animation_set_handlers(s_push.a_in, (AnimationHandlers){.stopped = stage1_stopped}, NULL);
  } else {
    s_push.settling = true;
    s_push.a_in = frame_anim(s_push.l_in, GRect(0, in_rest, w, h), SETTLE_MS,
                             AnimationCurveEaseInOut);
    s_push.a_out = frame_anim(s_push.l_out, GRect(0, out_rest, w, h), SETTLE_MS,
                              AnimationCurveEaseInOut);
    s_push.a_seam = frame_anim(s_push.l_seam, GRect(0, seam_rest, w, 4), SETTLE_MS,
                               AnimationCurveEaseInOut);
    animation_set_handlers(s_push.a_in, (AnimationHandlers){.stopped = stage2_stopped}, NULL);
  }

  animation_schedule(s_push.a_in);
  animation_schedule(s_push.a_out);
  animation_schedule(s_push.a_seam);
}

void anim_push(Layer *incoming, Layer *outgoing, Layer *seam, int dir, int w, int h,
               AnimDone done, void *ctx) {
  s_push.l_in = incoming;
  s_push.l_out = outgoing;
  s_push.l_seam = seam;
  s_push.done = done;
  s_push.ctx = ctx;
  s_push.w = w;
  s_push.h = h;
  s_push.dir = dir;
  s_push.settling = false;
  s_push.cancelled = false;
  s_busy = true;
  start_stage(1);
}

void anim_cancel(void) {
  s_push.cancelled = true;
  if (s_push.a_in) animation_unschedule(s_push.a_in);
  if (s_push.a_out) animation_unschedule(s_push.a_out);
  if (s_push.a_seam) animation_unschedule(s_push.a_seam);
  s_push.a_in = s_push.a_out = s_push.a_seam = NULL;
  if (s_push.l_seam) {
    layer_set_hidden(s_push.l_seam, true);
    layer_set_frame(s_push.l_seam, GRect(0, -4, s_push.w, 4));
  }
  if (s_push.l_out) {
    layer_set_hidden(s_push.l_out, true);
    layer_set_frame(s_push.l_out, GRect(0, 0, s_push.w, s_push.h));
  }
  /* Drop layer references: the caller may destroy them next. */
  s_push.l_in = s_push.l_out = s_push.l_seam = NULL;
  s_busy = false;
}
