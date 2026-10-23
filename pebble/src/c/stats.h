#pragma once
#include <pebble.h>

#ifndef HG_DEMO
#define HG_DEMO 0
#endif

#define ST_LEN 24

/* Everything the four cards need. Filled from AppMessage tuples whose key
 * numbers are unchanged from the Alloy build (see package.json messageKeys). */
typedef struct {
  int steps, dist_m, calories, resting_hr, sleep_min;
  int step_goal; /* the user's daily target, set in the companion (default 10000) */
  int deep, light, rem, awake;
  char sleep_start[ST_LEN], sleep_end[ST_LEN], updated[ST_LEN];
  int hr_min, hr_max;
  bool has_workout;
  int wo_dur, wo_cal, wo_avg, wo_dist, wo_lo, wo_mid, wo_hi;
  char wo_title[ST_LEN];
  char status[ST_LEN];
} Stats;

extern Stats g_stats;

/* Live heart rate from the companion's BLE strap reader (HR_LIVE* keys).
 * Deliberately NOT part of Stats and never written to the cache: a stale "live"
 * reading tomorrow morning would be a lie. */
typedef struct {
  int bpm, min, max, batt, state;
  uint32_t ts; /* phone clock, seconds */
} LiveHr;

extern LiveHr g_live;

/* True while the last strap sample is fresh (20s): a stale reading must never be
 * presented as live. */
bool stats_live_fresh(void);

void stats_init(void);
/* Short "HH:MM" view of an ISO timestamp, same slicing rules as the JS app. */
const char *stats_clock(const char *v);
/* Same, into the caller's buffer (use this when two times are needed at once). */
void stats_clock_to(char *dst, size_t n, const char *v);
/* "synced HH:MM" once the phone has pushed, "cached HH:MM" while showing the
 * last snapshot read from flash, otherwise the status line. */
const char *stats_footer(void);

/* Persist the last pushed snapshot so the deck is never empty on a cold start.
 * Writes only when the values actually changed (flash wear). */
void stats_load_cache(void);
/* Writes the scalars and the charts; each is skipped when unchanged (flash wear).
 * Safe to call every minute: the change checks are what stop it churning. */
void stats_save_cache(void);
/* True when a write would actually change something (first fill, or new charts). */
bool stats_cache_needed(void);
bool stats_is_cached(void);
