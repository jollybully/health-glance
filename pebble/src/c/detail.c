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
static bool s_no_live_hint; /* user asked for live while nothing was streaming */
static bool s_dirty;     /* series changed since the last flash write */
/* Totals for the day on screen (sent with that day's charts): steps, asleep,
 * deep, rem, hr_min, hr_max. */
static int s_day_totals[6];
static bool s_day_totals_ok;
static void clear_series_for(int card);
static int s_waiting_day = -1; /* day asked of the phone, -1 = none */
static void (*s_request_day)(int day);
static bool s_any;       /* any series has arrived at all */

static bool live_fresh(void) { return stats_live_fresh(); }

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
  char ctxw[14];
  if (s_day == 0) snprintf(ctxw, sizeof ctxw, "today");
  else if (s_day == 1) snprintf(ctxw, sizeof ctxw, "yesterday");
  else snprintf(ctxw, sizeof ctxw, "%d days ago", s_day);
  frame(ctx, CARD_D_STEPS, ctxw);

  const bool past = s_day > 0;
  if (past && peak <= 0) {
    /* We have nothing for that day. Showing today's numbers under a "2 days ago"
     * header is how data stops being trustworthy, so say nothing instead. */
    cards_hero_text(ctx, "", HERO_Y);
    label_left(ctx, "no data", GColorDarkGray, UNIT_Y);
    label_right(ctx, detail_day_pending() ? "loading" : "", GColorDarkGray, UNIT_Y);
    label_full(ctx, "phone sent nothing", GColorBlack, META_Y);
    cue(ctx);
    return;
  }
  if (peak > 0) {
    int sum = 0, active = 0;
    for (int i = 0; i < SERIES_SLOTS; i++) {
      sum += s_series[CARD_D_STEPS][i];
      if (s_series[CARD_D_STEPS][i] > 0) active++;
    }
    const int avg = active > 0 ? (sum / active) : 0;
    /* The hero is the same number the card showed, for the day on screen: a
     * drill-down adds context, it does not swap the metric. */
    if (past && s_day_totals_ok) {
      snprintf(a, sizeof a, "%d", s_day_totals[0]);
    } else {
      snprintf(a, sizeof a, "%d", g_stats.steps);
    }
    cards_hero_text(ctx, a, HERO_Y);
    /* LEFT names the number, RIGHT names the chart. One rule, every screen. */
    label_left(ctx, "steps", GColorDarkGray, UNIT_Y);
    label_right(ctx, "0-23h", GColorDarkGray, UNIT_Y);
    /* A real average, drawn only when it is derived from this day's hours — the
     * old line was decoration and implied a meaning it did not have. */
    if (avg > 0 && avg < peak) {
      int ay = BAR_Y - 2 - (avg * (BAR_Y - 2 - CHART_TOP)) / peak;
      fill(ctx, GColorDarkGray, GRect(X_INSET, ay, BW, 1));
    }
    for (int i = 0; i < SERIES_SLOTS; i++) {
      int h = (s_series[CARD_D_STEPS][i] * (BAR_Y - 2 - CHART_TOP)) / peak;
      bar(ctx, X_INSET + i * 8, 5, h, i == peak_hour ? GColorBlack : GColorJaegerGreen);
    }
    /* One short line: today gets the day's own totals, a past day gets its peak
     * hour. Two facts in one 180px line truncates, which is what happened. */
    if (past) {
      snprintf(a, sizeof a, "peak %d at %d-%d", peak * 100, peak_hour, (peak_hour + 1) % 24);
    } else {
      snprintf(a, sizeof a, "%d.%d km · %d cal", g_stats.dist_m / 1000,
               (g_stats.dist_m % 1000) / 100, g_stats.calories);
    }
    label_full(ctx, a, GColorBlack, META_Y);
    cue(ctx);
    return;
  } else {
    snprintf(a, sizeof a, "%d", g_stats.steps);
    cards_hero_text(ctx, a, HERO_Y);
    label_left(ctx, "steps", GColorDarkGray, UNIT_Y);
    label_right(ctx, "no series", GColorDarkGray, UNIT_Y);
  }

  snprintf(a, sizeof a, "%d.%d km · %d cal", g_stats.dist_m / 1000,
           (g_stats.dist_m % 1000) / 100, g_stats.calories);
  label_full(ctx, a, GColorBlack, META_Y);
  cue(ctx);
}

/* ---------------- sleep ---------------- */

static void draw_sleep(GContext *ctx) {
  char a[32], t1[8], t2[8];
  char ctxs[14];
  if (s_day == 0) snprintf(ctxs, sizeof ctxs, "last night");
  else if (s_day == 1) snprintf(ctxs, sizeof ctxs, "night before");
  else snprintf(ctxs, sizeof ctxs, "%d nights ago", s_day);
  frame(ctx, CARD_D_SLEEP, ctxs);
  const bool past = s_day > 0;
  bool any_stage = false;
  if (s_has_series[CARD_D_SLEEP]) {
    for (int i = 0; i < SERIES_SLOTS; i++) {
      if (s_series[CARD_D_SLEEP][i] != 0) any_stage = true;
    }
  }
  if (past && !any_stage) {
    cards_hero_text(ctx, "", HERO_Y);
    label_left(ctx, "no data", GColorDarkGray, UNIT_Y);
    label_right(ctx, detail_day_pending() ? "loading" : "", GColorDarkGray, UNIT_Y);
    label_full(ctx, "phone sent nothing", GColorBlack, META_Y);
    cue(ctx);
    return;
  }
  if (past && s_day_totals_ok && s_day_totals[1] > 0) {
    snprintf(a, sizeof a, "%d:%02d", s_day_totals[1] / 60, s_day_totals[1] % 60);
    cards_hero_text(ctx, a, HERO_Y);
    label_left(ctx, "asleep", GColorDarkGray, UNIT_Y);
  } else if (!past) {
    snprintf(a, sizeof a, "%d:%02d", g_stats.sleep_min / 60, g_stats.sleep_min % 60);
    cards_hero_text(ctx, a, HERO_Y);
    label_left(ctx, "asleep", GColorDarkGray, UNIT_Y);
  } else if (past) {
    cards_hero_text(ctx, "", HERO_Y);
    label_left(ctx, "asleep", GColorDarkGray, UNIT_Y);
  } else {
    snprintf(a, sizeof a, "%d", g_stats.deep);
    cards_hero_text(ctx, a, HERO_Y);
    label_left(ctx, "deep sleep", GColorDarkGray, UNIT_Y);
  }

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
    /* Only name the dominant stage when the hero is NOT the asleep total: two
     * different meanings under one number is what made these screens confusing. */
    if (!(past && s_day_totals_ok && s_day_totals[1] > 0)) {
      label_right(ctx, "stages", GColorDarkGray, UNIT_Y);
    }
  } else {
    label_right(ctx, "no series", GColorDarkGray, UNIT_Y);
  }

  if (!past) {
    stats_clock_to(t1, sizeof(t1), g_stats.sleep_start);
    stats_clock_to(t2, sizeof(t2), g_stats.sleep_end);
    snprintf(a, sizeof a, "%s-%s · %d:%02d rem", t1, t2, g_stats.rem / 60, g_stats.rem % 60);
    label_full(ctx, a, GColorBlack, META_Y);
  } else if (s_day_totals_ok) {
    snprintf(a, sizeof a, "deep %d:%02d · rem %d:%02d", s_day_totals[2] / 60,
             s_day_totals[2] % 60, s_day_totals[3] / 60, s_day_totals[3] % 60);
    label_full(ctx, a, GColorBlack, META_Y);
  } else {
    label_full(ctx, "stage timeline", GColorBlack, META_Y);
  }
  cue(ctx);
}

/* ---------------- heart ---------------- */

static void draw_heart(GContext *ctx) {
  char a[32];
  const bool live = s_live && live_fresh();
  char ctxh[14];
  if (live) {
    snprintf(ctxh, sizeof ctxh, "live");
  } else if (s_day == 0) {
    snprintf(ctxh, sizeof ctxh, "today");
  } else if (s_day == 1) {
    snprintf(ctxh, sizeof ctxh, "yesterday");
  } else {
    snprintf(ctxh, sizeof ctxh, "%d days ago", s_day);
  }
  frame(ctx, CARD_D_HEART, ctxh);

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
    snprintf(a, sizeof a, "%d", g_stats.resting_hr > 0 ? g_stats.resting_hr : peak);
    cards_hero_text(ctx, a, HERO_Y);
    label_left(ctx, "resting bpm", GColorDarkGray, UNIT_Y);
    /* resting rate as the datum, behind the bars */
    int floor_bpm = g_stats.resting_hr > 0 ? g_stats.resting_hr : 50;
    fill(ctx, GColorDarkGray,
         GRect(X_INSET, BAR_Y - 2 - ((floor_bpm - 40) * (BAR_Y - 2 - CHART_TOP)) / 120, BW, 1));
    for (int i = 0; i < SERIES_SLOTS; i++) {
      int v = s_series[CARD_D_HEART][i];
      int h = ((v - 40) * (BAR_Y - 2 - CHART_TOP)) / 120;
      bar(ctx, X_INSET + i * 8, 5, h, i == peak_hour ? GColorBlack : GColorImperialPurple);
    }
    label_right(ctx, "bpm by hour", GColorDarkGray, UNIT_Y);
  } else {
    snprintf(a, sizeof a, "%d", g_stats.resting_hr);
    cards_hero_text(ctx, a, HERO_Y);
    label_left(ctx, "resting bpm", GColorDarkGray, UNIT_Y);
    if (live_fresh()) {
      label_right(ctx, "SEL live", GColorDarkGray, UNIT_Y);
    } else if (s_no_live_hint) {
      label_right(ctx, "no live", GColorDarkGray, UNIT_Y);
    } else {
      label_right(ctx, "no series", GColorDarkGray, UNIT_Y);
    }
  }

  if (s_day > 0 && s_day_totals_ok) {
    snprintf(a, sizeof a, "peak %d · min %d", s_day_totals[5], s_day_totals[4]);
  } else {
    snprintf(a, sizeof a, "peak %d · min %d", g_stats.hr_max, g_stats.hr_min);
  }
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
  /* Unknown types must not claim to be a sport he did not do. */
  static const char *names[4] = {"walk", "run", "ride", "other"};
  return names[s_wo[i * 6 + 5] & 3];
}

/* The actual date of a session, not "today"/"earlier": he could not tell whether
 * a session was mis-dated because the label was relative. */
static void wo_when(int days_ago, char *out, size_t n) {
  static const char *mon[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  time_t now = time(NULL);
  struct tm tm;
  struct tm *t = localtime(&now);
  if (!t) {
    snprintf(out, n, "%dd", days_ago);
    return;
  }
  tm = *t;
  tm.tm_mday -= days_ago;
  tm.tm_isdst = -1;
  time_t then = mktime(&tm);
  struct tm *r = localtime(&then);
  if (!r) {
    snprintf(out, n, "%dd", days_ago);
    return;
  }
  if (days_ago == 0) {
    snprintf(out, n, "today");
  } else if (days_ago == 1) {
    snprintf(out, n, "yest");
  } else {
    snprintf(out, n, "%d %s", r->tm_mday, mon[r->tm_mon % 12]);
  }
}

static void draw_workouts(GContext *ctx) {
  char a[32];
  int n = wo_count();
  if (s_cursor >= n) s_cursor = 0;
  snprintf(a, sizeof a, n > 1 ? "%d recent" : "this week", n);
  frame(ctx, CARD_D_WORKOUTS, a);

  /* One bar per session, not per day. A weekly chart cannot show which of two
   * sessions on the same day is selected, which is exactly what he reported as
   * bad UX: the highlight had nowhere unambiguous to go. Bars now map 1:1 to the
   * sessions Up/Down walks, so the black bar and the block under it always say
   * which one is on screen. */
  if (n > 0) {
    int peak = 0;
    for (int i = 0; i < n; i++) {
      int secs, dist, avg, days;
      wo_entry(i, &secs, &dist, &avg, &days);
      if (secs > peak) peak = secs;
    }
    if (peak < 60) peak = 60;
    const int pitch = BW / (n > 0 ? n : 1);
    const int bw = pitch > 10 ? 10 : pitch - 2;
    for (int i = 0; i < n; i++) {
      int secs, dist, avg, days;
      wo_entry(i, &secs, &dist, &avg, &days);
      int h = (secs * (BAR_Y - 2 - CHART_TOP)) / peak;
      if (h < 3) h = 3;
      int x = X_INSET + i * pitch;
      bar(ctx, x, bw, h, i == s_cursor ? GColorBlack : GColorOrange);
      if (i == s_cursor) {
        /* the block on the rail: position, so selection is not shade-dependent */
        fill(ctx, GColorBlack, GRect(x - 1, BAR_Y, bw + 2, BAR_H));
      }
    }
  } else if (s_has_series[CARD_D_WORKOUTS]) {
    /* no session history: fall back to the weekly minutes chart */
    for (int i = 0; i < 7; i++) {
      int x = X_INSET + i * 26;
      int m = s_series[CARD_D_WORKOUTS][i];
      if (m <= 0) {
        fill(ctx, GColorLightGray, GRect(x, BAR_Y - 2, 16, 2));
        continue;
      }
      bar(ctx, x, 16, (m * (BAR_Y - 2 - CHART_TOP)) / 90, GColorOrange);
    }
  }

  if (n > 0) {
    int secs, dist, avg, days;
    wo_entry(s_cursor, &secs, &dist, &avg, &days);
    snprintf(a, sizeof a, "%d:%02d", secs / 60, secs % 60);
    cards_hero_text(ctx, a, HERO_Y);
    char when[12];
    wo_when(days, when, sizeof when);
    snprintf(a, sizeof a, "%s · %s", wo_name(s_cursor), when);
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
  s_no_live_hint = false;
  s_cursor = 0;
  s_day = 0;
  /* If the strap is streaming, open straight into live: the user tapped into the
   * Heart screen precisely to see their pulse, and requiring a fresh-sample-timed
   * Up press meant the live view usually never appeared. Up/Down still toggles
   * back to today's graph, and the cap says which one is on screen. */
  s_live = (card == CARD_D_HEART) && live_fresh();
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
  if (s_card == CARD_D_STEPS || s_card == CARD_D_SLEEP || s_card == CARD_D_HEART) {
    if (s_day > 0 && s_request_day) {
      s_day--;
      s_day_totals_ok = false;
      memset(s_day_totals, 0, sizeof(s_day_totals));
      if (s_day == 0) {
        s_waiting_day = -1; /* today's charts are already on the watch */
      } else {
        s_waiting_day = s_day;
        clear_series_for(s_card);
      }
      if (s_card == CARD_D_HEART && s_day == 0) s_live = live_fresh();
      layer_mark_dirty(s_layer);
      s_request_day(s_day);
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
  if (s_card == CARD_D_STEPS || s_card == CARD_D_SLEEP || s_card == CARD_D_HEART) {
    if (s_day < 29 && s_request_day) {
      s_day++;
      s_day_totals_ok = false;
      memset(s_day_totals, 0, sizeof(s_day_totals));
      s_waiting_day = s_day;
      /* asking for history means leaving the live view */
      if (s_card == CARD_D_HEART) s_live = false;
      clear_series_for(s_card);
      layer_mark_dirty(s_layer);
      s_request_day(s_day);
    }
    return true;
  }
  return true;
}

/* Select inside a detail: on Heart it switches between the strap and the graph.
 * Day cycling owns Up/Down on every screen, so this is the only free button. */
void detail_select(void) {
  if (s_card != CARD_D_HEART) return;
  if (live_fresh()) {
    s_live = !s_live;
  } else {
    s_no_live_hint = true;
  }
  layer_mark_dirty(s_layer);
}

void detail_set_series(int card, const uint8_t *data, int len) {
  if (card < 0 || card > 3 || !data) return;
  int n = len < SERIES_SLOTS ? len : SERIES_SLOTS;
  memset(s_series[card], 0, SERIES_SLOTS);
  memcpy(s_series[card], data, n);
  s_has_series[card] = true;
  s_any = true;
  s_dirty = true;
  detail_redraw();
}

void detail_set_workouts(const uint8_t *data, int len) {
  if (!data) return;
  int n = len > WO_MAX * 6 ? WO_MAX * 6 : len;
  memset(s_wo, 0, sizeof s_wo);
  memcpy(s_wo, data, n);
  s_wo_len = n;
  s_any = true;
  s_dirty = true;
  s_cursor = 0;
  detail_redraw();
}

void detail_set_day(int day_offset) {
  s_day = day_offset;
  if (day_offset == s_waiting_day) s_waiting_day = -1;
  detail_redraw();
}

void detail_set_request_day(void (*fn)(int day)) { s_request_day = fn; }

bool detail_day_pending(void) { return s_waiting_day >= 0 && s_waiting_day == s_day; }

int detail_day(void) { return s_day; }

/* Drop the charts we are leaving behind. Without this, cycling to a day with no
 * data kept showing the PREVIOUS day's chart under the new day's header, which is
 * worse than showing nothing: the numbers look real and are for the wrong day. */
void detail_set_day_total(int which, int value) {
  if (which < 0 || which > 5) return;
  s_day_totals[which] = value;
  s_day_totals_ok = true;
  detail_redraw();
}

static void clear_series_for(int card) {
  if (card < 0 || card > 3) return;
  memset(s_series[card], 0, SERIES_SLOTS);
  s_has_series[card] = false;
}

/* ---------------- persistence ---------------- */

bool detail_has_series(void) { return s_any; }

bool detail_series_dirty(void) { return s_dirty; }

void detail_series_mark_saved(void) { s_dirty = false; }

void detail_export_series(SeriesBlob *out) {
  memset(out, 0, sizeof(*out));
  out->magic_ok = 1;
  for (int i = 0; i < 4; i++) out->have[i] = s_has_series[i] ? 1 : 0;
  memcpy(out->steps, s_series[CARD_D_STEPS], SERIES_SLOTS);
  memcpy(out->sleep, s_series[CARD_D_SLEEP], SERIES_SLOTS);
  memcpy(out->heart, s_series[CARD_D_HEART], SERIES_SLOTS);
  memcpy(out->week, s_series[CARD_D_WORKOUTS], 7);
  out->wo_len = (uint8_t)(s_wo_len > WO_MAX * 6 ? WO_MAX * 6 : s_wo_len);
  if (out->wo_len) memcpy(out->wo, s_wo, out->wo_len);
}

void detail_import_series(const SeriesBlob *in) {
  if (!in->magic_ok) return;
  s_has_series[CARD_D_STEPS] = in->have[0] != 0;
  s_has_series[CARD_D_SLEEP] = in->have[1] != 0;
  s_has_series[CARD_D_HEART] = in->have[2] != 0;
  s_has_series[CARD_D_WORKOUTS] = in->have[3] != 0;
  memcpy(s_series[CARD_D_STEPS], in->steps, SERIES_SLOTS);
  memcpy(s_series[CARD_D_SLEEP], in->sleep, SERIES_SLOTS);
  memcpy(s_series[CARD_D_HEART], in->heart, SERIES_SLOTS);
  memcpy(s_series[CARD_D_WORKOUTS], in->week, 7);
  s_wo_len = in->wo_len <= WO_MAX * 6 ? in->wo_len : 0;
  if (s_wo_len) memcpy(s_wo, in->wo, s_wo_len);
  s_any = in->have[0] || in->have[1] || in->have[2] || in->have[3] || s_wo_len > 0;
  s_dirty = false;
  detail_redraw();
}
