#include "detail.h"

#include <pebble.h>

#include "cards.h"
#include "stats.h"

/* Drill-down screens on the deck's own skeleton (proven in the mockup round):
 *
 *   y   0..36  cap       36px card colour, 24 bold caps title, "<" then the title
 *   y   2..37  2px black page rule directly under the cap  (cards never have one)
 *   y  44..94  hero      the deck's own blocky digits (cards_hero_text)
 *   y  98      label     18 bold DarkGray, right-hand sub-label in its own column
 *   y 120..170 chart     bars grown up from the rail
 *   y 170..178 rail      the card's 8px LightGray bar, now divided into slots
 *   y 178      meta      18 bold Black
 *   y 202      cue       18 bold DarkGray (synced/waiting)
 *
 * The chevron, the page rule and the divided rail are the three cues that say
 * "one level in": the deck has none of them. */

#define X_INSET 10
#define CAP_H 36
#define HERO_Y 44
#define UNIT_Y 98
#define CHART_TOP 120
#define BAR_Y 170
#define BAR_H 8
#define META_Y 178
#define CUE_Y 202
#define BW 180

#define SERIES_SLOTS 24
#define WO_MAX 7

enum { CARD_D_STEPS = 0, CARD_D_SLEEP = 1, CARD_D_HEART = 2, CARD_D_WORKOUTS = 3 };

static const GColor CAP_COL[4] = {GColorJaegerGreen, GColorCobaltBlue, GColorImperialPurple,
                                  GColorOrange};
/* stage colours, identical to the sleep card's 8px stage bar */
static const GColor STAGE_COL[4] = {GColorDukeBlue, GColorCobaltBlue, GColorIndigo,
                                    GColorWindsorTan};
static const int STAGE_H[4] = {20, 12, 30, 6}; /* deep / light / rem / awake */

/* 0 = deep, 1 = light, 2 = rem, 3 = awake (what the companion sends) */
static const char *STAGE_NAME[4] = {"deep", "light", "rem", "awake"};

static Layer *s_layer;
static GRect s_bounds;
static int s_card = -1;   /* -1 = closed */
static int s_cursor;      /* workout index, or 0/1 for heart today/live */

static uint8_t s_series[4][SERIES_SLOTS];
static bool s_has_series[4];
static uint8_t s_wo[WO_MAX * 6];
static int s_wo_len;
static int s_day;        /* 0 today, 1 yesterday ... */
static bool s_live;      /* heart screen showing the strap instead of the graph */

static bool live_fresh(void) {
  /* HR_LIVE_TS is seconds; give it 20s before calling it live */
  if (g_live.bpm <= 0 || g_live.ts == 0) return false;
  return (time(NULL) - (time_t)g_live.ts) <= 20;
}

static void fill(GContext *ctx, GColor c, GRect r) {
  graphics_context_set_fill_color(ctx, c);
  graphics_fill_rect(ctx, r, 0, GCornerNone);
}

static void text_at(GContext *ctx, const char *s, GFont f, GColor c, GRect r, GTextAlignment a) {
  if (!s || !s[0]) return;
  graphics_context_set_text_color(ctx, c);
  graphics_draw_text(ctx, s, f, r, GTextOverflowModeTrailingEllipsis, a, NULL);
}

static GFont f18(void) { return fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD); }
static GFont f14(void) { return fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD); }
static GFont f24(void) { return fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD); }

static void label_left(GContext *ctx, const char *s, GColor c, int y) {
  text_at(ctx, s, f18(), c, GRect(X_INSET, y, 96, 22), GTextAlignmentLeft);
}

static void label_full(GContext *ctx, const char *s, GColor c, int y) {
  text_at(ctx, s, f18(), c, GRect(X_INSET, y, BW, 22), GTextAlignmentLeft);
}

static void label_right(GContext *ctx, const char *s, GColor c, int y) {
  /* its own column, so a long left label can never run into it */
  text_at(ctx, s, f18(), c, GRect(X_INSET + 98, y, BW - 98, 22), GTextAlignmentRight);
}

/* ---------------- frame ---------------- */

static void frame(GContext *ctx, int card, const char *context) {
  static const char *titles[4] = {"STEPS", "SLEEP", "HEART", "WORKOUTS"};
  fill(ctx, GColorWhite, GRect(0, 0, 200, 228));
  fill(ctx, CAP_COL[card], GRect(0, 0, 200, CAP_H));
  /* "<" needs a real line height (26px) or 18 bold draws nothing at all */
  text_at(ctx, "<", f18(), GColorWhite, GRect(2, 2, 14, 26), GTextAlignmentLeft);
  text_at(ctx, titles[card], f24(), GColorWhite, GRect(14, 1, 110, 32), GTextAlignmentLeft);
  text_at(ctx, context, f14(), GColorWhite, GRect(200 - X_INSET - 92, 9, 92, 18),
          GTextAlignmentRight);
  fill(ctx, GColorBlack, GRect(0, CAP_H, 200, 2)); /* the page rule */
  fill(ctx, GColorLightGray, GRect(X_INSET, BAR_Y, BW, BAR_H));
  for (int x = X_INSET + 8; x < X_INSET + BW; x += 8) {
    fill(ctx, GColorWhite, GRect(x, BAR_Y, 1, BAR_H)); /* slot dividers */
  }
}

static const char *strlwr_title(const char *s) {
  static char buf[ST_LEN];
  int i = 0;
  for (; s[i] && i < ST_LEN - 1; i++) {
    char c = s[i];
    buf[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
  }
  buf[i] = '\0';
  return buf;
}

static void bar(GContext *ctx, int x, int w, int h, GColor c) {
  if (h < 2) h = 2;
  if (h > BAR_Y - CHART_TOP) h = BAR_Y - CHART_TOP;
  fill(ctx, c, GRect(x, BAR_Y - h, w, h));
}

static void cue(GContext *ctx) {
  label_left(ctx, stats_footer(), GColorDarkGray, CUE_Y);
}

/* ---------------- steps ---------------- */

static void draw_steps(GContext *ctx) {
  char a[32];
  int peak = 0, peak_hour = 0;
  for (int i = 0; i < SERIES_SLOTS; i++) {
    if (!s_has_series[CARD_D_STEPS]) break;
    if (s_series[CARD_D_STEPS][i] > peak) {
      peak = s_series[CARD_D_STEPS][i];
      peak_hour = i;
    }
  }
  frame(ctx, CARD_D_STEPS, s_day == 0 ? "today" : "earlier");

  if (peak > 0) {
    snprintf(a, sizeof a, "%d", peak * 100); /* series is steps/100 per hour */
    cards_hero_text(ctx, a, HERO_Y);
    label_left(ctx, "peak hour", GColorDarkGray, UNIT_Y);
    fill(ctx, GColorDarkGray, GRect(X_INSET, CHART_TOP + 8, BW, 1));
    for (int i = 0; i < SERIES_SLOTS; i++) {
      int h = (s_series[CARD_D_STEPS][i] * (BAR_Y - 2 - CHART_TOP)) / peak;
      bar(ctx, X_INSET + i * 8, 5, h, i == peak_hour ? GColorBlack : GColorJaegerGreen);
    }
    snprintf(a, sizeof a, "%d-%d", peak_hour, (peak_hour + 1) % 24);
    label_right(ctx, a, GColorDarkGray, UNIT_Y);
  } else {
    snprintf(a, sizeof a, "%d", g_stats.steps);
    cards_hero_text(ctx, a, HERO_Y);
    label_left(ctx, "steps", GColorDarkGray, UNIT_Y);
    label_right(ctx, "waiting", GColorDarkGray, UNIT_Y);
  }

  snprintf(a, sizeof a, "%d.%d km · %d cal", g_stats.dist_m / 1000, (g_stats.dist_m % 1000) / 100,
           g_stats.calories);
  label_full(ctx, a, GColorBlack, META_Y);
  cue(ctx);
}

/* ---------------- sleep ---------------- */

static void draw_sleep(GContext *ctx) {
  char a[32], t1[8], t2[8];
  frame(ctx, CARD_D_SLEEP, s_day == 0 ? "last night" : "earlier");
  snprintf(a, sizeof a, "%d", g_stats.deep);
  cards_hero_text(ctx, a, HERO_Y);
  label_left(ctx, "deep sleep", GColorDarkGray, UNIT_Y);

  if (s_has_series[CARD_D_SLEEP]) {
    for (int i = 0; i < SERIES_SLOTS; i++) {
      int band = s_series[CARD_D_SLEEP][i];
      if (band > 3) band = 3;
      bar(ctx, X_INSET + i * 8, 6, STAGE_H[band], STAGE_COL[band]);
    }
    /* the dominant stage goes in the sub-label so the bars are named, not guessed */
    int count[4] = {0, 0, 0, 0};
    for (int i = 0; i < SERIES_SLOTS; i++) {
      int band = s_series[CARD_D_SLEEP][i];
      if (band <= 3) count[band]++;
    }
    int most = 0;
    for (int i = 1; i < 4; i++) {
      if (count[i] > count[most]) most = i;
    }
    label_right(ctx, STAGE_NAME[most], GColorDarkGray, UNIT_Y);
  } else {
    label_right(ctx, "waiting", GColorDarkGray, UNIT_Y);
  }

  stats_clock_to(t1, sizeof(t1), g_stats.sleep_start);
  stats_clock_to(t2, sizeof(t2), g_stats.sleep_end);
  snprintf(a, sizeof a, "%s-%s · %d:%02d rem", t1, t2, g_stats.rem / 60, g_stats.rem % 60);
  label_full(ctx, a, GColorBlack, META_Y);
  cue(ctx);
}

/* ---------------- heart ---------------- */

static void draw_heart(GContext *ctx) {
  char a[32];
  const bool live = s_live && live_fresh();
  frame(ctx, CARD_D_HEART, live ? "live" : (s_day == 0 ? "today" : "earlier"));

  if (live) {
    /* the strap: hero is the current bpm, and the rail becomes a min/max gauge */
    snprintf(a, sizeof a, "%d", g_live.bpm);
    cards_hero_text(ctx, a, HERO_Y);
    label_left(ctx, "live bpm", GColorDarkGray, UNIT_Y);
    if (g_live.batt > 0) {
      snprintf(a, sizeof a, "batt %d%%", g_live.batt);
    } else {
      a[0] = 0;
    }
    label_right(ctx, a, GColorDarkGray, UNIT_Y);

    int lo = g_live.min > 0 ? g_live.min : g_live.bpm;
    int hi = g_live.max > g_live.bpm ? g_live.max : g_live.bpm;
    int span = hi - lo;
    if (span < 1) span = 1;
    int w = ((g_live.bpm - lo) * BW) / span;
    if (w < 3) w = 3;
    fill(ctx, GColorImperialPurple, GRect(X_INSET, BAR_Y, w, BAR_H));

    snprintf(a, sizeof a, "min %d · max %d · live", lo, hi);
    label_full(ctx, a, GColorBlack, META_Y);
    cue(ctx);
    return;
  }

  int peak = 0, peak_hour = 0;
  for (int i = 0; i < SERIES_SLOTS; i++) {
    if (!s_has_series[CARD_D_HEART]) break;
    if (s_series[CARD_D_HEART][i] > peak) {
      peak = s_series[CARD_D_HEART][i];
      peak_hour = i;
    }
  }
  if (peak > 0) {
    snprintf(a, sizeof a, "%d", peak);
    cards_hero_text(ctx, a, HERO_Y);
    label_left(ctx, "peak bpm", GColorDarkGray, UNIT_Y);
    /* resting rate as the datum, behind the bars */
    int floor_bpm = g_stats.resting_hr > 0 ? g_stats.resting_hr : 50;
    fill(ctx, GColorDarkGray,
         GRect(X_INSET, BAR_Y - 2 - ((floor_bpm - 40) * (BAR_Y - 2 - CHART_TOP)) / 120, BW, 1));
    for (int i = 0; i < SERIES_SLOTS; i++) {
      int v = s_series[CARD_D_HEART][i];
      int h = ((v - 40) * (BAR_Y - 2 - CHART_TOP)) / 120;
      bar(ctx, X_INSET + i * 8, 5, h, i == peak_hour ? GColorBlack : GColorImperialPurple);
    }
    snprintf(a, sizeof a, "%d-%d", peak_hour, (peak_hour + 1) % 24);
    label_right(ctx, a, GColorDarkGray, UNIT_Y);
  } else {
    snprintf(a, sizeof a, "%d", g_stats.resting_hr);
    cards_hero_text(ctx, a, HERO_Y);
    label_left(ctx, "resting bpm", GColorDarkGray, UNIT_Y);
    label_right(ctx, live_fresh() ? "live ↑" : "waiting", GColorDarkGray, UNIT_Y);
  }

  snprintf(a, sizeof a, "min %d · max %d", g_stats.hr_min, g_stats.hr_max);
  label_full(ctx, a, GColorBlack, META_Y);
  cue(ctx);
}

/* ---------------- workouts ---------------- */

/* The phone always sends a fixed 42-byte buffer, zero-padded past the real
 * sessions, so count the contiguous non-zero prefix: a padded slot has a zero
 * duration and must not appear as a 0:00 workout. */
static int wo_count(void) {
  int n = s_wo_len / 6;
  int c = 0;
  while (c < n) {
    int secs = s_wo[c * 6] | (s_wo[c * 6 + 1] << 8);
    if (secs <= 0) break;
    c++;
  }
  return c;
}

static void wo_entry(int i, int *secs, int *dist_x10, int *avg, int *days) {
  int o = i * 6;
  *secs = s_wo[o] | (s_wo[o + 1] << 8); /* duration in seconds (m:ss hero) */
  *dist_x10 = s_wo[o + 2];
  *avg = s_wo[o + 3];
  *days = s_wo[o + 4];
  /* byte 5 reserved */
}

static const char *wo_name(int i) {
  static const char *names[4] = {"walk", "run", "ride", "swim"};
  return names[s_wo[i * 6 + 5] & 3];
}

static void draw_workouts(GContext *ctx) {
  char a[32];
  int n = wo_count();
  if (s_cursor >= n) s_cursor = 0;
  snprintf(a, sizeof a, n > 1 ? "%d recent" : "this week", n);
  frame(ctx, CARD_D_WORKOUTS, a);

  if (s_has_series[CARD_D_WORKOUTS]) {
    for (int i = 0; i < 7; i++) {
      int x = X_INSET + i * 26;
      int m = s_series[CARD_D_WORKOUTS][i];
      if (m <= 0) {
        fill(ctx, GColorLightGray, GRect(x, BAR_Y - 2, 16, 2)); /* rest day */
        continue;
      }
      bar(ctx, x, 16, (m * (BAR_Y - 2 - CHART_TOP)) / 90, i == 6 ? GColorBlack : GColorOrange);
    }
  }

  if (n > 0) {
    int secs, dist, avg, days;
    wo_entry(s_cursor, &secs, &dist, &avg, &days);
    snprintf(a, sizeof a, "%d:%02d", secs / 60, secs % 60);
    cards_hero_text(ctx, a, HERO_Y);
    snprintf(a, sizeof a, "%s · %s", wo_name(s_cursor),
             days == 0 ? "today" : (days == 1 ? "yest" : "earlier"));
    label_left(ctx, a, GColorDarkGray, UNIT_Y);
    snprintf(a, sizeof a, "%d of %d", s_cursor + 1, n);
    label_right(ctx, a, GColorDarkGray, UNIT_Y);
    snprintf(a, sizeof a, "%d.%d km · %d avg hr", dist / 10, dist % 10, avg);
    label_full(ctx, a, GColorBlack, META_Y);
  } else if (g_stats.has_workout && g_stats.wo_dur > 0) {
    /* no history yet: show today's session from the summary keys */
    snprintf(a, sizeof a, "%d:%02d", g_stats.wo_dur / 60, g_stats.wo_dur % 60);
    cards_hero_text(ctx, a, HERO_Y);
    label_left(ctx, g_stats.wo_title[0] ? strlwr_title(g_stats.wo_title) : "workout",
               GColorDarkGray, UNIT_Y);
    label_right(ctx, "today", GColorDarkGray, UNIT_Y);
    snprintf(a, sizeof a, "%d.%d km · %d avg hr", g_stats.wo_dist / 1000,
             (g_stats.wo_dist % 1000) / 100, g_stats.wo_avg);
    label_full(ctx, a, GColorBlack, META_Y);
  } else {
    cards_hero_text(ctx, "0", HERO_Y);
    label_left(ctx, "sessions", GColorDarkGray, UNIT_Y);
    label_full(ctx, "no workouts synced", GColorBlack, META_Y);
  }
  cue(ctx);
}

/* ---------------- public ---------------- */

void detail_draw(GContext *ctx, GRect bounds, int card) {
  (void)bounds;
  switch (card) {
    case CARD_D_STEPS: draw_steps(ctx); break;
    case CARD_D_SLEEP: draw_sleep(ctx); break;
    case CARD_D_HEART: draw_heart(ctx); break;
    default: draw_workouts(ctx); break;
  }
}

static void layer_update(Layer *l, GContext *ctx) {
  detail_draw(ctx, layer_get_bounds(l), s_card);
}

void detail_redraw(void) {
  if (s_layer && s_card >= 0) layer_mark_dirty(s_layer);
}

void detail_init(Layer *parent, GRect bounds) {
  s_bounds = bounds;
  s_layer = layer_create(bounds);
  layer_set_update_proc(s_layer, layer_update);
  layer_set_hidden(s_layer, true);
  layer_add_child(parent, s_layer);
}

void detail_deinit(void) {
  if (s_layer) {
    layer_destroy(s_layer);
    s_layer = NULL;
  }
  s_card = -1;
}

bool detail_is_open(void) { return s_card >= 0; }

int detail_card(void) { return s_card; }

void detail_open(int card) {
  if (!s_layer || card < 0 || card > 3) return;
  s_card = card;
  s_cursor = 0;
  s_day = 0;
  /* Live is opt-in: the graph is the default even when the strap is streaming,
   * and Up switches to the live value. A screen that silently changes what the
   * hero number means is exactly what "glance" must not do. */
  s_live = false;
  layer_set_hidden(s_layer, false);
  layer_mark_dirty(s_layer);
  APP_LOG(APP_LOG_LEVEL_INFO, "detail open %d live=%d", card, (int)s_live);
}

void detail_close(void) {
  s_card = -1;
  s_live = false;
  if (s_layer) layer_set_hidden(s_layer, true);
}

bool detail_up(void) {
  if (s_card < 0) return false;
  if (s_card == CARD_D_WORKOUTS && wo_count() > 1) {
    s_cursor = (s_cursor - 1 + wo_count()) % wo_count();
    layer_mark_dirty(s_layer);
    return true;
  }
  if (s_card == CARD_D_HEART) {
    if (live_fresh()) { /* only offer live when there is something live to show */
      s_live = !s_live;
      layer_mark_dirty(s_layer);
    }
    return true;
  }
  return true; /* consumed: the deck must not page while a detail is open */
}

bool detail_down(void) {
  if (s_card < 0) return false;
  if (s_card == CARD_D_WORKOUTS && wo_count() > 1) {
    s_cursor = (s_cursor + 1) % wo_count();
    layer_mark_dirty(s_layer);
    return true;
  }
  if (s_card == CARD_D_HEART) {
    s_live = false;
    layer_mark_dirty(s_layer);
    return true;
  }
  return true;
}

void detail_set_series(int card, const uint8_t *data, int len) {
  if (card < 0 || card > 3 || !data) return;
  int n = len < SERIES_SLOTS ? len : SERIES_SLOTS;
  memset(s_series[card], 0, SERIES_SLOTS);
  memcpy(s_series[card], data, n);
  s_has_series[card] = true;
  detail_redraw();
}

void detail_set_workouts(const uint8_t *data, int len) {
  if (!data) return;
  int n = len > WO_MAX * 6 ? WO_MAX * 6 : len;
  memset(s_wo, 0, sizeof s_wo);
  memcpy(s_wo, data, n);
  s_wo_len = n;
  s_cursor = 0;
  detail_redraw();
}

void detail_set_day(int day_offset) {
  s_day = day_offset;
  detail_redraw();
}
