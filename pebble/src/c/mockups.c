#include "mockups.h"

#if HG_MOCKUP

/* Drill-down mockups: what pressing Select on each card could open. Built as a
 * mockup-only screen (cycle with Up/Down, captured by script) so options can be
 * judged at the real size, fonts and palette instead of a sketch.
 *
 * Screens are grouped per card: 1 rows, 2 chart + rows, 3 week / recent list. */

#include <pebble.h>
#include "stats.h"

#define X 10
#define W_ 200
#define BW (W_ - 2 * X)

static void fill(GContext *ctx, GColor c, GRect r) {
  graphics_context_set_fill_color(ctx, c);
  graphics_fill_rect(ctx, r, 0, GCornerNone);
}

static void tx(GContext *ctx, const char *s, GFont f, GColor c, GRect r, GTextAlignment a) {
  graphics_context_set_text_color(ctx, c);
  graphics_draw_text(ctx, s, f, r, GTextOverflowModeTrailingEllipsis, a, NULL);
}

static GFont f24(void) { return fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD); }
static GFont f18(void) { return fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD); }
static GFont f14(void) { return fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD); }

/* cap: same chrome as the deck so the drill-down looks like the same app */
static void chrome(GContext *ctx, int w, GColor cap, const char *title, const char *sub) {
  fill(ctx, GColorWhite, GRect(0, 0, w, 228));
  fill(ctx, cap, GRect(0, 0, w, 36));
  tx(ctx, title, f24(), GColorWhite, GRect(X, 1, BW - 10, 32), GTextAlignmentLeft);
  if (sub) tx(ctx, sub, f14(), GColorWhite, GRect(120, 11, 70, 20), GTextAlignmentRight);
}

static void row(GContext *ctx, int y, const char *label, const char *value) {
  tx(ctx, label, f18(), GColorDarkGray, GRect(X, y + 3, 110, 24), GTextAlignmentLeft);
  tx(ctx, value, f24(), GColorBlack, GRect(100, y, 90, 28), GTextAlignmentRight);
}

/* 24 hourly bars, 6px wide with 2px gaps = 190px */
static void bars_hourly(GContext *ctx, const int v[24], int maxv, GColor col, int top, int h) {
  int base = top + h;
  fill(ctx, GColorLightGray, GRect(X, base, BW, 1));
  for (int i = 0; i < 24; i++) {
    int bh = maxv > 0 ? (v[i] * h) / maxv : 0;
    if (v[i] > 0 && bh < 2) bh = 2;
    fill(ctx, col, GRect(X + i * 8, base - bh, 6, bh));
  }
  tx(ctx, "6a", f14(), GColorDarkGray, GRect(X, base + 1, 30, 18), GTextAlignmentLeft);
  tx(ctx, "noon", f14(), GColorDarkGray, GRect(85, base + 1, 40, 18), GTextAlignmentCenter);
  tx(ctx, "6p", f14(), GColorDarkGray, GRect(160, base + 1, 30, 18), GTextAlignmentRight);
}

/* 7 day bars with weekday letters, index=highlighted */
static void bars_week(GContext *ctx, const int v[7], const char *const *labels, int idx,
                      GColor col, int top, int h, const char *unit) {
  int base = top + h;
  int maxv = 1;
  for (int i = 0; i < 7; i++)
    if (v[i] > maxv) maxv = v[i];
  fill(ctx, GColorLightGray, GRect(X, base, BW, 1));
  for (int i = 0; i < 7; i++) {
    int bh = (v[i] * h) / maxv;
    if (v[i] > 0 && bh < 2) bh = 2;
    int bx = X + i * 26;
    fill(ctx, i == idx ? col : GColorLightGray, GRect(bx, base - bh, 18, bh));
    tx(ctx, labels[i], f14(), i == idx ? GColorBlack : GColorDarkGray,
       GRect(bx - 2, base + 1, 22, 18), GTextAlignmentCenter);
  }
  if (unit) tx(ctx, unit, f14(), GColorDarkGray, GRect(X, top - 16, BW, 18), GTextAlignmentLeft);
}

/* ---------------- screens ---------------- */

static void steps_rows(GContext *ctx, int w) {
  char a[24], b[24], c[24], d[24];
  snprintf(a, sizeof a, "%d", g_stats.steps);
  snprintf(b, sizeof b, "%d.%d km", g_stats.dist_m / 1000, (g_stats.dist_m % 1000) / 100);
  snprintf(c, sizeof c, "%d", g_stats.calories);
  chrome(ctx, w, GColorJaegerGreen, "STEPS", "TODAY");
  int pct = g_stats.steps > 0 ? (g_stats.steps * BW) / 10000 : 0;
  if (pct > BW) pct = BW;
  fill(ctx, GColorLightGray, GRect(X, 44, BW, 6));
  if (pct) fill(ctx, GColorJaegerGreen, GRect(X, 44, pct, 6));
  snprintf(d, sizeof d, "%d%% of 10k", g_stats.steps / 100);
  row(ctx, 56, "Steps", a);
  row(ctx, 88, "Distance", b);
  row(ctx, 120, "Calories", c);
  row(ctx, 152, "Goal", "10,000");
  tx(ctx, d, f14(), GColorDarkGray, GRect(X, 186, BW, 18), GTextAlignmentLeft);
}

static void steps_chart(GContext *ctx, int w) {
  static const int hourly[24] = {0, 0, 0, 0, 0, 0, 80, 180, 320, 520, 780, 1240,
                                 960, 680, 860, 600, 420, 500, 720, 380, 260, 120, 40, 0};
  char a[24];
  chrome(ctx, w, GColorJaegerGreen, "STEPS", "8,240");
  bars_hourly(ctx, hourly, 1240, GColorJaegerGreen, 46, 104);
  snprintf(a, sizeof a, "%d.%d km", g_stats.dist_m / 1000, (g_stats.dist_m % 1000) / 100);
  row(ctx, 166, "Distance", a);
  char c[24];
  snprintf(c, sizeof c, "%d cal", g_stats.calories);
  tx(ctx, c, f18(), GColorDarkGray, GRect(X, 198, BW, 22), GTextAlignmentLeft);
  tx(ctx, "peak 10a", f14(), GColorDarkGray, GRect(110, 200, 80, 20), GTextAlignmentRight);
}

static void steps_week(GContext *ctx, int w) {
  static const int days[7] = {5500, 7200, 4000, 8800, 6500, 9500, 8240};
  static const char *labels[7] = {"M", "T", "W", "T", "F", "S", "S"};
  chrome(ctx, w, GColorJaegerGreen, "STEPS", "7 DAYS");
  bars_week(ctx, days, labels, 6, GColorJaegerGreen, 76, 96, "avg 6,963");
  row(ctx, 166, "Best", "9,500");
  tx(ctx, "Sat", f14(), GColorDarkGray, GRect(X, 200, BW, 18), GTextAlignmentLeft);
}

static void sleep_rows(GContext *ctx, int w) {
  char a[24], b[24], c[24], d[24], e[24];
  chrome(ctx, w, GColorCobaltBlue, "SLEEP", "LAST NIGHT");
  int v[4] = {g_stats.deep, g_stats.light, g_stats.rem, g_stats.awake};
  const GColor cols[4] = {GColorDukeBlue, GColorCobaltBlue, GColorIndigo, GColorWindsorTan};
  int total = v[0] + v[1] + v[2] + v[3];
  int x = X;
  for (int i = 0; i < 4; i++) {
    if (v[i] <= 0) continue;
    int pw = (v[i] * BW) / (total > 0 ? total : 1);
    fill(ctx, cols[i], GRect(x, 44, pw, 10));
    x += pw;
  }
  snprintf(a, sizeof a, "%d:%02d", g_stats.deep / 60, g_stats.deep % 60);
  snprintf(b, sizeof b, "%d:%02d", g_stats.light / 60, g_stats.light % 60);
  snprintf(c, sizeof c, "%d:%02d", g_stats.rem / 60, g_stats.rem % 60);
  snprintf(d, sizeof d, "%d:%02d", g_stats.awake / 60, g_stats.awake % 60);
  snprintf(e, sizeof e, "%d:%02d asleep", g_stats.sleep_min / 60, g_stats.sleep_min % 60);
  row(ctx, 60, "Deep", a);
  row(ctx, 90, "Light", b);
  row(ctx, 120, "REM", c);
  row(ctx, 150, "Awake", d);
  tx(ctx, e, f18(), GColorBlack, GRect(X, 186, BW, 22), GTextAlignmentLeft);
  tx(ctx, "score 82", f14(), GColorDarkGray, GRect(110, 190, 80, 18), GTextAlignmentRight);
}

static void sleep_chart(GContext *ctx, int w) {
  /* hypnogram-ish stage timeline across the night */
  chrome(ctx, w, GColorCobaltBlue, "SLEEP", "6:42");
  static const uint8_t stages[24] = {3, 2, 1, 0, 0, 1, 2, 0, 1, 1, 0, 2,
                                     1, 0, 0, 1, 2, 1, 0, 0, 1, 2, 3, 3};
  const GColor cols[4] = {GColorDukeBlue, GColorCobaltBlue, GColorIndigo, GColorWindsorTan};
  for (int i = 0; i < 24; i++) {
    int band = stages[i];
    int bh = 18;
    fill(ctx, cols[band], GRect(X + i * 8, 56 + band * 18, 6, bh));
  }
  tx(ctx, "23:47", f14(), GColorDarkGray, GRect(X, 132, 60, 18), GTextAlignmentLeft);
  tx(ctx, "06:29", f14(), GColorDarkGray, GRect(130, 132, 60, 18), GTextAlignmentRight);
  row(ctx, 156, "Deep", "1:01");
  row(ctx, 188, "REM", "1:28");
}

static void sleep_week(GContext *ctx, int w) {
  static const int nights[7] = {420, 480, 360, 510, 450, 400, 402};
  static const char *labels[7] = {"M", "T", "W", "T", "F", "S", "S"};
  chrome(ctx, w, GColorCobaltBlue, "SLEEP", "7 NIGHTS");
  bars_week(ctx, nights, labels, 6, GColorCobaltBlue, 76, 96, "avg 7:11");
  row(ctx, 166, "Best", "8:30");
  tx(ctx, "Wed", f14(), GColorDarkGray, GRect(X, 200, BW, 18), GTextAlignmentLeft);
}

static void heart_rows(GContext *ctx, int w) {
  char a[24], b[24], c[24];
  chrome(ctx, w, GColorImperialPurple, "HEART", "TODAY");
  fill(ctx, GColorLightGray, GRect(X, 44, BW, 6));
  if (g_stats.hr_max > g_stats.hr_min) {
    int lo = X + ((g_stats.hr_min - g_stats.hr_min) * BW) / (g_stats.hr_max - g_stats.hr_min);
    int hi = X + BW;
    fill(ctx, GColorPictonBlue, GRect(lo + 10, 44, hi - lo - 10, 6));
  }
  snprintf(a, sizeof a, "%d", g_stats.resting_hr);
  snprintf(b, sizeof b, "%d", g_stats.hr_min);
  snprintf(c, sizeof c, "%d", g_stats.hr_max);
  row(ctx, 56, "Resting", a);
  row(ctx, 88, "Min", b);
  row(ctx, 120, "Max", c);
  row(ctx, 152, "Sample", "5 min");
  tx(ctx, "zone 2 for 41 min", f14(), GColorDarkGray, GRect(X, 190, BW, 18),
     GTextAlignmentLeft);
}

static void heart_chart(GContext *ctx, int w) {
  static const int hr[24] = {52, 50, 49, 50, 55, 61, 78, 96, 88, 72, 84, 110,
                             132, 96, 88, 92, 104, 118, 96, 84, 74, 66, 58, 54};
  chrome(ctx, w, GColorImperialPurple, "HEART", "REST 61");
  bars_hourly(ctx, hr, 132, GColorImperialPurple, 46, 104);
  row(ctx, 166, "Peak", "152");
  tx(ctx, "at 12:40", f14(), GColorDarkGray, GRect(X, 200, BW, 18), GTextAlignmentLeft);
}

static void heart_week(GContext *ctx, int w) {
  static const int rhr[7] = {64, 63, 66, 62, 60, 63, 61};
  static const char *labels[7] = {"M", "T", "W", "T", "F", "S", "S"};
  chrome(ctx, w, GColorImperialPurple, "HEART", "7 DAYS");
  bars_week(ctx, rhr, labels, 6, GColorImperialPurple, 76, 96, "resting avg 63");
  row(ctx, 166, "Lowest", "60");
  tx(ctx, "Thu", f14(), GColorDarkGray, GRect(X, 200, BW, 18), GTextAlignmentLeft);
}

static void wo_rows(GContext *ctx, int w) {
  char a[24], b[24], c[24], d[24], e[64];
  chrome(ctx, w, GColorOrange, "WALK", "TODAY");
  snprintf(a, sizeof a, "%02d:%02d", g_stats.wo_dur / 60, g_stats.wo_dur % 60);
  snprintf(b, sizeof b, "%d.%d km", g_stats.wo_dist / 1000, (g_stats.wo_dist % 1000) / 100);
  snprintf(c, sizeof c, "%d bpm", g_stats.wo_avg);
  snprintf(d, sizeof d, "%d", g_stats.wo_cal);
  snprintf(e, sizeof e, "%d min low / %d mid / %d high", g_stats.wo_lo, g_stats.wo_mid,
           g_stats.wo_hi);
  row(ctx, 50, "Duration", a);
  row(ctx, 82, "Distance", b);
  row(ctx, 114, "Avg HR", c);
  row(ctx, 146, "Calories", d);
  tx(ctx, e, f14(), GColorDarkGray, GRect(X, 190, BW, 18), GTextAlignmentLeft);
}

static void wo_zones(GContext *ctx, int w) {
  chrome(ctx, w, GColorOrange, "WALK", "31:15");
  const GColor cols[3] = {GColorRajah, GColorOrange, GColorDarkCandyAppleRed};
  const int v[3] = {g_stats.wo_lo, g_stats.wo_mid, g_stats.wo_hi};
  int total = v[0] + v[1] + v[2];
  int y = 52;
  for (int i = 0; i < 2; i++) { /* placeholder to keep loop simple */
  }
  for (int i = 0; i < 3; i++) {
    int pw = total > 0 ? (v[i] * BW) / total : 0;
    if (pw) fill(ctx, cols[i], GRect(X, y + i * 30, pw, 20));
    fill(ctx, GColorLightGray, GRect(X, y + i * 30, BW, 20));
    int pw2 = total > 0 ? (v[i] * BW) / total : 0;
    if (pw2) fill(ctx, cols[i], GRect(X, y + i * 30, pw2, 20));
    char t[16];
    snprintf(t, sizeof t, "%d%%", v[i]);
    tx(ctx, t, f14(), GColorBlack, GRect(X + 4, y + i * 30 + 2, 40, 18), GTextAlignmentLeft);
  }
  row(ctx, 152, "Calories", "297");
  tx(ctx, "peak 168 bpm", f14(), GColorDarkGray, GRect(X, 190, BW, 18), GTextAlignmentLeft);
}

static void wo_recent(GContext *ctx, int w) {
  static const char *names[4] = {"WALK", "RUN", "RIDE", "WALK"};
  static const char *times[4] = {"31:15", "42:18", "1:02:05", "18:40"};
  static const char *meta[4] = {"today  137 avg", "yesterday  148 avg", "Tue  121 avg",
                                "Mon  112 avg"};
  chrome(ctx, w, GColorOrange, "WORKOUTS", "1/4");
  for (int i = 0; i < 4; i++) {
    int y = 46 + i * 44;
    if (i == 0) fill(ctx, GColorRajah, GRect(0, y - 4, w, 40));
    tx(ctx, names[i], f18(), GColorBlack, GRect(X, y, 120, 22), GTextAlignmentLeft);
    tx(ctx, times[i], f24(), GColorBlack, GRect(100, y - 4, 90, 28), GTextAlignmentRight);
    tx(ctx, meta[i], f14(), i == 0 ? GColorBlack : GColorDarkGray,
       GRect(X, y + 18, BW, 16), GTextAlignmentLeft);
  }
}

/* ---------------- public ---------------- */

int mockups_count(void) { return 12; }

void mockups_draw(GContext *ctx, GRect bounds, int screen) {
  const int w = bounds.size.w;
  switch (screen) {
    case 0: steps_rows(ctx, w); break;
    case 1: steps_chart(ctx, w); break;
    case 2: steps_week(ctx, w); break;
    case 3: sleep_rows(ctx, w); break;
    case 4: sleep_chart(ctx, w); break;
    case 5: sleep_week(ctx, w); break;
    case 6: heart_rows(ctx, w); break;
    case 7: heart_chart(ctx, w); break;
    case 8: heart_week(ctx, w); break;
    case 9: wo_rows(ctx, w); break;
    case 10: wo_zones(ctx, w); break;
    default: wo_recent(ctx, w); break;
  }
}

const char *mockups_name(int screen) {
  static const char *names[12] = {"steps-rows",  "steps-chart", "steps-week",
                                  "sleep-rows",  "sleep-chart", "sleep-week",
                                  "heart-rows",  "heart-chart", "heart-week",
                                  "wo-rows",     "wo-zones",    "wo-recent"};
  if (screen < 0 || screen >= 12) return "?";
  return names[screen];
}

#endif /* HG_MOCKUP */
