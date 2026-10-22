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
  int deep, light, rem, awake;
  char sleep_start[ST_LEN], sleep_end[ST_LEN], updated[ST_LEN];
  int hr_min, hr_max;
  bool has_workout;
  int wo_dur, wo_cal, wo_avg, wo_dist, wo_lo, wo_mid, wo_hi;
  char wo_title[ST_LEN];
  char status[ST_LEN];
} Stats;

extern Stats g_stats;

void stats_init(void);
/* Short "HH:MM" view of an ISO timestamp, same slicing rules as the JS app. */
const char *stats_clock(const char *v);
/* Same, into the caller's buffer (use this when two times are needed at once). */
void stats_clock_to(char *dst, size_t n, const char *v);
/* "synced HH:MM" once the phone has pushed, otherwise the status line. */
const char *stats_footer(void);
