#include "cards.h"

#define CAP_H 36
#define X_INSET 10
#define HERO_Y 44
#define UNIT_Y 106
#define BAR_Y 136
#define BAR_H 8
#define META_Y 178
#define CUE_Y 202
#define STEP_GOAL 10000

/* 0..1000 reveal progress applied to bars, stage segments, the HR marker and
 * the active pip. At rest it is 1000, so the settled card is pixel-identical. */
static int s_reveal_pm = 1000;
static Animation *s_reveal;
static Layer *s_reveal_target;

static GFont s_f_title;
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

static int scale(int value, int pm) { return (int)(((int64_t)value * pm) / 1000); }

static void draw_pips(GContext *ctx, int w, int card) {
  int x = w - 10 - 34;
  for (int i = 0; i < 4; i++) {
    /* the active pip blooms 5px -> 7px with the reveal */
    int s = (i == card) ? 5 + scale(2, s_reveal_pm) : 5;
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
    int shown = scale(pw, s_reveal_pm);
    if (shown > 0) fill(ctx, c[i], GRect(x, y, shown, BAR_H));
    x += shown;
  }
}


/* The original hero: 13 strokes (x,y,w,h +32), then 12 fill masks and 12 hole
 * masks, 7 bits per byte — the packed-glyph table from the Alloy app, ported
 * verbatim so the big blocky digits are identical. Digits advance 36 px. */
static const uint8_t GLYPH[100] = {
     34,  34,  62,  42,  34,  54,  62,  42,  34,  74,  62,  42,
     34,  34,  42,  62,  34,  54,  42,  62,  54,  34,  42,  62,
     54,  54,  42,  62,  54,  34,  42,  82,  44,  34,  42,  82,
     34,  34,  62,  82,  44,  44,  42,  62,  44,  44,  42,  42,
     44,  64,  42,  42,  32,  36,  32,  34,  87,  32,  39,  33,
     42,  33, 111,  32, 111,  32,  33,  33,  32,  36,  47,  33,
     32,  80,  34,  32,  32,  40,  32,  32,  32,  32,  32,  32,
     32,  32,  32,  32,  32,  64,  32,  32,  32,  80,  32,  48,
     32,  32,  32,  32,
};

static void draw_hero(GContext *ctx, const char *text) {
  int x = X_INSET;
  for (int i = 0; text[i]; i++) {
    int ch = (unsigned char)text[i];
    int g = -1;
    if (ch >= '0' && ch <= '9') {
      g = ch - '0';
    } else if (ch == ':') {
      g = 10;
    } else if (ch == '-') {
      g = 11;
    }
    if (g >= 0) {
      int fill_mask = (GLYPH[52 + g * 2] - 32) | ((GLYPH[53 + g * 2] - 32) << 7);
      int hole_mask = (GLYPH[76 + g * 2] - 32) | ((GLYPH[77 + g * 2] - 32) << 7);
      for (int b = 0; b < 13; b++) {
        int bit = 1 << b;
        if ((fill_mask | hole_mask) & bit) {
          int p = b * 4;
          fill(ctx, (hole_mask & bit) ? GColorWhite : GColorBlack,
               GRect(x + GLYPH[p] - 32, HERO_Y + GLYPH[p + 1] - 32,
                     GLYPH[p + 2] - 32, GLYPH[p + 3] - 32));
        }
      }
    }
    x += 36;
  }
}

void cards_draw(GContext *ctx, GRect bounds, int card) {
  const int w = bounds.size.w, h = bounds.size.h;
  const int bw = w - 2 * X_INSET;
  char hero[24], meta[64], tmp1[16], tmp2[16];

  fill(ctx, GColorWhite, GRect(0, 0, w, h));
  fill(ctx, CAP[card], GRect(0, 0, w, CAP_H));
  /* y=1 puts the cap title ink at 11..24, matching the Alloy cards exactly. */
  text(ctx, card_title(card), s_f_title, GColorWhite, X_INSET, 1, bw - 40, 32);
  draw_pips(ctx, w, card);

  hero_str(hero, sizeof(hero), card);
  draw_hero(ctx, hero);

  text(ctx, stats_footer(), s_f_body, GColorDarkGray, X_INSET, CUE_Y, bw, 22);

  if (card == CARD_STEPS) {
    text(ctx, "steps today", s_f_body, GColorDarkGray, X_INSET, UNIT_Y, bw, 22);
    fill(ctx, GColorLightGray, GRect(X_INSET, BAR_Y, bw, BAR_H));
    int pct = g_stats.steps > 0 ? (g_stats.steps * bw) / STEP_GOAL : 0;
    if (pct > bw) pct = bw;
    pct = scale(pct, s_reveal_pm);
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
      /* slides out from the left edge as the card settles */
      dot = X_INSET + scale(dot - X_INSET, s_reveal_pm);
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
      int full = scale(bw, s_reveal_pm);
      if (full > 0) fill(ctx, GColorOrange, GRect(X_INSET, BAR_Y, full, BAR_H));
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

/* ---------------- reveal animation ---------------- */

static void reveal_update(Animation *animation, const AnimationProgress progress) {
  (void)animation;
  s_reveal_pm = (int)(((int64_t)progress * 1000) / ANIMATION_NORMALIZED_MAX);
  if (s_reveal_target) layer_mark_dirty(s_reveal_target);
}

static void reveal_stopped(Animation *animation, bool finished, void *context) {
  (void)animation;
  (void)finished;
  (void)context;
  s_reveal = NULL;
  s_reveal_pm = 1000;
  if (s_reveal_target) layer_mark_dirty(s_reveal_target);
}

void cards_reveal(Layer *target) {
  cards_cancel_reveal();
  s_reveal_target = target;
  s_reveal_pm = 0;
  static const AnimationImplementation impl = {.update = reveal_update};
  s_reveal = animation_create();
  if (!s_reveal) {
    s_reveal_pm = 1000;
    return;
  }
  animation_set_implementation(s_reveal, &impl);
  animation_set_duration(s_reveal, 260); /* outlives the 175ms push, so it lands filled */
  animation_set_curve(s_reveal, AnimationCurveEaseOut);
  animation_set_handlers(s_reveal, (AnimationHandlers){.stopped = reveal_stopped}, NULL);
  animation_schedule(s_reveal);
}

void cards_cancel_reveal(void) {
  if (s_reveal) {
    animation_unschedule(s_reveal);
    s_reveal = NULL;
  }
  s_reveal_target = NULL;
  s_reveal_pm = 1000;
}
