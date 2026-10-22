#include "mockups.h"

#if HG_MOCKUP

/* Drill-down mockups, Direction A "Timeline Native" — designed by gemini-4-argon,
 * implemented here at the real 200x228 / 64-colour budget.
 *
 *   concept   full-bleed card colour, black/white structure, LECO numerals
 *   type      Gothic 18 bold (titles/labels), Gothic 14 bold (units),
 *             LECO 32 bold (stats), LECO 20 bold (list numbers)
 *   spacing   8px well margins, 4px structural borders
 *   limit     one chart well + one bottom stat split, nothing else
 *
 * Signature: the 30-day average is a 2 px datum painted UNDER the bars, so it
 * reads as weaving behind the data instead of sitting on top of it. */

#include <pebble.h>
#include "stats.h"

#define M 8   /* well margin */
#define IN 10 /* text inset */

static void fill(GContext *ctx, GColor c, GRect r) {
  graphics_context_set_fill_color(ctx, c);
  graphics_fill_rect(ctx, r, 0, GCornerNone);
}

static void tx(GContext *ctx, const char *s, GFont f, GColor c, GRect r, GTextAlignment a) {
  if (!s || !s[0]) return;
  graphics_context_set_text_color(ctx, c);
  graphics_draw_text(ctx, s, f, r, GTextOverflowModeTrailingEllipsis, a, NULL);
}

static GFont g18(void) { return fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD); }
static GFont g14(void) { return fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD); }
static GFont leco32(void) { return fonts_get_system_font(FONT_KEY_LECO_32_BOLD_NUMBERS); }
static GFont leco20(void) { return fonts_get_system_font(FONT_KEY_LECO_20_BOLD_NUMBERS); }

/* Full-bleed canvas + black 32px cap with a context word on the right. */
static void canvas(GContext *ctx, GColor body, GColor capCol, GColor ctxCol, const char *title,
                   const char *context) {
  fill(ctx, body, GRect(0, 0, 200, 228));
  fill(ctx, capCol, GRect(0, 0, 200, 32));
  tx(ctx, title, g18(), GColorWhite, GRect(IN, 4, 110, 24), GTextAlignmentLeft);
  tx(ctx, context, g14(), ctxCol, GRect(90, 7, 102, 20), GTextAlignmentRight);
}

/* White chart well, 96px tall, closed by a 4px black base rule at y=134. */
static void well(GContext *ctx, GColor base) {
  fill(ctx, GColorWhite, GRect(M, 38, 184, 96));
  fill(ctx, base, GRect(M, 130, 184, 4));
}

/* Bottom split: black divider at x=99, LECO stat + 14px label either side. */
static void split_labels(GContext *ctx, const char *left, GColor leftCol, const char *right,
                         GColor rightCol) {
  fill(ctx, GColorBlack, GRect(99, 142, 2, 76));
  tx(ctx, left, g14(), leftCol, GRect(IN, 180, 84, 18), GTextAlignmentLeft);
  tx(ctx, right, g14(), rightCol, GRect(108, 180, 88, 18), GTextAlignmentLeft);
}

/* ---------------- steps ---------------- */

static void steps(GContext *ctx) {
  static const int hourly[24] = {0, 0, 0, 0, 0, 0, 80, 180, 320, 520, 780, 1240,
                                 960, 680, 860, 600, 420, 500, 720, 380, 260, 120, 40, 0};
  char a[24];
  canvas(ctx, GColorJaegerGreen, GColorBlack, GColorJaegerGreen, "STEPS", "TODAY");
  well(ctx, GColorBlack);
  fill(ctx, GColorBlack, GRect(M, 100, 184, 2)); /* the datum, under the bars */
  for (int i = 0; i < 24; i++) {
    int x = 16 + i * 7;
    if (hourly[i] <= 0) {
      fill(ctx, GColorLightGray, GRect(x, 128, 5, 2)); /* empty-hour tick */
      continue;
    }
    int bh = (hourly[i] * 68) / 1240;
    if (bh < 2) bh = 2;
    fill(ctx, hourly[i] >= 1240 ? GColorOrange : GColorDarkGreen,
         GRect(x, 130 - bh, 5, bh));
  }
  tx(ctx, "6A", g14(), GColorDarkGray, GRect(44, 40, 24, 14), GTextAlignmentLeft);
  tx(ctx, "6P", g14(), GColorDarkGray, GRect(128, 40, 24, 14), GTextAlignmentLeft);

  split_labels(ctx, "KM TOTAL", GColorBlack, "KCAL BURNED", GColorBlack);
  snprintf(a, sizeof a, "%d.%d", g_stats.dist_m / 1000, (g_stats.dist_m % 1000) / 100);
  tx(ctx, a, leco32(), GColorWhite, GRect(IN, 144, 84, 34), GTextAlignmentLeft);
  snprintf(a, sizeof a, "%d", g_stats.calories);
  tx(ctx, a, leco32(), GColorWhite, GRect(108, 144, 88, 34), GTextAlignmentLeft);
  snprintf(a, sizeof a, "%d%% GOAL", g_stats.steps / 100);
  tx(ctx, a, g14(), GColorWhite, GRect(IN, 198, 84, 18), GTextAlignmentLeft);
}

/* ---------------- sleep ---------------- */

static void sleep_screen(GContext *ctx) {
  static const uint8_t stage[24] = {3, 2, 1, 0, 0, 1, 2, 0, 1, 1, 0, 2,
                                    1, 0, 0, 1, 2, 1, 0, 0, 1, 2, 3, 3};
  const GColor cols[4] = {GColorIndigo, GColorCobaltBlue, GColorPictonBlue, GColorOrange};
  const int hts[4] = {20, 36, 52, 70};
  char a[24], t1[8], t2[8];
  canvas(ctx, GColorCobaltBlue, GColorDukeBlue, GColorPictonBlue, "SLEEP", "");
  stats_clock_to(t1, sizeof(t1), g_stats.sleep_start);
  stats_clock_to(t2, sizeof(t2), g_stats.sleep_end);
  snprintf(a, sizeof a, "%s-%s", t1, t2);
  tx(ctx, a, g14(), GColorPictonBlue, GRect(90, 7, 102, 20), GTextAlignmentRight);

  fill(ctx, GColorBlack, GRect(M, 38, 184, 96)); /* hypnogram well */
  fill(ctx, GColorBlack, GRect(M, 130, 184, 4));
  for (int i = 0; i < 24; i++) {
    int band = stage[i];
    fill(ctx, cols[band], GRect(16 + i * 7, 130 - hts[band], 6, hts[band]));
  }

  split_labels(ctx, "DEEP SLEEP", GColorWhite, "REM SLEEP", GColorPictonBlue);
  snprintf(a, sizeof a, "%d:%02d", g_stats.deep / 60, g_stats.deep % 60);
  tx(ctx, a, leco32(), GColorWhite, GRect(IN, 144, 84, 34), GTextAlignmentLeft);
  snprintf(a, sizeof a, "%d:%02d", g_stats.rem / 60, g_stats.rem % 60);
  tx(ctx, a, leco32(), GColorPictonBlue, GRect(108, 144, 88, 34), GTextAlignmentLeft);

  fill(ctx, GColorDukeBlue, GRect(M, 200, 184, 22));
  snprintf(a, sizeof a, "%d:%02d TOTAL ASLEEP", g_stats.sleep_min / 60,
           g_stats.sleep_min % 60);
  tx(ctx, a, g14(), GColorWhite, GRect(M, 202, 184, 18), GTextAlignmentCenter);
}

/* ---------------- heart ---------------- */

static void heart(GContext *ctx) {
  static const int hr[24] = {52, 50, 49, 50, 55, 61, 78, 96, 88, 72, 84, 110,
                             132, 96, 88, 92, 104, 118, 96, 84, 74, 66, 58, 54};
  char a[24];
  canvas(ctx, GColorImperialPurple, GColorBlack, GColorRajah, "HEART", "TODAY");
  well(ctx, GColorBlack);
  fill(ctx, GColorRajah, GRect(M, 108, 184, 2)); /* resting datum, under the bars */
  for (int i = 0; i < 24; i++) {
    int bh = (hr[i] * 68) / 132;
    if (bh < 2) bh = 2;
    fill(ctx, hr[i] >= 132 ? GColorRed : GColorImperialPurple,
         GRect(16 + i * 7, 130 - bh, 5, bh));
  }
  tx(ctx, "6A", g14(), GColorDarkGray, GRect(44, 40, 24, 14), GTextAlignmentLeft);
  tx(ctx, "6P", g14(), GColorDarkGray, GRect(128, 40, 24, 14), GTextAlignmentLeft);

  split_labels(ctx, "RESTING BPM", GColorRajah, "MIN / MAX", GColorRajah);
  snprintf(a, sizeof a, "%d", g_stats.resting_hr);
  tx(ctx, a, leco32(), GColorWhite, GRect(IN, 144, 84, 34), GTextAlignmentLeft);
  /* "52-148" needs 6 wide LECO digits: 32pt overflows 88px, so step down a size and
   * keep the baseline aligned with the resting value on the left. */
  snprintf(a, sizeof a, "%d-%d", g_stats.hr_min, g_stats.hr_max);
  tx(ctx, a, leco20(), GColorWhite, GRect(108, 158, 88, 22), GTextAlignmentLeft);
}

/* ---------------- workouts ---------------- */

static void workout_row(GContext *ctx, int y, bool current, GColor accent, const char *name,
                        const char *dur, const char *meta, const char *when) {
  fill(ctx, current ? GColorWhite : GColorDarkGray, GRect(M, y, 184, 56));
  fill(ctx, accent, GRect(M, y, 6, 56));
  GColor ink = current ? GColorBlack : GColorWhite;
  tx(ctx, name, g18(), ink, GRect(20, y + 4, 90, 20), GTextAlignmentLeft);
  tx(ctx, dur, leco20(), ink, GRect(100, y + 6, 86, 22), GTextAlignmentRight);
  tx(ctx, meta, g14(), current ? GColorDarkGray : GColorLightGray, GRect(20, y + 32, 110, 18),
     GTextAlignmentLeft);
  tx(ctx, when, g14(), accent, GRect(130, y + 32, 56, 18), GTextAlignmentRight);
}

static void workouts(GContext *ctx) {
  canvas(ctx, GColorBlack, GColorOrange, GColorBlack, "WORKOUTS", "3 RECENT");
  workout_row(ctx, 38, true, GColorOrange, "WALK", "31:15", "4.3 KM · 137 AVG", "TODAY");
  workout_row(ctx, 100, false, GColorRajah, "RUN", "42:18", "6.1 KM · 148 AVG", "YEST");
  workout_row(ctx, 162, false, GColorWindsorTan, "RIDE", "1:02", "18 KM · 121 AVG", "TUE");
}

/* ---------------- public ---------------- */

int mockups_count(void) { return 4; }

void mockups_draw(GContext *ctx, GRect bounds, int screen) {
  (void)bounds;
  switch (screen) {
    case 0: steps(ctx); break;
    case 1: sleep_screen(ctx); break;
    case 2: heart(ctx); break;
    default: workouts(ctx); break;
  }
}

const char *mockups_name(int screen) {
  static const char *names[4] = {"steps-timeline", "sleep-timeline", "heart-timeline",
                                 "workouts-timeline"};
  if (screen < 0 || screen >= 4) return "?";
  return names[screen];
}

#endif /* HG_MOCKUP */
