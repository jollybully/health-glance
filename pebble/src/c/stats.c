#include "stats.h"

Stats g_stats;
LiveHr g_live;

#define CACHE_KEY 1u
#define SERIES_KEY 2u
#define CACHE_MAGIC 0x48475331u /* "HGS1" */
#define CACHE_VERSION 1
#define CACHE_HAS_WORKOUT 0x1

/* Kept well under PERSIST_DATA_MAX_LENGTH (256 bytes per key). */
typedef struct {
  uint32_t magic;
  uint16_t version;
  uint16_t flags;
  int32_t steps, dist_m, calories, resting_hr, sleep_min;
  int32_t deep, light, rem, awake;
  int32_t hr_min, hr_max;
  int32_t wo_dur, wo_cal, wo_avg, wo_dist, wo_lo, wo_mid, wo_hi;
  char updated[8], sleep_start[8], sleep_end[8];
  char wo_title[13];
} CacheBlob;

static bool s_from_cache;
static bool s_have_last;
static CacheBlob s_last;

#if !HG_DEMO
static void blob_from_stats(CacheBlob *b) {
  memset(b, 0, sizeof(*b)); /* deterministic padding: the change check memcmps */
  b->magic = CACHE_MAGIC;
  b->version = CACHE_VERSION;
  b->flags = g_stats.has_workout ? CACHE_HAS_WORKOUT : 0;
  b->steps = g_stats.steps;
  b->dist_m = g_stats.dist_m;
  b->calories = g_stats.calories;
  b->resting_hr = g_stats.resting_hr;
  b->sleep_min = g_stats.sleep_min;
  b->deep = g_stats.deep;
  b->light = g_stats.light;
  b->rem = g_stats.rem;
  b->awake = g_stats.awake;
  b->hr_min = g_stats.hr_min;
  b->hr_max = g_stats.hr_max;
  b->wo_dur = g_stats.wo_dur;
  b->wo_cal = g_stats.wo_cal;
  b->wo_avg = g_stats.wo_avg;
  b->wo_dist = g_stats.wo_dist;
  b->wo_lo = g_stats.wo_lo;
  b->wo_mid = g_stats.wo_mid;
  b->wo_hi = g_stats.wo_hi;
  snprintf(b->updated, sizeof(b->updated), "%s", g_stats.updated);
  snprintf(b->sleep_start, sizeof(b->sleep_start), "%s", g_stats.sleep_start);
  snprintf(b->sleep_end, sizeof(b->sleep_end), "%s", g_stats.sleep_end);
  snprintf(b->wo_title, sizeof(b->wo_title), "%s", g_stats.wo_title);
}
#endif /* !HG_DEMO */

static void stats_from_blob(const CacheBlob *b) {
  g_stats.steps = b->steps;
  g_stats.dist_m = b->dist_m;
  g_stats.calories = b->calories;
  g_stats.resting_hr = b->resting_hr;
  g_stats.sleep_min = b->sleep_min;
  g_stats.deep = b->deep;
  g_stats.light = b->light;
  g_stats.rem = b->rem;
  g_stats.awake = b->awake;
  g_stats.hr_min = b->hr_min;
  g_stats.hr_max = b->hr_max;
  g_stats.has_workout = (b->flags & CACHE_HAS_WORKOUT) != 0;
  g_stats.wo_dur = b->wo_dur;
  g_stats.wo_cal = b->wo_cal;
  g_stats.wo_avg = b->wo_avg;
  g_stats.wo_dist = b->wo_dist;
  g_stats.wo_lo = b->wo_lo;
  g_stats.wo_mid = b->wo_mid;
  g_stats.wo_hi = b->wo_hi;
  snprintf(g_stats.updated, sizeof(g_stats.updated), "%s", b->updated);
  snprintf(g_stats.sleep_start, sizeof(g_stats.sleep_start), "%s", b->sleep_start);
  snprintf(g_stats.sleep_end, sizeof(g_stats.sleep_end), "%s", b->sleep_end);
  snprintf(g_stats.wo_title, sizeof(g_stats.wo_title), "%s", b->wo_title);
}

void stats_load_cache(void) {
  CacheBlob b;
  int read = persist_read_data(CACHE_KEY, &b, sizeof(b));
  if (read == (int)sizeof(b) && b.magic == CACHE_MAGIC && b.version == CACHE_VERSION) {
    stats_from_blob(&b);
    s_last = b;
    s_have_last = true;
    s_from_cache = true;
  }
}

bool stats_cache_needed(void) {
  return !s_from_cache;
}

void stats_save_cache(void) {
#if HG_DEMO
  return; /* never let emulator fixtures masquerade as cached health data */
#else
  s_from_cache = false;
  CacheBlob b;
  blob_from_stats(&b);
  if (!s_have_last || memcmp(&b, &s_last, sizeof(b)) != 0) {
    /* unchanged data spares the flash, but a cold-start read still needs it once */
    if (persist_write_data(CACHE_KEY, &b, sizeof(b)) == (int)sizeof(b)) {
      s_last = b;
      s_have_last = true;
      APP_LOG(APP_LOG_LEVEL_INFO, "cache written (%d bytes)", (int)sizeof(b));
    } else {
      APP_LOG(APP_LOG_LEVEL_ERROR, "cache write FAILED");
    }
  }
#endif
}

bool stats_is_cached(void) { return s_from_cache; }

bool stats_live_fresh(void) {
  if (g_live.bpm <= 0 || g_live.ts == 0) return false;
  return (time(NULL) - (time_t)g_live.ts) <= 20;
}

void stats_init(void) {
  memset(&g_stats, 0, sizeof(g_stats));
  strcpy(g_stats.status, "Waiting for phone");
  stats_load_cache();
  if (s_from_cache) strcpy(g_stats.status, "Cached");

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
  snprintf(g_stats.sleep_start, sizeof(g_stats.sleep_start), "23:12");
  snprintf(g_stats.sleep_end, sizeof(g_stats.sleep_end), "06:45");
  snprintf(g_stats.updated, sizeof(g_stats.updated), "10:04");
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
    snprintf(buf, sizeof(buf), "%s %s", s_from_cache ? "cached" : "synced",
             stats_clock(g_stats.updated));
    return buf;
  }
  return g_stats.status;
}
