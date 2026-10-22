package com.jollybully.healthglance.data

/**
 * The last seven exercise sessions, newest first, as the watch's Workouts screen
 * cycles through them.
 */
data class WorkoutEntry(
    /** Index into the watch's sport table: 0 walk, 1 run, 2 ride, 3 swim/other. */
    val sport: Int = 0,
    val durationSec: Int = 0,
    val distanceM: Int = 0,
    val avgHr: Int = 0,
    val daysAgo: Int = 0,
)
