#include "mockups.h"

#if HG_MOCKUP

/* Drill-down mockups, one screen per card, all on the same grammar:
 *
 *   cap      title 24 bold at x=10,y=1  ·  one-word context, 14 bold, right
 *   hero     ONE visual only: the shared 24-slot chart, or the workout list
 *   lines    at most TWO supporting lines, 18 bold, x=10, y=156 and y=182
 *   nothing  no label/value tables, no footer chrome, no invented scores
 *
 * "Glance" means the detail screen answers exactly one question the card cannot:
 * the shape of the day/night, or which workouts came before this one. */

#include <pebble.h>
#include "stats.h"

#define X 10
#define BW 180
#define CHART_TOP 52
#define CHART_H 76
#define LINE1 158
#define LINE2 184

static void fill(GContext *ctx, GColor c, GRect r) {
  graphics_context_set_fill_color(ctx, c);
  graphics_fill_rect(ctx, r, 0, GCornerNone);
}

static void tx(GContext *ctx, const char *s, GFont f, GColor c, GRect r, GTextAlignment a) {
  if (!s || !s[0]) return;
  graphics_context_set_text_color(ctx, c);
  graphics_draw_text(ctx, s, f, r, GTextOverflowModeTrailingEllipsis, a, NULL);
}

static GFont f24(void) { return fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD); }
static GFont f18(void) { return fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD); }
static GFont f14(void) { return fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD); }

static void chrome(GContext *ctx, int w, GColor cap, const char *title, const char *context) {
  fill(ctx, GColorWhite, GRect(0, 0, w, 228));
  fill(ctx, cap, GRect(0, 0, w, 36));
  tx(ctx, title, f24(), GColorWhite, GRect(X, 1, BW - 60, 32), GTextAlignmentLeft);
  tx(ctx, context, f14(), GColorWhite, GRect(110, 11, 80, 20), GTextAlignmentRight);
}

static void line(GContext *ctx, int y, const char *s) {
  tx(ctx, s, f18(), GColorDarkGray, GRect(X, y, BW, 24), GTextAlignmentLeft);
}

/* The one chart style: 24 slots, 6 px bars on 8 px pitch, baseline rule, two
 * endpoint labels. Every charting detail screen uses exactly this. */
static void chart24(GContext *ctx, const int v[24], int maxv, GColor col, const char *left,
                    const char *right) {
  for (int i = 0; i < 24; i++) {
    int bh = maxv > 0 ? (v[i] * CHART_H) / maxv : 0;
    if (v[i] > 0 && bh < 2) bh = 2;
    if (bh > 0) fill(ctx, col, GRect(X + i * 8, CHART_TOP + CHART_H - bh, 6, bh));
  }
  fill(ctx, GColorLightGray, GRect(X, CHART_TOP + CHART_H, BW, 1));
  tx(ctx, left, f14(), GColorDarkGray, GRect(X, CHART_TOP + CHART_H + 3, 60, 18),
     GTextAlignmentLeft);
  tx(ctx, right, f14(), GColorDarkGray, GRect(130, CHART_TOP + CHART_H + 3, 60, 18),
     GTextAlignmentRight);
}

/* ---------------- steps: the shape of the day ---------------- */

static void steps_day(GContext *ctx, int w) {
  static const int hourly[24] = {0, 0, 0, 0, 0, 0, 80, 180, 320, 520, 780, 1240,
                                 960, 680, 860, 600, 420, 500, 720, 380, 260, 120, 40, 0};
  char a[48];
  chrome(ctx, w, GColorJaegerGreen, "STEPS", "TODAY");
  chart24(ctx, hourly, 1240, GColorJaegerGreen, "6a", "6p");
  snprintf(a, sizeof a, "%d.%d km · %d cal", g_stats.dist_m / 1000,
           (g_stats.dist_m % 1000) / 100, g_stats.calories);
  line(ctx, LINE1, a);
  snprintf(a, sizeof a, "%d%% of 10,000", g_stats.steps / 100);
  line(ctx, LINE2, a);
}

/* ---------------- sleep: the shape of the night ---------------- */

static void sleep_night(GContext *ctx, int w) {
  /* 24 slots across the night, one block per slot at its stage's height */
  static const uint8_t stage[24] = {3, 2, 1, 0, 0, 1, 2, 0, 1, 1, 0, 2,
                                    1, 0, 0, 1, 2, 1, 0, 0, 1, 2, 3, 3};
  const GColor cols[4] = {GColorDukeBlue, GColorCobaltBlue, GColorIndigo, GColorWindsorTan};
  char a[48], t1[8], t2[8];
  chrome(ctx, w, GColorCobaltBlue, "SLEEP", "LAST NIGHT");
  for (int i = 0; i < 24; i++) {
    int band = stage[i];
    fill(ctx, cols[band], GRect(X + i * 8, CHART_TOP + band * 18, 6, 18));
  }
  fill(ctx, GColorLightGray, GRect(X, CHART_TOP + CHART_H, BW, 1));
  stats_clock_to(t1, sizeof(t1), g_stats.sleep_start);
  stats_clock_to(t2, sizeof(t2), g_stats.sleep_end);
  tx(ctx, t1, f14(), GColorDarkGray, GRect(X, CHART_TOP + CHART_H + 3, 60, 18),
     GTextAlignmentLeft);
  tx(ctx, t2, f14(), GColorDarkGray, GRect(130, CHART_TOP + CHART_H + 3, 60, 18),
     GTextAlignmentRight);
  snprintf(a, sizeof a, "deep %d:%02d · REM %d:%02d", g_stats.deep / 60, g_stats.deep % 60,
           g_stats.rem / 60, g_stats.rem % 60);
  line(ctx, LINE1, a);
  snprintf(a, sizeof a, "%d:%02d asleep", g_stats.sleep_min / 60, g_stats.sleep_min % 60);
  line(ctx, LINE2, a);
}

/* ---------------- heart: the shape of the day ---------------- */

static void heart_day(GContext *ctx, int w) {
  static const int hr[24] = {52, 50, 49, 50, 55, 61, 78, 96, 88, 72, 84, 110,
                             132, 96, 88, 92, 104, 118, 96, 84, 74, 66, 58, 54};
  char a[48];
  chrome(ctx, w, GColorImperialPurple, "HEART", "TODAY");
  chart24(ctx, hr, 132, GColorImperialPurple, "6a", "6p");
  snprintf(a, sizeof a, "resting %d bpm", g_stats.resting_hr);
  line(ctx, LINE1, a);
  snprintf(a, sizeof a, "range %d-%d", g_stats.hr_min, g_stats.hr_max);
  line(ctx, LINE2, a);
}

/* ---------------- workout: what came before ---------------- */

static void wo_recent(GContext *ctx, int w) {
  static const char *name[3] = {"WALK", "RUN", "RIDE"};
  static const char *when[3] = {"today", "yesterday", "Tue"};
  static const char *dur[3] = {"31:15", "42:18", "1:02"};
  static const char *meta[3] = {"4.3 km · 137 avg", "6.1 km · 148 avg", "18 km · 121 avg"};
  chrome(ctx, w, GColorOrange, "WORKOUTS", "3 RECENT");
  for (int i = 0; i < 3; i++) {
    int y = 50 + i * 58;
    if (i == 0) fill(ctx, GColorOrange, GRect(0, y - 8, 3, 50)); /* current: one accent bar */
    tx(ctx, name[i], f18(), GColorBlack, GRect(X, y, 100, 22), GTextAlignmentLeft);
    tx(ctx, dur[i], f24(), GColorBlack, GRect(110, y - 5, 80, 28), GTextAlignmentRight);
    tx(ctx, meta[i], f14(), GColorDarkGray, GRect(X, y + 20, 130, 18), GTextAlignmentLeft);
    tx(ctx, when[i], f14(), GColorDarkGray, GRect(120, y + 20, 70, 18), GTextAlignmentRight);
  }
}

/* ---------------- public ---------------- */

int mockups_count(void) { return 4; }

void mockups_draw(GContext *ctx, GRect bounds, int screen) {
  const int w = bounds.size.w;
  switch (screen) {
    case 0: steps_day(ctx, w); break;
    case 1: sleep_night(ctx, w); break;
    case 2: heart_day(ctx, w); break;
    default: wo_recent(ctx, w); break;
  }
}

const char *mockups_name(int screen) {
  static const char *names[4] = {"steps-day", "sleep-night", "heart-day", "workouts-recent"};
  if (screen < 0 || screen >= 4) return "?";
  return names[screen];
}

#endif /* HG_MOCKUP */
