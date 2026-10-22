#include <pebble.h>
#include <ctype.h>
#include "stats.h"
#include "cards.h"
#include "anim.h"

/* Health Glance — native C watch app (Pebble Time 2 / Emery).
 * Same UUID, same AppMessage keys, same 4-card deck as the Alloy build. */

static Window *s_window;
static Layer *s_card[2];
static Layer *s_seam;
static int s_front;   /* index of the visible card layer */
static int s_card_idx; /* card currently shown */
static int s_w, s_h;
static bool s_loaded; /* an inbox push has filled the stats */

/* ---------------- card layers ---------------- */

static void card_update(Layer *l, GContext *ctx) {
  cards_draw(ctx, layer_get_bounds(l), *(int *)layer_get_data(l));
}

static void seam_update(Layer *l, GContext *ctx) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, layer_get_bounds(l), 0, GCornerNone);
}

static Layer *make_card_layer(GRect frame, int card) {
  Layer *l = layer_create_with_data(frame, sizeof(int));
  *(int *)layer_get_data(l) = card;
  layer_set_update_proc(l, card_update);
  return l;
}

/* ---------------- deck push ---------------- */

static void push_done(void *ctx) {
  (void)ctx;
  s_front = 1 - s_front;
  s_card_idx = *(int *)layer_get_data(s_card[s_front]);
  layer_mark_dirty(s_card[s_front]);
}

static void push(int to, int dir) {
  if (anim_busy() || to < 0 || to >= CARD_COUNT || to == s_card_idx) return;

  int in = 1 - s_front, out = s_front;
  Layer *li = s_card[in], *lo = s_card[out];

  layer_set_frame(li, GRect(0, dir > 0 ? s_h : -s_h, s_w, s_h));
  *(int *)layer_get_data(li) = to;
  layer_set_hidden(li, false);
  layer_mark_dirty(li);

  layer_set_frame(lo, GRect(0, 0, s_w, s_h));
  layer_set_hidden(lo, false);

  layer_set_frame(s_seam, GRect(0, dir > 0 ? s_h : -4, s_w, 4));
  layer_set_hidden(s_seam, false);

  anim_push(li, lo, s_seam, dir, s_w, s_h, push_done, NULL);
}

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
  if (s_card_idx == 0) {
    window_stack_pop_all(true); /* Down on Steps exits to the watchface */
    return;
  }
  push(s_card_idx - 1, 1); /* previous card enters from the bottom */
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
  window_single_click_subscribe(BUTTON_ID_UP, up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click);
  window_single_click_subscribe(BUTTON_ID_BACK, back_click);
}

/* ---------------- AppMessage ---------------- */

/* Phone also pushes on its own onAppOpened, but keep asking until data lands:
 * one lost request must not leave the watch empty for half an hour. */
static AppTimer *s_req_timer;
static int s_tries;
/* Escalating, not machine-gun: every STATS_REQ makes the phone re-read Health
 * Connect, so six asks spread over ~25s is the budget. */
static const int REQ_DELAYS_MS[] = {400, 800, 1500, 3000, 6000, 12000};
#define REQ_MAX_TRIES ((int)(sizeof(REQ_DELAYS_MS) / sizeof(REQ_DELAYS_MS[0])))

static void req_timer(void *data);

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
  bool got_root = false, got_wo = false;
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
  if (got_root || got_wo) {
    s_loaded = true;
    stats_save_cache();
    if (s_req_timer) {
      app_timer_cancel(s_req_timer);
      s_req_timer = NULL;
    }
    strncpy(s->status, "Live", ST_LEN - 1);
    s->status[ST_LEN - 1] = '\0';
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

  s_card[0] = make_card_layer(GRect(0, 0, s_w, s_h), s_card_idx);
  s_card[1] = make_card_layer(GRect(0, 0, s_w, s_h), 0);
  layer_set_hidden(s_card[1], true);

  s_seam = layer_create(GRect(0, -4, s_w, 4));
  layer_set_update_proc(s_seam, seam_update);
  layer_set_hidden(s_seam, true);

  layer_add_child(root, s_card[1]);
  layer_add_child(root, s_card[0]);
  layer_add_child(root, s_seam); /* seam on top of both cards */
}

static void window_appear(Window *w) {
  (void)w;
  start_req_loop();
}

static void window_disappear(Window *w) {
  (void)w;
  if (s_req_timer) {
    app_timer_cancel(s_req_timer);
    s_req_timer = NULL;
  }
}

static void window_unload(Window *w) {
  (void)w;
  anim_cancel();
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
