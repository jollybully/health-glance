#pragma once
#include <pebble.h>

/* Drill-down mockups (HG_MOCKUP builds only). */
#ifndef HG_MOCKUP
#define HG_MOCKUP 0
#endif

int mockups_count(void);
void mockups_draw(GContext *ctx, GRect bounds, int screen);
const char *mockups_name(int screen);
