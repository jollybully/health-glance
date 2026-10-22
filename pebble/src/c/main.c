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
static bool s_asked;

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

  layer_set_frame(s_seam, GRect(0, dir > 0 ? s_h - 4 : -4, s_w, 4));
  layer_set_hidden(s_seam, false);

  anim_push(li, lo, s_seam, dir, s_w, s_h, push_done, NULL);
}

/* ---------------- clicks ---------------- */

static void up_click(ClickRecognizerRef r, void *ctx) {
  (void)r;
  (void)ctx;
  if (s_card_idx < CARD_COUNT - 1) push(s_card_idx + 1, 1);
}

static void down_click(ClickRecognizerRef r, void *ctx) {
  (void)r;
  (void)ctx;
  if (anim_busy()) return; /* let the push settle before acting on the deck */
  if (s_card_idx == 0) {
    window_stack_pop_all(true); /* Down on Steps exits to the watchface */
    return;
  }
  push(s_card_idx - 1, -1);
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

static void send_req(void) {
#if HG_DEMO
  return;
#else
  if (s_asked) return;
  DictionaryIterator *it;
  if (app_message_outbox_begin(&it) != APP_MSG_OK) return;
  dict_write_int32(it, MESSAGE_KEY_STATS_REQ, 1);
  if (app_message_outbox_send() == APP_MSG_OK) s_asked = true;
#endif
}

static void req_timer(void *data) {
  (void)data;
  send_req();
}

static void outbox_failed(DictionaryIterator *it, AppMessageResult reason, void *ctx) {
  (void)it;
  (void)ctx;
  APP_LOG(APP_LOG_LEVEL_WARNING, "stats req failed: %d", (int)reason);
  s_asked = false;
  app_timer_register(1000, req_timer, NULL);
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
    int32_t iv = (t->type == TUPLE_INT) ? t->value->int32 : 0;

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

  if (got_wo) s->has_workout = true;
  if (got_root || got_wo) {
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
  app_timer_register(400, req_timer, NULL);
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
  app_message_open(512, 64);

  s_window = window_create();
  window_set_background_color(s_window, GColorWhite);
  window_set_click_config_provider(s_window, click_config);
  window_set_window_handlers(s_window, (WindowHandlers){
                                           .load = window_load,
                                           .appear = window_appear,
                                           .unload = window_unload,
                                       });
  window_stack_push(s_window, true);
}

static void deinit(void) {
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
