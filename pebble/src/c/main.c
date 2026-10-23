#include <pebble.h>
#include <ctype.h>
#include "stats.h"
#include "cards.h"
#include "anim.h"
#include "mockups.h"
#include "pulse.h"

#ifndef HG_CACHE_PROBE
#define HG_CACHE_PROBE 0
#endif

#ifndef HG_SQUASH_PROBE
#define HG_SQUASH_PROBE 0
#endif
#ifndef HG_TOUCH_SELFTEST
#define HG_TOUCH_SELFTEST 0
#endif

/* Health Glance — native C watch app (Pebble Time 2 / Emery).
 * Same UUID, same AppMessage keys, same 4-card deck as the Alloy build. */

static Window *s_window;
static Layer *s_card[2];
static Layer *s_seam;
static int s_front;   /* index of the visible card layer */
static int s_card_idx; /* card currently shown */
static int s_w, s_h;
static bool s_loaded; /* an inbox push has filled the stats */

#if HG_MOCKUP
/* Mockup mode: Up/Down walk the drill-down concepts, nothing else runs. */
static int s_mock;
static Layer *s_mock_layer;

static void mock_update(Layer *l, GContext *ctx) {
  mockups_draw(ctx, layer_get_bounds(l), s_mock);
}

static void mock_redraw(void) {
  if (s_mock_layer) layer_mark_dirty(s_mock_layer);
}

static void mock_up(ClickRecognizerRef r, void *ctx) {
  (void)r;
  (void)ctx;
  s_mock = (s_mock + 1) % mockups_count();
  APP_LOG(APP_LOG_LEVEL_INFO, "mockup %d", s_mock);
  mock_redraw();
}

static void mock_down(ClickRecognizerRef r, void *ctx) {
  (void)r;
  (void)ctx;
  s_mock = (s_mock + mockups_count() - 1) % mockups_count();
  APP_LOG(APP_LOG_LEVEL_INFO, "mockup %d", s_mock);
  mock_redraw();
}

static void mock_back(ClickRecognizerRef r, void *ctx) {
  (void)r;
  (void)ctx;
  window_stack_pop_all(true);
}
#endif

/* ---------------- card layers ---------------- */

static void card_update(Layer *l, GContext *ctx) {
  CardLayer *d = (CardLayer *)layer_get_data(l);
#if HG_SQUASH_PROBE
  /* QA only: hold a fixed squash so the deformation can be measured against the
   * identity frame instead of argued about from a moving animation. */
  cards_draw_squash(ctx, layer_get_bounds(l), d->card, 1000 * 1000 / HG_SQUASH_PROBE,
                    HG_SQUASH_PROBE);
  return;
#endif
  cards_draw_squash(ctx, layer_get_bounds(l), d->card, d->pm_x, d->pm_y);
}

static void seam_update(Layer *l, GContext *ctx) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, layer_get_bounds(l), 0, GCornerNone);
}

static Layer *make_card_layer(GRect frame, int card) {
  Layer *l = layer_create_with_data(frame, sizeof(CardLayer));
  CardLayer *d = (CardLayer *)layer_get_data(l);
  d->card = card;
  d->pm_x = 1000;
  d->pm_y = 1000;
  layer_set_update_proc(l, card_update);
  return l;
}

/* ---------------- deck push ---------------- */

static void push_done(void *ctx) {
  (void)ctx;
  s_front = 1 - s_front;
  s_card_idx = ((CardLayer *)layer_get_data(s_card[s_front]))->card;
  layer_mark_dirty(s_card[s_front]);
}

/* The launch slide: the deck comes down over the watchface, which is how PebbleOS
 * presents an app panel. It used to rise from the bottom edge, i.e. backwards. */
#ifndef HG_LAUNCH_MS
#define HG_LAUNCH_MS 220
#endif
static Animation *s_launch;

static void launch_stopped(Animation *a, bool finished, void *ctx) {
  (void)a;
  (void)finished;
  (void)ctx;
  s_launch = NULL;
  layer_set_frame(s_card[0], GRect(0, 0, s_w, s_h));
  APP_LOG(APP_LOG_LEVEL_INFO, "launch slide settled at y=%d", layer_get_frame(s_card[0]).origin.y);
  /* It lands rather than simply arriving: the contents compress on contact and
   * spring back, which is the squash that sells the weight of the card. */
  anim_squash_settle(s_card[0], NULL, NULL);
}

static void launch_anim_start(void) {
  layer_set_frame(s_card[0], GRect(0, -s_h, s_w, s_h)); /* starts above the glass */
  GRect f0 = layer_get_frame(s_card[0]);
  APP_LOG(APP_LOG_LEVEL_INFO, "launch slide from y=%d to y=0 over %d ms", f0.origin.y,
          HG_LAUNCH_MS);
#if HG_LAUNCH_HOLD
  return; /* probe build: hold the first frame for a screenshot */
#endif
  GRect to = GRect(0, 0, s_w, s_h);
  PropertyAnimation *pa = property_animation_create_layer_frame(s_card[0], NULL, &to);
  s_launch = property_animation_get_animation(pa);
  animation_set_duration(s_launch, HG_LAUNCH_MS);
  animation_set_curve(s_launch, AnimationCurveEaseOut);
  animation_set_handlers(s_launch, (AnimationHandlers){.stopped = launch_stopped}, NULL);
  animation_schedule(s_launch);
  cards_warp_start(s_card[0]); /* the numbers warp in as the deck rises */
}

static void push(int to, int dir) {
  if (anim_busy() || to < 0 || to >= CARD_COUNT || to == s_card_idx) return;

  if (s_launch) { /* a press during the launch slide cancels it, snapping home */
    animation_unschedule(s_launch);
    s_launch = NULL;
    layer_set_frame(s_card[0], GRect(0, 0, s_w, s_h));
  }

  pulse_set_card(to); /* the pulse belongs to the Heart card, nowhere else */
  int in = 1 - s_front, out = s_front;
  Layer *li = s_card[in], *lo = s_card[out];

  layer_set_frame(li, GRect(0, dir > 0 ? s_h : -s_h, s_w, s_h));
  ((CardLayer *)layer_get_data(li))->card = to;
  layer_set_hidden(li, false);
  layer_mark_dirty(li);

  layer_set_frame(lo, GRect(0, 0, s_w, s_h));
  layer_set_hidden(lo, false);

  layer_set_frame(s_seam, GRect(0, dir > 0 ? s_h : -4, s_w, 4));
  layer_set_hidden(s_seam, false);

  /* The card warps as it arrives: stretch, squash, settle. */
  cards_warp_start(li);
  anim_push(li, lo, s_seam, dir, s_w, s_h, push_done, NULL);
}

/* ---------------- touch scrolling ---------------- */

/* Drag the deck: the finger pulls the neighbouring card into view, and letting go
 * past a third of the screen commits the page. Finger down = the next card comes
 * down from the top, exactly like pressing Up. */
/* How far the finger travels before the card starts to follow, and how far it has
 * to have travelled at release to turn the page. The old commit distance was a
 * third of the screen (76 px), which felt like hauling the card across the glass;
 * a quarter of that reads as a flick. */
#define DRAG_START_PX 6
#define DRAG_COMMIT_DIV 9

static bool s_drag;
static int s_drag_x0, s_drag_y0;
static int s_drag_dir; /* 0 undecided, -1 next-from-top, +1 previous-from-bottom */
static int s_drag_dy;

static void drag_apply(int dy) {
  if (s_drag_dir < 0) {
    if (dy < 0) dy = 0;
    if (dy > s_h) dy = s_h;
  } else {
    if (dy > 0) dy = 0;
    if (dy < -s_h) dy = -s_h;
  }
  s_drag_dy = dy;

  int in = 1 - s_front;
  int y_in = (s_drag_dir < 0 ? -s_h : s_h) + dy;
  int join = s_drag_dir < 0 ? y_in + s_h : y_in;
  layer_set_frame(s_card[in], GRect(0, y_in, s_w, s_h));
  layer_set_frame(s_card[s_front], GRect(0, s_drag_dir < 0 ? join : join - s_h, s_w, s_h));
  layer_set_frame(s_seam, GRect(0, join, s_w, 4));
  layer_set_hidden(s_seam, false);
  anim_squash_drag(s_card[in], s_card[s_front], dy, s_h);
}

static void drag_pick_direction(int dir) {
  int to = s_card_idx + (dir < 0 ? 1 : -1);
  if (to < 0 || to >= CARD_COUNT) return; /* nothing to pull in that way */
  int in = 1 - s_front;
  layer_set_frame(s_card[in], GRect(0, dir < 0 ? -s_h : s_h, s_w, s_h));
  ((CardLayer *)layer_get_data(s_card[in]))->card = to;
  layer_set_hidden(s_card[in], false);
  layer_mark_dirty(s_card[in]);
  layer_set_frame(s_card[s_front], GRect(0, 0, s_w, s_h));
  layer_set_hidden(s_card[s_front], false);
  s_drag_dir = dir;
}

static void drag_retreat_done(void *ctx) {
  (void)ctx;
  layer_mark_dirty(s_card[s_front]);
}

static void touch_handler(const TouchEvent *event, void *context) {
  (void)context;
  if (!event) return;

  if (event->type == TouchEvent_Touchdown) {
    s_drag = false;
    s_drag_dir = 0;
    s_drag_dy = 0;
    if (anim_busy()) return; /* let the running page turn finish */
    s_drag = true;
    s_drag_x0 = event->x;
    s_drag_y0 = event->y;
    return;
  }

  if (!s_drag) return;

  if (event->type == TouchEvent_PositionUpdate) {
    int dy = event->y - s_drag_y0;
    int dx = event->x - s_drag_x0;
    if (s_drag_dir == 0) {
      if (dy > -DRAG_START_PX && dy < DRAG_START_PX) return;
      if (dy < 0 ? -dy <= (dx < 0 ? -dx : dx) : dy <= (dx < 0 ? -dx : dx)) return; /* too horizontal */
      drag_pick_direction(dy > 0 ? -1 : 1);
      if (s_drag_dir == 0) return; /* at an end: no card to pull in */
    }
    drag_apply(dy);
    return;
  }

  if (event->type == TouchEvent_Liftoff) {
    s_drag = false;
    if (s_drag_dir == 0) return;
    int travel = s_drag_dir < 0 ? s_drag_dy : -s_drag_dy;
    int in = 1 - s_front;
    if (travel * DRAG_COMMIT_DIV >= s_h) {
      cards_warp_start(s_card[in]);
      anim_push(s_card[in], s_card[s_front], s_seam, s_drag_dir, s_w, s_h, push_done, NULL);
    } else {
      anim_retreat(s_card[in], s_card[s_front], s_seam, s_drag_dir, s_w, s_h,
                   drag_retreat_done, NULL);
    }
    s_drag_dir = 0;
    s_drag_dy = 0;
  }
}

#if HG_TOUCH_SELFTEST
/* There is no way to inject touchscreen events into the emulator (no CLI command,
 * no QEMU packet), so this replays a synthetic swipe through the real handler:
 * Select = a long drag (should page), long-press Select = a short one (should
 * spring back). Off in shipped builds. */
static void synth_swipe(int dy) {
  TouchEvent ev;
  ev.type = TouchEvent_Touchdown;
  ev.x = 100;
  ev.y = 140;
  touch_handler(&ev, NULL);
  for (int i = 1; i <= 8; i++) {
    ev.type = TouchEvent_PositionUpdate;
    ev.y = 140 + (dy * i) / 8;
    touch_handler(&ev, NULL);
  }
  ev.type = TouchEvent_Liftoff;
  ev.y = 140 + dy;
  touch_handler(&ev, NULL);
}

static void selftest_click(ClickRecognizerRef r, void *ctx) {
  (void)r;
  (void)ctx;
  const int before = s_card_idx;
  synth_swipe(30); /* flick towards the next card: should turn the page */
  APP_LOG(APP_LOG_LEVEL_INFO, "TOUCH_SELFTEST flick 30px: %d -> %d (want %d)", before,
          s_card_idx, before < CARD_COUNT - 1 ? before + 1 : before);
}

static void selftest_long_click(ClickRecognizerRef r, void *ctx) {
  (void)r;
  (void)ctx;
  const int before = s_card_idx;
  synth_swipe(12); /* a nudge below the threshold: should spring back */
  APP_LOG(APP_LOG_LEVEL_INFO, "TOUCH_SELFTEST nudge 12px: %d -> %d (want %d)", before,
          s_card_idx, before);
}
#endif

/* ---------------- clicks ---------------- */

static void up_click(ClickRecognizerRef r, void *ctx) {
  (void)r;
  (void)ctx;
  if (s_card_idx < CARD_COUNT - 1) push(s_card_idx + 1, -1); /* next card enters from the top */
}

static void down_click(ClickRecognizerRef r, void *ctx) {
  (void)r;
  (void)ctx;
  if (anim_busy()) return; /* let the push settle before acting on the deck */
  if (s_card_idx == 0) return; /* Back is the only way out of the app */
  push(s_card_idx - 1, 1); /* previous card enters from the bottom */
}

static void select_click(ClickRecognizerRef r, void *ctx) {
  (void)r;
  (void)ctx;
  /* The deck is the app: four cards, nothing to drill into. */
}

static void back_click(ClickRecognizerRef r, void *ctx) {
  (void)r;
  (void)ctx;
  /* Cancel first: the layers are destroyed in window_unload. */
  anim_cancel();
  window_stack_pop_all(true);
}

static void click_config(void *ctx) {
  (void)ctx;
#if HG_MOCKUP
  window_single_click_subscribe(BUTTON_ID_UP, mock_up);
  window_single_click_subscribe(BUTTON_ID_DOWN, mock_down);
  window_single_click_subscribe(BUTTON_ID_BACK, mock_back);
  return;
#endif
#if HG_TOUCH_SELFTEST
  /* QA build: Select replays a synthetic swipe, so it must NOT also be bound to
   * select_click — two handlers on one button fired the gesture twice. */
  window_single_click_subscribe(BUTTON_ID_SELECT, selftest_click);
  window_long_click_subscribe(BUTTON_ID_SELECT, 0, selftest_long_click, NULL);
#else
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
#endif
  window_single_click_subscribe(BUTTON_ID_UP, up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click);
  window_single_click_subscribe(BUTTON_ID_BACK, back_click);
}

/* ---------------- AppMessage ---------------- */

/* Phone also pushes on its own onAppOpened, but keep asking until data lands:
 * one lost request must not leave the watch empty for half an hour. */
static AppTimer *s_req_timer;
static int s_tries;

/* Data lands in two messages (scalars, then charts), so a short follow-up write
 * catches whatever the first one could not. Flash is spared by the change check. */
static AppTimer *s_save_timer;

static void save_timer_cb(void *data) {
  (void)data;
  s_save_timer = NULL;
  if (stats_cache_needed()) {
    APP_LOG(APP_LOG_LEVEL_INFO, "saving cache (follow-up)");
    stats_save_cache();
  }
}

static void schedule_cache_save(void) {
  if (!s_save_timer) {
    s_save_timer = app_timer_register(1200, save_timer_cb, NULL);
  }
}
#if !HG_DEMO
/* Escalating, not machine-gun: every STATS_REQ makes the phone re-read Health
 * Connect, so six asks spread over ~25s is the budget. */
static const int REQ_DELAYS_MS[] = {400, 800, 1500, 3000, 6000, 12000};
#define REQ_MAX_TRIES ((int)(sizeof(REQ_DELAYS_MS) / sizeof(REQ_DELAYS_MS[0])))
#endif

static void req_timer(void *data);

/* Ask the phone for one past day's charts (detail screens cycle with Up/Down). */
static void send_req(void) {
#if HG_DEMO
  return;
#else
  if (s_loaded) return;
  DictionaryIterator *it;
  AppMessageResult result = app_message_outbox_begin(&it);
  if (result == APP_MSG_OK) {
    dict_write_int32(it, MESSAGE_KEY_STATS_REQ, 1);
    result = app_message_outbox_send();
  }
  if (result == APP_MSG_OK) {
    APP_LOG(APP_LOG_LEVEL_INFO, "STATS_REQ sent (try %d)", s_tries + 1);
  } else {
    APP_LOG(APP_LOG_LEVEL_WARNING, "STATS_REQ not sent: %d", (int)result);
  }
  /* "Sent" is not "answered": keep asking until an inbox push fills the
   * stats, otherwise one lost request leaves the deck empty for half an hour. */
  int delay = REQ_DELAYS_MS[s_tries < REQ_MAX_TRIES ? s_tries : REQ_MAX_TRIES - 1];
  s_tries++;
  if (s_tries < REQ_MAX_TRIES) {
    if (s_req_timer) app_timer_cancel(s_req_timer);
    s_req_timer = app_timer_register(delay, req_timer, NULL);
  } else {
    /* Say so instead of sitting on "Waiting for phone" forever. */
    strncpy(g_stats.status, "phone not replying", ST_LEN - 1);
    g_stats.status[ST_LEN - 1] = '\0';
    layer_mark_dirty(s_card[s_front]);
  }
#endif
}

static void req_timer(void *data) {
  (void)data;
  s_req_timer = NULL;
  send_req();
}

static void start_req_loop(void) {
  s_tries = 0;
  if (s_req_timer) app_timer_cancel(s_req_timer);
  s_req_timer = app_timer_register(400, req_timer, NULL);
}

static void outbox_failed(DictionaryIterator *it, AppMessageResult reason, void *ctx) {
  (void)it;
  (void)ctx;
  APP_LOG(APP_LOG_LEVEL_WARNING, "outbox failed: %d", (int)reason);
}

/* The Kotlin companion writes every number as PebbleDictionaryItem.UInt32, so
 * tuples arrive as TUPLE_UINT; some older paths used TUPLE_INT. Accept both. */
static int32_t tuple_int(Tuple *t) {
  if (t->type == TUPLE_INT) return (int32_t)t->value->int32;
  if (t->type == TUPLE_UINT) {
    switch (t->length) {
      case 1: return (int32_t)t->value->uint8;
      case 2: return (int32_t)t->value->uint16;
      case 4: return (int32_t)t->value->uint32;
      default: return 0;
    }
  }
  return 0;
}

static void copy_str(char *dst, Tuple *t) {
  if (t->type == TUPLE_CSTRING) {
    strncpy(dst, t->value->cstring, ST_LEN - 1);
    dst[ST_LEN - 1] = '\0';
  } else if (t->type == TUPLE_BYTE_ARRAY) {
    size_t n = t->length < ST_LEN - 1 ? t->length : ST_LEN - 1;
    memcpy(dst, t->value->data, n);
    dst[n] = '\0';
  } else if (t->type == TUPLE_INT) {
    snprintf(dst, ST_LEN, "%d", (int)t->value->int32);
  }
}

static void inbox_received(DictionaryIterator *it, void *ctx) {
  (void)ctx;
  Stats *s = &g_stats;
  bool got_root = false, got_wo = false, got_live = false;
  char title[ST_LEN];
  title[0] = '\0';

  for (Tuple *t = dict_read_first(it); t; t = dict_read_next(it)) {
    /* Keys arrive as uint32_t externs from message_keys.auto.h, so no switch. */
    uint32_t k = t->key;
    int32_t iv = tuple_int(t);

    if (k == MESSAGE_KEY_STEPS) {
      s->steps = iv;
      got_root = true;
    } else if (k == MESSAGE_KEY_DIST_M) {
      s->dist_m = iv;
      got_root = true;
    } else if (k == MESSAGE_KEY_CALORIES) {
      s->calories = iv;
      got_root = true;
    } else if (k == MESSAGE_KEY_RESTING_HR) {
      s->resting_hr = iv;
      got_root = true;
    } else if (k == MESSAGE_KEY_SLEEP_MIN) {
      s->sleep_min = iv;
      got_root = true;
    } else if (k == MESSAGE_KEY_DEEP) {
      s->deep = iv;
    } else if (k == MESSAGE_KEY_LIGHT) {
      s->light = iv;
    } else if (k == MESSAGE_KEY_REM) {
      s->rem = iv;
    } else if (k == MESSAGE_KEY_AWAKE) {
      s->awake = iv;
    } else if (k == MESSAGE_KEY_SLEEP_START) {
      copy_str(s->sleep_start, t);
    } else if (k == MESSAGE_KEY_SLEEP_END) {
      copy_str(s->sleep_end, t);
    } else if (k == MESSAGE_KEY_UPDATED) {
      copy_str(s->updated, t);
    } else if (k == MESSAGE_KEY_HR_MIN) {
      s->hr_min = iv;
      got_root = true;
    } else if (k == MESSAGE_KEY_HR_MAX) {
      s->hr_max = iv;
      got_root = true;
    } else if (k == MESSAGE_KEY_WO_DUR_S) {
      s->wo_dur = iv;
      got_wo = true;
    } else if (k == MESSAGE_KEY_WO_CAL) {
      s->wo_cal = iv;
    } else if (k == MESSAGE_KEY_WO_AVG_HR) {
      s->wo_avg = iv;
    } else if (k == MESSAGE_KEY_WO_DIST_M) {
      s->wo_dist = iv;
    } else if (k == MESSAGE_KEY_WO_TITLE) {
      copy_str(title, t);
    } else if (k == MESSAGE_KEY_WO_ZONE_LOW) {
      s->wo_lo = iv;
    } else if (k == MESSAGE_KEY_WO_ZONE_MID) {
      s->wo_mid = iv;
    } else if (k == MESSAGE_KEY_WO_ZONE_HIGH) {
      s->wo_hi = iv;
    } else if (k == MESSAGE_KEY_HR_LIVE) {
      g_live.bpm = iv;
      got_live = true;
    } else if (k == MESSAGE_KEY_HR_LIVE_TS) {
      g_live.ts = (uint32_t)iv;
    } else if (k == MESSAGE_KEY_HR_LIVE_BATT) {
      g_live.batt = iv;
    } else if (k == MESSAGE_KEY_HR_LIVE_STATE) {
      g_live.state = iv;
    } else if (k == MESSAGE_KEY_HR_LIVE_MIN) {
      g_live.min = iv;
      got_live = true;
    } else if (k == MESSAGE_KEY_HR_LIVE_MAX) {
      g_live.max = iv;
    } else if (k == MESSAGE_KEY_STATS_ERR) {
      if (t->type == TUPLE_CSTRING) {
        strncpy(s->status, t->value->cstring, ST_LEN - 1);
        s->status[ST_LEN - 1] = '\0';
      }
    }
    /* unknown keys ignored */
  }

  if (title[0]) {
    int j = 0;
    for (int i = 0; title[i] && j < 12; i++) {
      s->wo_title[j++] = (char)toupper((unsigned char)title[i]);
    }
    s->wo_title[j] = '\0';
  }

  APP_LOG(APP_LOG_LEVEL_INFO, "inbox root=%d wo=%d steps=%d", (int)got_root, (int)got_wo,
          s->steps);
  if (got_wo) s->has_workout = true;
  /* Live heart rate arrives on its own, is never cached, and the Heart screen is
   * usually open while it streams: repaint or the number never moves. */
  if (got_live) {
    /* the frame beats at the strap's rate while the deck is showing */
    pulse_set_bpm(stats_live_fresh() ? g_live.bpm : 0);
    layer_mark_dirty(s_card[s_front]);
  }
  /* The charts travel as their own AppMessage, so a series-only message has to
   * count as data arriving — without this the cache was never written for them. */
  if (got_root || got_wo) {
    s_loaded = true;
    stats_save_cache();
    if (s_req_timer) {
      app_timer_cancel(s_req_timer);
      s_req_timer = NULL;
    }
    strncpy(s->status, "Live", ST_LEN - 1);
    s->status[ST_LEN - 1] = '\0';
    schedule_cache_save();
    layer_mark_dirty(s_card[s_front]);
  }
}

static void inbox_dropped(AppMessageResult reason, void *ctx) {
  (void)ctx;
  APP_LOG(APP_LOG_LEVEL_ERROR, "inbox dropped: %d", (int)reason);
}

/* ---------------- window ---------------- */

static void window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  GRect b = layer_get_bounds(root);
  s_w = b.size.w;
  s_h = b.size.h;

#if HG_MOCKUP
  s_mock_layer = layer_create(b);
  layer_set_update_proc(s_mock_layer, mock_update);
  layer_add_child(root, s_mock_layer);
  return;
#endif

  s_card[0] = make_card_layer(GRect(0, 0, s_w, s_h), s_card_idx);
  s_card[1] = make_card_layer(GRect(0, 0, s_w, s_h), 0);
  layer_set_hidden(s_card[1], true);

  s_seam = layer_create(GRect(0, -4, s_w, 4));
  layer_set_update_proc(s_seam, seam_update);
  layer_set_hidden(s_seam, true);

  layer_add_child(root, s_card[1]);
  layer_add_child(root, s_card[0]);
  layer_add_child(root, s_seam); /* seam on top of both cards */
  pulse_init(root, b);  /* live-rate frame, shown on the Heart card only */
  pulse_set_card(s_card_idx);

#if HG_CACHE_PROBE
  /* QA only: paint the cache state where a screenshot can read it. */
  app_log(APP_LOG_LEVEL_INFO, "CACHE_PROBE steps=%d updated=%s from_cache=%d series=%d",
          g_stats.steps, g_stats.updated, (int)stats_is_cached(), 0);
#endif

  /* Always rise into place: quick launch (Up from the watchface) is the case
   * that matters, but every launch reads better with it. */
  APP_LOG(APP_LOG_LEVEL_INFO, "launch reason %d", (int)launch_reason());
  launch_anim_start();
}

static void window_appear(Window *w) {
  (void)w;
#if HG_MOCKUP
  return;
#endif
  start_req_loop();
  /* Touch is only present on some platforms, so ask before subscribing. */
  touch_service_subscribe(touch_handler, NULL);
  APP_LOG(APP_LOG_LEVEL_INFO, "touch enabled=%d", (int)touch_service_is_enabled());
}

static void window_disappear(Window *w) {
  (void)w;
#if HG_MOCKUP
  return;
#endif
  touch_service_unsubscribe();
  if (s_req_timer) {
    app_timer_cancel(s_req_timer);
    s_req_timer = NULL;
  }
}

static void window_unload(Window *w) {
  (void)w;
#if HG_MOCKUP
  if (s_mock_layer) {
    layer_destroy(s_mock_layer);
    s_mock_layer = NULL;
  }
  return;
#endif
  if (s_launch) {
    animation_unschedule(s_launch);
    s_launch = NULL;
  }
  cards_warp_cancel();
  anim_cancel();
  pulse_deinit();
  layer_destroy(s_seam);
  layer_destroy(s_card[0]);
  layer_destroy(s_card[1]);
  s_seam = NULL;
  s_card[0] = s_card[1] = NULL;
}

static void init(void) {
  stats_init();
  cards_init();

  app_message_register_inbox_received(inbox_received);
  app_message_register_inbox_dropped(inbox_dropped);
  app_message_register_outbox_failed(outbox_failed);
  /* The Moddable runtime always opened the maximum, so the old Alloy build never
   * actually ran with 512 bytes; a full companion push can exceed that. */
  app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());

  s_window = window_create();
  window_set_background_color(s_window, GColorWhite);
  window_set_click_config_provider(s_window, click_config);
  window_set_window_handlers(s_window, (WindowHandlers){
                                           .load = window_load,
                                           .appear = window_appear,
                                           .disappear = window_disappear,
                                           .unload = window_unload,
                                       });
  window_stack_push(s_window, true);
}

static void deinit(void) {
  if (s_req_timer) {
    app_timer_cancel(s_req_timer);
    s_req_timer = NULL;
  }
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
