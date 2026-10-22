#include "mockups.h"

#if HG_MOCKUP

/* Drill-down screens that keep the deck's grammar instead of inventing a new one.
 *
 * Every element sits on an existing card coordinate:
 *
 *   y   0..36   cap          36px in the card colour, title 24 bold white
 *   y  44..94   hero         the SAME blocky glyph digits as the cards (cards_hero_text)
 *   y 106       unit label   18 bold DarkGray, exactly like "steps today"
 *   y  96..136  chart        grows upward out of the card's bar slot
 *   y 136..144  rail         the card's own 8px LightGray progress bar, undivided
 *   y 178       meta         18 bold Black
 *   y 202       footer       18 bold DarkGray, the same synced/cached cue
 *
 * A detail screen is therefore a card whose progress bar has expanded into a chart
 * and whose hero has become the secondary metric. Nothing new is introduced.
 *
 * The brief's Direction A (full-bleed colour, LECO numerals) was rejected as out
 * of keeping: it is a different app's voice. */

#include <pebble.h>
#include "cards.h"
#include "stats.h"

/* HG_MARK picks how a detail screen announces itself (QA comparison builds):
 *   0 none            1 "A" page rule under the cap
 *   2 "B+C" chevron   3 chevron + rule                                    */
#ifndef HG_MARK
#define HG_MARK 0
#endif

#define X_INSET 10
#define CAP_H 36
#define HERO_Y 44
#define UNIT_Y 98
#define CHART_TOP 120
#define BAR_Y 170 /* the card's 8px rail, moved below the chart: 170..178 */
#define BAR_H 8
#define META_Y 178 /* unchanged: rail ends exactly where the meta line starts */
#define CUE_Y 202
#define BW 180 /* 200 - 2*X_INSET */

static const GColor CAP_COL[4] = {GColorJaegerGreen, GColorCobaltBlue, GColorImperialPurple,
                                  GColorOrange};
/* stage colours, identical to the sleep card's 8px stage bar */
static const GColor STAGE_COL[4] = {GColorDukeBlue, GColorCobaltBlue, GColorIndigo,
                                    GColorWindsorTan};

static void fill(GContext *ctx, GColor c, GRect r) {
  graphics_context_set_fill_color(ctx, c);
  graphics_fill_rect(ctx, r, 0, GCornerNone);
}

static void label(GContext *ctx, const char *s, GColor c, int y, GTextAlignment a) {
  if (!s || !s[0]) return;
  graphics_context_set_text_color(ctx, c);
  graphics_draw_text(ctx, s, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     GRect(X_INSET, y, BW, 22), GTextOverflowModeTrailingEllipsis, a, NULL);
}

/* Right-hand label owns its own 92px column so it can never collide with the left
 * label, which is what happened when both shared the full 180px box. */
static void label_right(GContext *ctx, const char *s, GColor c, int y) {
  if (!s || !s[0]) return;
  graphics_context_set_text_color(ctx, c);
  graphics_draw_text(ctx, s, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     GRect(X_INSET + 98, y, BW - 98, 22), GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentRight, NULL);
}

/* Cap + white body + the card's bar rail, which is always drawn so the chart reads
 * as the progress bar grown upward rather than as a new component. */
static void frame(GContext *ctx, int screen) {
  static const char *titles[4] = {"STEPS", "SLEEP", "HEART", "WORKOUTS"};
  fill(ctx, GColorWhite, GRect(0, 0, 200, 228));
  fill(ctx, CAP_COL[screen], GRect(0, 0, 200, CAP_H));
#if HG_MARK == 1 || HG_MARK == 3
  fill(ctx, GColorBlack, GRect(0, CAP_H, 200, 2)); /* A: page rule the cards never have */
#endif
  graphics_context_set_text_color(ctx, GColorWhite);
#if HG_MARK == 2 || HG_MARK == 3
  /* B: chevron + indented title; the pips are the deck's paging cue and a detail
   * screen isn't paging, so nothing sits on the right of the title any more. */
  graphics_draw_text(ctx, "<", fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     GRect(2, 6, 12, 20), GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentLeft, NULL);
  graphics_draw_text(ctx, titles[screen], fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(14, 1, BW - 60, 32), GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentLeft, NULL);
#else
  graphics_draw_text(ctx, titles[screen], fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(X_INSET, 1, BW - 60, 32), GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentLeft, NULL);
#endif
  fill(ctx, GColorLightGray, GRect(X_INSET, BAR_Y, BW, BAR_H));
#if HG_MARK == 2 || HG_MARK == 3
  /* C: slot dividers turn the card's progress bar into a chart scale */
  for (int x = X_INSET + 8; x < X_INSET + BW; x += 8) {
    fill(ctx, GColorWhite, GRect(x, BAR_Y, 1, BAR_H));
  }
#endif
}

static void context(GContext *ctx, const char *s) {
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, s, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                     GRect(200 - X_INSET - 100, 9, 100, 18), GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentRight, NULL);
}

static void footer(GContext *ctx) {
  label(ctx, "synced 09:41", GColorDarkGray, CUE_Y, GTextAlignmentLeft);
}

/* One bar, grown up from the rail. */
static void bar(GContext *ctx, int x, int w, int h, GColor c) {
  if (h < 2) h = 2;
  fill(ctx, c, GRect(x, BAR_Y - h, w, h));
}

/* ---------------- steps ---------------- */

static void steps(GContext *ctx) {
  static const int hourly[24] = {0, 0, 0, 0, 0, 0, 80, 180, 320, 520, 780, 1240,
                                 960, 680, 860, 600, 420, 500, 720, 380, 260, 120, 40, 0};
  const int peak = 1240, peak_hour = 11;
  char a[32];
  frame(ctx, 0);
  context(ctx, "today");
  snprintf(a, sizeof a, "%d", peak);
  cards_hero_text(ctx, a, HERO_Y);
  label(ctx, "peak hour", GColorDarkGray, UNIT_Y, GTextAlignmentLeft);
  fill(ctx, GColorDarkGray, GRect(X_INSET, BAR_Y - 22, BW, 1)); /* 30-day datum */
  for (int i = 0; i < 24; i++) {
    int h = (hourly[i] * (BAR_Y - 2 - CHART_TOP)) / peak;
    bar(ctx, X_INSET + i * 8, 5, h, i == peak_hour ? GColorBlack : GColorJaegerGreen);
  }
  snprintf(a, sizeof a, "%d km · %d cal", g_stats.dist_m / 1000, g_stats.calories);
  label(ctx, a, GColorBlack, META_Y, GTextAlignmentLeft);
  label_right(ctx, "6am-11pm", GColorDarkGray, UNIT_Y);
  footer(ctx);
}

/* ---------------- sleep ---------------- */

static void sleep_screen(GContext *ctx) {
  static const uint8_t stage[24] = {3, 2, 1, 0, 0, 1, 2, 0, 1, 1, 0, 2,
                                    1, 0, 0, 1, 2, 1, 0, 0, 1, 2, 3, 3};
  const int hts[4] = {18, 30, 40, 48}; /* awake / light / rem / deep, up from the rail */
  char a[32], t1[8], t2[8];
  frame(ctx, 1);
  context(ctx, "last night");
  snprintf(a, sizeof a, "%d", g_stats.deep);
  cards_hero_text(ctx, a, HERO_Y);
  label(ctx, "deep sleep", GColorDarkGray, UNIT_Y, GTextAlignmentLeft);
  for (int i = 0; i < 24; i++) {
    int band = stage[i];
    bar(ctx, X_INSET + i * 8, 6, hts[band], STAGE_COL[band]);
  }
  stats_clock_to(t1, sizeof(t1), g_stats.sleep_start);
  stats_clock_to(t2, sizeof(t2), g_stats.sleep_end);
  snprintf(a, sizeof a, "%s-%s", t1, t2);
  label_right(ctx, a, GColorDarkGray, UNIT_Y);
  snprintf(a, sizeof a, "%d:%02d rem · %d:%02d awake", g_stats.rem / 60, g_stats.rem % 60,
           (g_stats.sleep_min / 6) / 60, (g_stats.sleep_min / 6) % 60);
  label(ctx, a, GColorBlack, META_Y, GTextAlignmentLeft);
  footer(ctx);
}

/* ---------------- heart ---------------- */

static void heart(GContext *ctx) {
  static const int hr[24] = {52, 50, 49, 50, 55, 61, 78, 96, 88, 72, 84, 110,
                             132, 96, 88, 92, 104, 118, 96, 84, 74, 66, 58, 54};
  const int peak = 132, peak_hour = 12;
  char a[32];
  frame(ctx, 2);
  context(ctx, "today");
  snprintf(a, sizeof a, "%d", peak);
  cards_hero_text(ctx, a, HERO_Y);
  label(ctx, "peak bpm", GColorDarkGray, UNIT_Y, GTextAlignmentLeft);
  fill(ctx, GColorDarkGray, GRect(X_INSET, BAR_Y - 26, BW, 1)); /* resting datum */
  for (int i = 0; i < 24; i++) {
    int h = ((hr[i] - 44) * (BAR_Y - 2 - CHART_TOP)) / (peak - 44);
    bar(ctx, X_INSET + i * 8, 5, h, i == peak_hour ? GColorBlack : GColorImperialPurple);
  }
  snprintf(a, sizeof a, "%d resting", g_stats.resting_hr);
  label_right(ctx, a, GColorDarkGray, UNIT_Y);
  snprintf(a, sizeof a, "range %d-%d", g_stats.hr_min, g_stats.hr_max);
  label(ctx, a, GColorBlack, META_Y, GTextAlignmentLeft);
  footer(ctx);
}

/* ---------------- workouts ---------------- */

static void workouts(GContext *ctx) {
  static const int mins[7] = {0, 42, 31, 62, 0, 31, 0}; /* Mon..Sun */
  char a[32];
  frame(ctx, 3);
  context(ctx, "this week");
  snprintf(a, sizeof a, "%d", 3);
  cards_hero_text(ctx, a, HERO_Y);
  label(ctx, "sessions", GColorDarkGray, UNIT_Y, GTextAlignmentLeft);
  for (int i = 0; i < 7; i++) {
    int x = X_INSET + i * 26;
    if (mins[i] <= 0) {
      fill(ctx, GColorLightGray, GRect(x, BAR_Y - 2, 16, 2)); /* rest day */
      continue;
    }
    bar(ctx, x, 16, (mins[i] * (BAR_Y - 2 - CHART_TOP)) / 62,
        i == 5 ? GColorBlack : GColorOrange);
  }
  label(ctx, "31:15 walk today", GColorBlack, META_Y, GTextAlignmentLeft);
  footer(ctx);
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
  static const char *names[4] = {"steps-card", "sleep-card", "heart-card", "workouts-card"};
  if (screen < 0 || screen >= 4) return "?";
  return names[screen];
}

#endif /* HG_MOCKUP */
