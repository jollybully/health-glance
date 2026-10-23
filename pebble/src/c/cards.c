#include "cards.h"

#define BAR_H 8
#define STEP_GOAL_DEFAULT 10000

/* One geometry table, two shapes. Emery is 200x228 and rectangular, so its numbers
 * are the ones this app was pixel-fitted against and must not move by a pixel.
 * Gabbro (Round 2) is 260x260 *round*: a rectangle of content loses its corners to
 * the bezel, so the card is laid out inside the inscribed square instead. 154/1000
 * of the short edge is 40px on 260, giving a 180px column - just inside the
 * geometric 184px square, which is the margin that stops the cap band and the
 * footer from being shaved by the glass. */
typedef struct {
  int cap_x, cap_y, cap_w;
  int ink_x;
  int cap_h, title_y, pips_x, pips_y;
  int hero_y, unit_y, bar_y, meta_y, cue_y;
} CardLayout;

static CardLayout s_l;

static void layout_for(GRect b) {
  s_l.ink_x = 10;
  s_l.cap_x = 0;
  s_l.cap_w = b.size.w;
  s_l.cap_y = 0;
  s_l.cap_h = 36;
  s_l.title_y = 1;
  s_l.pips_x = b.size.w - 10 - 34;
  s_l.pips_y = 18;
  s_l.hero_y = 44;
  s_l.unit_y = 106;
  s_l.bar_y = 136;
  s_l.meta_y = 178;
  s_l.cue_y = 202;
#if PBL_ROUND
  const int short_edge = b.size.w < b.size.h ? b.size.w : b.size.h;
  const int inset = (short_edge * 154) / 1000;
  const int col = short_edge - 2 * inset;      /* 180 on 260 */
  const int top = (b.size.h - col) / 2;        /* 40 on 260 */
  s_l.ink_x = inset;
  s_l.cap_x = inset;
  s_l.cap_w = col;
  s_l.cap_y = top;
  s_l.cap_w = col;
  s_l.cap_h = 28;
  s_l.title_y = top + 1;
  s_l.pips_x = inset + col - 8 - 34;
  s_l.pips_y = top + 14;
  s_l.hero_y = top + 30;
  s_l.unit_y = top + 84;
  s_l.bar_y = top + 112;
  s_l.meta_y = top + 128;
  s_l.cue_y = top + 156;
#endif
}

#define CAP_H (s_l.cap_h)
#define X_INSET (s_l.ink_x)
#define HERO_Y (s_l.hero_y)
#define UNIT_Y (s_l.unit_y)
#define BAR_Y (s_l.bar_y)
#define META_Y (s_l.meta_y)
#define CUE_Y (s_l.cue_y)

/* The numbers warp, nothing else moves. s_hero_pm scales the hero glyph block
 * vertically about its own centre (1000 = identity, so a settled card is
 * pixel-identical); the cap, labels and bars stay put, which is what keeps it
 * feeling like PebbleOS rather than jelly. */
#define HERO_BLOCK_H 50
static int s_hero_pm = 1000;
static Animation *s_warp;
static Layer *s_warp_target;

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
    case CARD_HEART:
      /* Live when we have it: this is the number he actually wants on the main
       * screen. The label and cap say which number it is, so it cannot be
       * mistaken for the resting rate. */
      if (stats_live_fresh() && g_live.bpm > 0) {
        f_dash(b, n, g_live.bpm);
      } else {
        f_dash(b, n, g_stats.resting_hr);
      }
      return;
    default: f_wo_time(b, n); return;
  }
}

/* ---------------- chrome ---------------- */

static void draw_pips(GContext *ctx, int w, int card) {
  (void)w;
  int x = s_l.pips_x;
  for (int i = 0; i < 4; i++) {
    int s = (i == card) ? 7 : 5;
    fill(ctx, (i == card) ? GColorWhite : CAP_DIM[card], GRect(x, s_l.pips_y - s, s, s));
    x += s + 4;
  }
}

static void draw_parts(GContext *ctx, int y, int w, const int v[4], const GColor c[4]) {
  int bw = w - 2 * X_INSET;
  int total = 0;
  for (int i = 0; i < 4; i++) total += v[i];
  const int y0 = y, bh = BAR_H;
  if (total <= 0) {
    fill(ctx, GColorLightGray, GRect(X_INSET, y0, bw, bh));
    return;
  }
  int x = X_INSET;
  for (int i = 0; i < 4; i++) {
    if (v[i] <= 0) continue;
    int pw = (v[i] * bw) / total;
    if (pw < 2) pw = 2;
    fill(ctx, c[i], GRect(x, y0, pw, bh));
    x += pw;
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

static void draw_hero_at(GContext *ctx, const char *text, int y0) {
  int x = X_INSET;
  const int cy = y0 + HERO_BLOCK_H / 2; /* warp pivot: the middle of the digits */
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
          int top = y0 + GLYPH[p + 1] - 32;
          int warped_top = cy + ((top - cy) * s_hero_pm) / 1000;
          int warped_h = ((GLYPH[p + 3] - 32) * s_hero_pm) / 1000;
          if (warped_h < 1) warped_h = 1;
          fill(ctx, (hole_mask & bit) ? GColorWhite : GColorBlack,
               GRect(x + GLYPH[p] - 32, warped_top, GLYPH[p + 2] - 32, warped_h));
        }
      }
    }
    x += 36;
  }
}

void cards_draw(GContext *ctx, GRect bounds, int card) {
  const int w = bounds.size.w, h = bounds.size.h;
  layout_for(bounds);
  const int bw = w - 2 * X_INSET;
  char hero[24], meta[128], tmp1[16], tmp2[16];

  fill(ctx, GColorWhite, GRect(0, 0, w, h));
  fill(ctx, CAP[card], GRect(s_l.cap_x, s_l.cap_y, s_l.cap_w, CAP_H));
  /* y=1 puts the cap title ink at 11..24, matching the Alloy cards exactly; on a
   * round screen the same +1 is applied to the cap the layout actually drew. */
  text(ctx, card_title(card), s_f_title, GColorWhite, X_INSET, s_l.title_y, bw - 40, 32);
  draw_pips(ctx, w, card);

  hero_str(hero, sizeof(hero), card);
  draw_hero_at(ctx, hero, HERO_Y);

  text(ctx, stats_footer(), s_f_body, GColorDarkGray, X_INSET, CUE_Y, bw, 22);

  if (card == CARD_STEPS) {
    text(ctx, "steps today", s_f_body, GColorDarkGray, X_INSET, UNIT_Y, bw, 22);
    fill(ctx, GColorLightGray, GRect(X_INSET, BAR_Y, bw, BAR_H));
    const int goal = g_stats.step_goal > 0 ? g_stats.step_goal : STEP_GOAL_DEFAULT;
    int pct = g_stats.steps > 0 ? (g_stats.steps * bw) / goal : 0;
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
    /* While the strap streams, the card says so: a filled pip in the cap plus the
     * live rate in the meta line. The hero stays resting bpm so the card keeps
     * meaning what it always meant. */
    /* The hero stays resting bpm, so the label must say so: calling a resting
     * number "live bpm" was contradictory. The live rate belongs in the meta line,
     * where it cannot be mistaken for the hero's meaning. */
    const bool live = stats_live_fresh() && g_live.bpm > 0;
    text(ctx, live ? "live bpm" : "resting bpm", s_f_body, GColorDarkGray, X_INSET, UNIT_Y, bw,
         22);
    fill(ctx, GColorLightGray, GRect(X_INSET, BAR_Y, bw, BAR_H));
    if (g_stats.hr_max > g_stats.hr_min && g_stats.resting_hr > 0) {
      int dot = X_INSET + ((g_stats.resting_hr - g_stats.hr_min) * bw) /
                               (g_stats.hr_max - g_stats.hr_min);
      if (dot < X_INSET) dot = X_INSET;
      if (dot > X_INSET + bw) dot = X_INSET + bw;
      fill(ctx, GColorImperialPurple, GRect(dot - 7, BAR_Y - 3, 14, 14));
    }
    if (live) {
      f_dash(tmp1, sizeof(tmp1), g_stats.hr_min);
      f_dash(tmp2, sizeof(tmp2), g_stats.hr_max);
      snprintf(meta, sizeof(meta), "min %s \xc2\xb7 max %s", tmp1, tmp2);
    } else {
      f_dash(tmp1, sizeof(tmp1), g_stats.hr_min);
      f_dash(tmp2, sizeof(tmp2), g_stats.hr_max);
      snprintf(meta, sizeof(meta), "min %s \xc2\xb7 max %s", tmp1, tmp2);
    }
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

/* ---------------- warp animation ---------------- */

/* No bounce: the digits simply ease out of a slight vertical stretch into their
 * resting shape, the way PebbleOS settles a readout. */
#define HERO_PM_START 1290 /* 1.29x tall on the way in */
#define WARP_MS 240

static void warp_update(Animation *animation, const AnimationProgress progress) {
  (void)animation;
  int p = (int)(((int64_t)progress * 1000) / ANIMATION_NORMALIZED_MAX);
  int inv = 1000 - p;
  int eased = 1000 - (int)(((int64_t)inv * inv * inv) / 1000000); /* ease-out cubic */
  s_hero_pm = HERO_PM_START + ((1000 - HERO_PM_START) * eased) / 1000;
  if (s_warp_target) layer_mark_dirty(s_warp_target);
}

static void warp_stopped(Animation *animation, bool finished, void *context) {
  (void)animation;
  (void)finished;
  (void)context;
  s_warp = NULL;
  s_hero_pm = 1000;
  if (s_warp_target) layer_mark_dirty(s_warp_target);
}

void cards_warp_start(Layer *target) {
  cards_warp_cancel();
  s_warp_target = target;
  s_hero_pm = HERO_PM_START;
  static const AnimationImplementation impl = {.update = warp_update};
  s_warp = animation_create();
  if (!s_warp) {
    s_hero_pm = 1000;
    return;
  }
  animation_set_implementation(s_warp, &impl);
  animation_set_duration(s_warp, WARP_MS);
  animation_set_curve(s_warp, AnimationCurveLinear); /* warp_at owns the easing */
  animation_set_handlers(s_warp, (AnimationHandlers){.stopped = warp_stopped}, NULL);
  animation_schedule(s_warp);
}

void cards_warp_cancel(void) {
  if (s_warp) {
    animation_unschedule(s_warp);
    s_warp = NULL;
  }
  s_warp_target = NULL;
  s_hero_pm = 1000;
}

/* Same chunky digits at any y, so drill-down screens share the deck's typography. */
void cards_hero_text(GContext *ctx, const char *text, int y) { draw_hero_at(ctx, text, y); }
