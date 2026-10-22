package com.jollybully.healthglance.pebble

import com.jollybully.healthglance.data.ActivityStats
import com.jollybully.healthglance.data.GlanceStats
import com.jollybully.healthglance.data.HeartStats
import com.jollybully.healthglance.data.SleepStats
import com.jollybully.healthglance.data.WorkoutStats

/**
 * Packs the detail screens' series into AppMessage byte arrays.
 *
 * AppMessage keys are far too precious for 96 hourly counters: four 24-slot
 * charts and the workout history travel as five byte-array keys instead. The
 * watch decodes them in detail.c — the layout here IS the contract, so it is
 * pure code with unit tests rather than something buried in the sync loop.
 *
 *   STEPS_SERIES  24 bytes  steps per hour / 100   (0..255 => 0..25500 steps)
 *   SLEEP_SERIES  24 bytes  stage 0 deep, 1 light, 2 rem, 3 awake
 *   HR_SERIES     24 bytes  highest bpm in the hour (0 = no samples)
 *   WO_SERIES      7 bytes  workout minutes per day, Mon..Sun
 *   WO_HIST       42 bytes  7 x 6: duration s (LE16), distance km x10,
 *                           average bpm, days ago, sport
 */
object SeriesCodec {

    const val SLOTS = 24
    const val DAYS = 7
    const val WO_ENTRY = 6
    const val WO_MAX = 7

    /** Steps per hour, in hundreds — the watch multiplies back up. */
    fun stepsSeries(activity: ActivityStats): ByteArray =
        ByteArray(SLOTS) { i ->
            val hundreds = (activity.stepsHourly.getOrElse(i) { 0 } / 100)
            hundreds.coerceIn(0, 255).toByte()
        }

    /** The night as 24 stage slots, each 0..3. */
    fun sleepSeries(sleep: SleepStats): ByteArray =
        ByteArray(SLOTS) { i -> sleep.stageSlots.getOrElse(i) { 1 }.coerceIn(0, 3).toByte() }

    /** Highest bpm per hour. A missing hour is 0 and the watch draws no bar. */
    fun heartSeries(heart: HeartStats): ByteArray =
        ByteArray(SLOTS) { i -> heart.hrHourly.getOrElse(i) { 0 }.coerceIn(0, 255).toByte() }

    /**
     * Minutes trained on each of the last seven days, oldest first, derived from
     * the session history (so a 90-minute ride cannot overflow a byte).
     */
    fun workoutWeek(workouts: WorkoutStats): ByteArray {
        val minutes = IntArray(DAYS)
        for (e in workouts.history) {
            if (e.daysAgo !in 0 until DAYS) continue
            val dayIndex = DAYS - 1 - e.daysAgo
            minutes[dayIndex] += e.durationSec / 60
        }
        return ByteArray(DAYS) { i -> minutes[i].coerceIn(0, 255).toByte() }
    }

    /**
     * Seven sessions, newest first. Entries with no duration are dropped rather
     * than sent as zero-second workouts, and the buffer is zero-padded so the
     * watch can read a fixed 42 bytes.
     */
    fun workoutHistory(workouts: WorkoutStats): ByteArray {
        val out = ByteArray(WO_MAX * WO_ENTRY)
        val usable = workouts.history
            .filter { it.durationSec > 0 }
            .sortedBy { it.daysAgo }
            .take(WO_MAX)
        usable.forEachIndexed { i, e ->
            val o = i * WO_ENTRY
            val secs = e.durationSec.coerceIn(0, 0xFFFF)
            out[o] = (secs and 0xFF).toByte()
            out[o + 1] = ((secs shr 8) and 0xFF).toByte()
            out[o + 2] = ((e.distanceM / 100).coerceIn(0, 255)).toByte()
            out[o + 3] = e.avgHr.coerceIn(0, 255).toByte()
            out[o + 4] = e.daysAgo.coerceIn(0, 255).toByte()
            out[o + 5] = e.sport.coerceIn(0, 3).toByte()
        }
        return out
    }

    /** Everything the watch needs for its four detail screens except HEAD ratios. */
    fun all(stats: GlanceStats): Map<String, ByteArray> = mapOf(
        "STEPS_SERIES" to stepsSeries(stats.activity),
        "SLEEP_SERIES" to sleepSeries(stats.sleep),
        "HR_SERIES" to heartSeries(stats.heart),
        "WO_SERIES" to workoutWeek(stats.workout),
        "WO_HIST" to workoutHistory(stats.workout),
    )

    /**
     * A one-line account of what the series actually contain, so the app can show
     * it. "0/24" hours means Health Connect had no samples, which is a different
     * fault from the watch never receiving the bytes.
     */
    fun describe(stats: com.jollybully.healthglance.data.GlanceStats): String {
        val steps = stepsSeries(stats.activity)
        val hr = heartSeries(stats.heart)
        val slp = sleepSeries(stats.sleep)
        val week = workoutWeek(stats.workout)
        val hist = workoutHistory(stats.workout)
        val woCount = (0 until WO_MAX).count { hist[it * WO_ENTRY].toInt() != 0 || hist[it * WO_ENTRY + 1].toInt() != 0 }
        return "steps ${steps.count { it.toInt() != 0 }}/24 · " +
            "hr ${hr.count { it.toInt() != 0 }}/24 · " +
            "sleep ${slp.size}B · week ${week.count { it.toInt() != 0 }}/7 · " +
            "sessions $woCount"
    }
}
