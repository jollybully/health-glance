#include "anim.h"

#define DURATION_MS 170

typedef struct {
  Animation *in;
  Animation *out;
  Animation *seam;
  Layer *l_in, *l_out, *l_seam;
  AnimDone done;
  void *ctx;
  int w, h;
} Push;

static Push s_push;
static bool s_busy;

bool anim_busy(void) { return s_busy; }

static Animation *frame_anim(Layer *l, GRect to) {
  PropertyAnimation *pa = property_animation_create_layer_frame(l, NULL, &to);
  Animation *a = property_animation_get_animation(pa);
  animation_set_duration(a, DURATION_MS);
  animation_set_curve(a, AnimationCurveEaseInOut);
  return a;
}

static void on_stopped(Animation *a, bool finished, void *ctx) {
  (void)a;
  (void)finished;
  (void)ctx;
  /* Siblings run the same duration; drop them so frames can be reset cleanly. */
  if (s_push.out) animation_unschedule(s_push.out);
  if (s_push.seam) animation_unschedule(s_push.seam);

  layer_set_hidden(s_push.l_seam, true);
  layer_set_frame(s_push.l_seam, GRect(0, -4, s_push.w, 4));

  layer_set_hidden(s_push.l_out, true);
  layer_set_frame(s_push.l_out, GRect(0, 0, s_push.w, s_push.h));

  s_busy = false;
  s_push.in = s_push.out = s_push.seam = NULL;
  if (s_push.done) s_push.done(s_push.ctx);
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
  s_busy = true;

  /* Nothing is drawn off the far edge while travelling: the incoming layer
   * starts fully outside, the outgoing ends fully outside. */
  s_push.in = frame_anim(incoming, GRect(0, 0, w, h));
  s_push.out = frame_anim(outgoing, GRect(0, dir > 0 ? -h : h, w, h));
  /* Seam sits on the join: incoming.y (pushing up) or incoming.y + h (down). */
  s_push.seam = frame_anim(seam, GRect(0, dir > 0 ? -4 : h - 4, w, 4));

  animation_set_handlers(s_push.in, (AnimationHandlers){.stopped = on_stopped}, NULL);
  animation_schedule(s_push.in);
  animation_schedule(s_push.out);
  animation_schedule(s_push.seam);
}

void anim_cancel(void) {
  if (s_push.in) animation_unschedule(s_push.in);
  if (s_push.out) animation_unschedule(s_push.out);
  if (s_push.seam) animation_unschedule(s_push.seam);
  s_push.in = s_push.out = s_push.seam = NULL;
  if (s_push.l_seam) layer_set_hidden(s_push.l_seam, true);
  if (s_push.l_out) {
    layer_set_hidden(s_push.l_out, true);
    layer_set_frame(s_push.l_out, GRect(0, 0, s_push.w, s_push.h));
  }
  /* Drop layer references: the caller may destroy them next. */
  s_push.l_in = s_push.l_out = s_push.l_seam = NULL;
  s_busy = false;
}
