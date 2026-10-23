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
static bool s_visible; /* only the Heart card shows the frame */

static int frame_thickness(void) {
  /* One cardiac cycle is a lub-dub: a strong contraction, a quick partial
   * release, then a second softer beat and a long rest. A single bump at the
   * right rate reads as a metronome, not as a heart. */
  const int t = s_phase;
  int v;
  if (t < 10) {          /* lub: fast in */
    v = (t * 4) / 10;
  } else if (t < 22) {   /* release */
    v = 4 - ((t - 10) * 3) / 12;
  } else if (t < 32) {   /* dub: softer second beat */
    v = 1 + ((t - 22) * 2) / 10;
  } else {               /* diastole: rest until the next beat */
    v = 3 - ((t - 32) * 3) / (PHASE_MAX - 32);
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
  /* Black, not purple: the Heart card's own cap is purple, so a purple frame was
   * invisible on the one card that carries it. */
  draw_frame(ctx, b, w, GColorBlack);
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

/* Board the frame only for the Heart card: on any other card it is decoration
 * that lies about which number it belongs to. */
void pulse_set_card(int card) {
  pulse_set_visible(card == 2 /* CARD_HEART */);
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
  } else if (s_bpm > 0) {
    s_phase = 0; /* start the new card on a beat, not mid-diastole */
    layer_set_hidden(s_layer, false);
    layer_mark_dirty(s_layer);
    schedule();
  }
}

void pulse_clear(void) {
  s_bpm = 0;
  stop();
}
