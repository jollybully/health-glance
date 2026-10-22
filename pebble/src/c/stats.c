#include "stats.h"

Stats g_stats;

static void set_clock(char *dst, const char *src) {
  strncpy(dst, src, ST_LEN - 1);
  dst[ST_LEN - 1] = '\0';
}

void stats_init(void) {
  memset(&g_stats, 0, sizeof(g_stats));
  strcpy(g_stats.status, "Waiting for phone");

#if HG_DEMO
  /* Same fixture numbers the JS app used for emulator QA. */
  g_stats.steps = 7611;
  g_stats.dist_m = 6500;
  g_stats.calories = 2345;
  g_stats.resting_hr = 58;
  g_stats.sleep_min = 435;
  g_stats.deep = 72;
  g_stats.light = 210;
  g_stats.rem = 95;
  g_stats.awake = 18;
  set_clock(g_stats.sleep_start, "23:12");
  set_clock(g_stats.sleep_end, "06:45");
  set_clock(g_stats.updated, "10:04");
  g_stats.hr_min = 52;
  g_stats.hr_max = 148;
  g_stats.has_workout = true;
  g_stats.wo_dur = 2538;
  g_stats.wo_cal = 386;
  g_stats.wo_avg = 142;
  g_stats.wo_dist = 5200;
  g_stats.wo_lo = 40;
  g_stats.wo_mid = 45;
  g_stats.wo_hi = 15;
  strcpy(g_stats.wo_title, "LAST RUN");
  strcpy(g_stats.status, "Demo");
#endif
}

/* Short "HH:MM" view of an ISO timestamp, same slicing rules as the JS app.
 * Writes into the caller's buffer: two clock values are often needed at once
 * (sleep start/end), so never share a static buffer between calls. */
void stats_clock_to(char *dst, size_t n, const char *v) {
  if (!v || !v[0]) {
    snprintf(dst, n, "--:--");
    return;
  }
  size_t len = strlen(v);
  if (len >= 16 && v[10] == 'T') {
    snprintf(dst, n, "%.5s", v + 11);
  } else if (len >= 5 && v[2] == ':') {
    snprintf(dst, n, "%.5s", v);
  } else {
    snprintf(dst, n, "%s", v);
  }
}

const char *stats_clock(const char *v) {
  static char buf[8];
  stats_clock_to(buf, sizeof(buf), v);
  return buf;
}

const char *stats_footer(void) {
  static char buf[ST_LEN + 12];
  if (g_stats.updated[0]) {
    snprintf(buf, sizeof(buf), "synced %s", stats_clock(g_stats.updated));
    return buf;
  }
  return g_stats.status;
}
