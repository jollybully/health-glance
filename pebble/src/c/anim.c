#include "anim.h"

/* Page movement, PebbleOS-flat: one animation per layer with a plain ease-out.
 *  - anim_push():    commit a page turn (button, or a drag that travelled far enough)
 *  - anim_retreat(): the drag was too short, so put the incoming card back where
 *                    it came from without changing the current page.
 * Both animate from the layer's CURRENT frame, so a drag hands off straight into
 * the settle animation with no jump. */

#define TRAVEL_MS 165

typedef struct {
  Layer *l_in, *l_out, *l_seam;
  Animation *a_in, *a_out, *a_seam; /* current stage only */
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
  if (s_push.retreat && s_push.l_in) layer_set_hidden(s_push.l_in, true);
  if (s_push.l_out) {
    layer_set_hidden(s_push.l_out, true);
    layer_set_frame(s_push.l_out, GRect(0, 0, s_push.w, s_push.h));
  }
  /* Drop layer references: the caller may destroy them next. */
  s_push.l_in = s_push.l_out = s_push.l_seam = NULL;
  s_busy = false;
}
