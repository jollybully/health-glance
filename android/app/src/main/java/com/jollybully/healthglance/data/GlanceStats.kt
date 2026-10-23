package com.jollybully.healthglance.data

data class ActivityStats(
    val steps: Long = 0,
    val stepGoal: Int = 10_000,
    val distanceKm: Double = 0.0,
    val calories: Long = 0,
    val activeMinutes: Int = 0,
    val restingHr: Int = 0,
    val floors: Int = 0,
    /** Oldest → today, length 7. */
    val stepsWeek: List<Int> = List(7) { 0 },
    /** Local-hour buckets 0–23. */
    val stepsHourly: List<Int> = List(24) { 0 },
)

data class SleepStages(
    val deep: Int = 0,
    val light: Int = 0,
    val rem: Int = 0,
    val awake: Int = 0,
)

data class SleepStats(
    val start: String = "",
    val end: String = "",
    val minutesAsleep: Int = 0,
    val stages: SleepStages = SleepStages(),
    /** Oldest → last night, length 7 (asleep minutes). */
    val sleepWeek: List<Int> = List(7) { 0 },
    /** Stage across the night in 24 equal slots: 0 deep, 1 light, 2 rem, 3 awake. */
    val spo2Percent: Int = 0,
    val hrvMs: Int = 0,
)

data class HeartStats(
    val restingHr: Int = 0,
    val minHr: Int = 0,
    val maxHr: Int = 0,
    /** Percent of today's samples in low / mid / high zones (sum ≈ 100). */
    val zoneLow: Int = 0,
    val zoneMid: Int = 0,
    val zoneHigh: Int = 0,
    /** Highest bpm in each local hour 0–23, 0 where there were no samples. */
)

data class WorkoutStats(
    val title: String = "",
    val durationSec: Int = 0,
    val calories: Int = 0,
    val avgHr: Int = 0,
    val peakHr: Int = 0,
    val distanceM: Int = 0,
    val start: String = "",
    val end: String = "",
    val zoneLow: Int = 0,
    val zoneMid: Int = 0,
    val zoneHigh: Int = 0,
    /** Newest first, at most seven, for the watch's Workouts screen. */
    val history: List<WorkoutEntry> = emptyList(),
)

data class GlanceStats(
    val updatedAt: String = "",
    val source: String = "health-connect",
    val activity: ActivityStats = ActivityStats(),
    val sleep: SleepStats = SleepStats(),
    val heart: HeartStats = HeartStats(),
    val workout: WorkoutStats = WorkoutStats(),
)
