#include "cards.h"

#define CAP_H 36
#define X_INSET 10
#define HERO_Y 28
#define UNIT_Y 106
#define BAR_Y 136
#define BAR_H 8
#define META_Y 178
#define CUE_Y 202
#define STEP_GOAL 10000

static GFont s_f_title;
static GFont s_f_hero;
static GFont s_f_body;

/* 64-colour palette picks closest to the RGB values the Alloy app used. */
static const GColor CAP[4] = {GColorJaegerGreen, GColorCobaltBlue, GColorImperialPurple,
                             GColorOrange};
static const GColor CAP_DIM[4] = {GColorCadetBlue, GColorPictonBlue, GColorPurpureus,
                                  GColorRajah};
static const GColor STAGE[4] = {GColorDukeBlue, GColorCobaltBlue, GColorIndigo,
                                GColorWindsorTan};
static const GColor ZONE[4] = {GColorRajah, GColorOrange, GColorDarkCandyAppleRed,
                               GColorOrange};

void cards_init(void) {
  s_f_title = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  s_f_hero = fonts_get_system_font(FONT_KEY_LECO_60_BOLD_NUMBERS_AM_PM);
  s_f_body = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
}

static void fill(GContext *ctx, GColor c, GRect r) {
  graphics_context_set_fill_color(ctx, c);
  graphics_fill_rect(ctx, r, 0, GCornerNone);
}

static void text(GContext *ctx, const char *s, GFont f, GColor c, int x, int y, int w, int h) {
  if (!s || !s[0]) return;
  graphics_context_set_text_color(ctx, c);
  graphics_draw_text(ctx, s, f, GRect(x, y, w, h), GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentLeft, NULL);
}

/* ---------------- value formatters ---------------- */

static void f_sleep_hero(char *b, size_t n) {
  snprintf(b, n, "%d:%02d", g_stats.sleep_min / 60, g_stats.sleep_min % 60);
}

static void f_wo_time(char *b, size_t n) {
  if (g_stats.wo_dur <= 0) {
    snprintf(b, n, "-");
    return;
  }
  snprintf(b, n, "%02d:%02d", g_stats.wo_dur / 60, g_stats.wo_dur % 60);
}

static void f_tenths(char *b, size_t n, int hundredths) {
  snprintf(b, n, "%d.%d", hundredths / 10, hundredths % 10);
}

static void f_dash(char *b, size_t n, int v) {
  if (v > 0) {
    snprintf(b, n, "%d", v);
  } else {
    snprintf(b, n, "-");
  }
}

static const char *card_title(int card) {
  switch (card) {
    case CARD_STEPS: return "STEPS";
    case CARD_SLEEP: return "SLEEP";
    case CARD_HEART: return "HEART";
    default:
      if (g_stats.has_workout && g_stats.wo_title[0]) return g_stats.wo_title;
      return "WORKOUT";
  }
}

static void hero_str(char *b, size_t n, int card) {
  switch (card) {
    case CARD_STEPS: snprintf(b, n, "%d", g_stats.steps); return;
    case CARD_SLEEP: f_sleep_hero(b, n); return;
    case CARD_HEART: f_dash(b, n, g_stats.resting_hr); return;
    default: f_wo_time(b, n); return;
  }
}

/* ---------------- chrome ---------------- */

static void draw_pips(GContext *ctx, int w, int card) {
  int x = w - 10 - 34;
  for (int i = 0; i < 4; i++) {
    int s = (i == card) ? 7 : 5;
    fill(ctx, (i == card) ? GColorWhite : CAP_DIM[card], GRect(x, 18 - s, s, s));
    x += s + 4;
  }
}

static void draw_parts(GContext *ctx, int y, int w, const int v[4], const GColor c[4]) {
  int bw = w - 2 * X_INSET;
  int total = 0;
  for (int i = 0; i < 4; i++) total += v[i];
  if (total <= 0) {
    fill(ctx, GColorLightGray, GRect(X_INSET, y, bw, BAR_H));
    return;
  }
  int x = X_INSET;
  for (int i = 0; i < 4; i++) {
    if (v[i] <= 0) continue;
    int pw = (v[i] * bw) / total;
    if (pw < 2) pw = 2;
    fill(ctx, c[i], GRect(x, y, pw, BAR_H));
    x += pw;
  }
}

void cards_draw(GContext *ctx, GRect bounds, int card) {
  const int w = bounds.size.w, h = bounds.size.h;
  const int bw = w - 2 * X_INSET;
  char hero[24], meta[64], tmp1[16], tmp2[16];

  fill(ctx, GColorWhite, GRect(0, 0, w, h));
  fill(ctx, CAP[card], GRect(0, 0, w, CAP_H));
  text(ctx, card_title(card), s_f_title, GColorWhite, X_INSET, 3, bw - 40, 30);
  draw_pips(ctx, w, card);

  hero_str(hero, sizeof(hero), card);
  text(ctx, hero, s_f_hero, GColorBlack, X_INSET, HERO_Y, bw, 64);

  text(ctx, stats_footer(), s_f_body, GColorDarkGray, X_INSET, CUE_Y, bw, 22);

  if (card == CARD_STEPS) {
    text(ctx, "steps today", s_f_body, GColorDarkGray, X_INSET, UNIT_Y, bw, 22);
    fill(ctx, GColorLightGray, GRect(X_INSET, BAR_Y, bw, BAR_H));
    int pct = g_stats.steps > 0 ? (g_stats.steps * bw) / STEP_GOAL : 0;
    if (pct > bw) pct = bw;
    if (pct > 0) fill(ctx, GColorJaegerGreen, GRect(X_INSET, BAR_Y, pct, BAR_H));
    if (g_stats.dist_m > 0) {
      f_tenths(tmp1, sizeof(tmp1), g_stats.dist_m / 100);
      snprintf(meta, sizeof(meta), "%s km \xc2\xb7 %d cal", tmp1, g_stats.calories);
    } else {
      snprintf(meta, sizeof(meta), "-");
    }
    text(ctx, meta, s_f_body, GColorBlack, X_INSET, META_Y, bw, 22);
  } else if (card == CARD_SLEEP) {
    int v[4] = {g_stats.deep, g_stats.light, g_stats.rem, g_stats.awake};
    text(ctx, "asleep", s_f_body, GColorDarkGray, X_INSET, UNIT_Y, bw, 22);
    draw_parts(ctx, BAR_Y, w, v, STAGE);
    char t1[8], t2[8];
    stats_clock_to(t1, sizeof(t1), g_stats.sleep_start);
    stats_clock_to(t2, sizeof(t2), g_stats.sleep_end);
    snprintf(meta, sizeof(meta), "%s -> %s", t1, t2);
    text(ctx, meta, s_f_body, GColorBlack, X_INSET, META_Y, bw, 22);
  } else if (card == CARD_HEART) {
    text(ctx, "resting bpm", s_f_body, GColorDarkGray, X_INSET, UNIT_Y, bw, 22);
    fill(ctx, GColorLightGray, GRect(X_INSET, BAR_Y, bw, BAR_H));
    if (g_stats.hr_max > g_stats.hr_min && g_stats.resting_hr > 0) {
      int dot = X_INSET + ((g_stats.resting_hr - g_stats.hr_min) * bw) /
                               (g_stats.hr_max - g_stats.hr_min);
      if (dot < X_INSET) dot = X_INSET;
      if (dot > X_INSET + bw) dot = X_INSET + bw;
      fill(ctx, GColorImperialPurple, GRect(dot - 7, BAR_Y - 3, 14, 14));
    }
    f_dash(tmp1, sizeof(tmp1), g_stats.hr_min);
    f_dash(tmp2, sizeof(tmp2), g_stats.hr_max);
    snprintf(meta, sizeof(meta), "min %s \xc2\xb7 max %s", tmp1, tmp2);
    text(ctx, meta, s_f_body, GColorBlack, X_INSET, META_Y, bw, 22);
  } else if (g_stats.has_workout && g_stats.wo_dur > 0) {
    text(ctx, "duration", s_f_body, GColorDarkGray, X_INSET, UNIT_Y, bw, 22);
    if (g_stats.wo_lo + g_stats.wo_mid + g_stats.wo_hi > 0) {
      int v[4] = {g_stats.wo_lo, g_stats.wo_mid, g_stats.wo_hi, 0};
      draw_parts(ctx, BAR_Y, w, v, ZONE);
    } else {
      fill(ctx, GColorOrange, GRect(X_INSET, BAR_Y, bw, BAR_H));
    }
    f_tenths(tmp1, sizeof(tmp1), g_stats.wo_dist / 100);
    f_dash(tmp2, sizeof(tmp2), g_stats.wo_avg);
    char tmp3[16];
    f_dash(tmp3, sizeof(tmp3), g_stats.wo_cal);
    if (g_stats.wo_dist > 0) {
      snprintf(meta, sizeof(meta), "%s km \xc2\xb7 %s avg \xc2\xb7 %s cal", tmp1, tmp2, tmp3);
    } else {
      snprintf(meta, sizeof(meta), "- \xc2\xb7 %s avg \xc2\xb7 %s cal", tmp2, tmp3);
    }
    text(ctx, meta, s_f_body, GColorBlack, X_INSET, META_Y, bw, 22);
  } else {
    text(ctx, "today", s_f_body, GColorDarkGray, X_INSET, UNIT_Y, bw, 22);
    fill(ctx, GColorLightGray, GRect(X_INSET, BAR_Y, bw, BAR_H));
    text(ctx, "No workout", s_f_body, GColorBlack, X_INSET, META_Y, bw, 22);
  }
}
