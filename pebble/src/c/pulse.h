#pragma once
#include <pebble.h>

/* A frame around the screen that pulses at the live heart rate. The bpm comes
 * from the companion's BLE strap reader; with no fresh sample the animation
 * stops rather than pretending. */

void pulse_init(Layer *parent, GRect bounds);
void pulse_deinit(void);

/* Set the rate. 0 stops it. Call on every live sample so staleness is tracked. */
void pulse_set_bpm(int bpm);
void pulse_clear(void);
/* Hide the pulse while a detail screen owns the display. */
void pulse_set_visible(bool visible);
