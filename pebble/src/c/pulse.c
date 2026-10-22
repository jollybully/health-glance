#include "pulse.h"

#include <pebble.h>

/* A thin frame around the screen that pulses at the live heart rate.
 *
 * Why a border and not a shape: the deck's cards own their pixels, so anything
 * drawn inside competes with the hero numbers. A 0-4px edge reads as ambient
 * state ("the strap is streaming, here is the beat") without touching the cards.
 *
 * Energy: the timer only runs while a fresh sample keeps arriving. It stops
 * itself after STALE_MS so a dead strap cannot leave the screen animating.
 */

#define FRAME_MS 50
#define PHASE_MAX 100
#define STALE_MS 20000

static Layer *s_layer;
static AppTimer *s_timer;
static int s_bpm;
static int s_phase;      /* 0..PHASE_MAX, one full beat */
static uint32_t s_last_push; /* time of the last live sample */
static bool s_visible = true; /* the deck shows it; detail screens do not */

static int frame_thickness(void) {
  /* fast attack, slow decay: what a pulse looks like on a heart monitor */
  int t = s_phase;
  int v;
  if (t < 12) {
    v = (t * 4) / 12;
  } else {
    v = 4 - ((t - 12) * 4) / (PHASE_MAX - 12);
  }
  if (v < 0) v = 0;
  if (v > 4) v = 4;
  return v;
}

static void draw_frame(GContext *ctx, GRect b, int w, GColor c) {
  if (w <= 0) return;
  graphics_context_set_fill_color(ctx, c);
  graphics_fill_rect(ctx, GRect(0, 0, b.size.w, w), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(0, b.size.h - w, b.size.w, w), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(0, 0, w, b.size.h), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(b.size.w - w, 0, w, b.size.h), 0, GCornerNone);
}

static void layer_update(Layer *l, GContext *ctx) {
  GRect b = layer_get_bounds(l);
  int w = frame_thickness();
  draw_frame(ctx, b, w, GColorImperialPurple);
  /* a thin white inner edge at the peak of the beat, so it reads as a pulse and
   * not just a growing border */
  if (s_phase < 10) {
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_draw_rect(ctx, GRect(w + 1, w + 1, b.size.w - 2 * w - 2, b.size.h - 2 * w - 2));
  }
}

static void tick(void *data);

static void schedule(void) {
  if (!s_timer) s_timer = app_timer_register(FRAME_MS, tick, NULL);
}

static void stop(void) {
  if (s_timer) {
    app_timer_cancel(s_timer);
    s_timer = NULL;
  }
  s_phase = 0;
  if (s_layer) {
    layer_set_hidden(s_layer, true);
    layer_mark_dirty(s_layer);
  }
}

static void tick(void *data) {
  (void)data;
  s_timer = NULL;
  if (s_bpm <= 0) {
    stop();
    return;
  }
  uint32_t now = time(NULL);
  if (s_last_push && (now - s_last_push) * 1000 > STALE_MS) {
    s_bpm = 0;
    stop();
    return;
  }
  /* advance one beat's worth of phase for this frame */
  int step = (PHASE_MAX * FRAME_MS * s_bpm) / (60 * 1000);
  if (step < 1) step = 1;
  s_phase = (s_phase + step) % PHASE_MAX;
  if (s_layer && s_visible) {
    layer_set_hidden(s_layer, false);
    layer_mark_dirty(s_layer);
  }
  schedule();
}

void pulse_init(Layer *parent, GRect bounds) {
  s_layer = layer_create(bounds);
  layer_set_update_proc(s_layer, layer_update);
  layer_set_hidden(s_layer, true);
  layer_add_child(parent, s_layer);
}

void pulse_deinit(void) {
  stop();
  if (s_layer) {
    layer_destroy(s_layer);
    s_layer = NULL;
  }
}

void pulse_set_bpm(int bpm) {
  s_last_push = time(NULL);
  if (bpm <= 0) {
    s_bpm = 0;
    stop();
    return;
  }
  s_bpm = bpm;
  schedule();
}

void pulse_set_visible(bool visible) {
  s_visible = visible;
  if (!s_layer) return;
  if (!visible) {
    layer_set_hidden(s_layer, true);
    layer_mark_dirty(s_layer);
  }
}

void pulse_clear(void) {
  s_bpm = 0;
  stop();
}
