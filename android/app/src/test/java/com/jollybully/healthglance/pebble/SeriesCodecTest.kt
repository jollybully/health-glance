package com.jollybully.healthglance.pebble

import com.jollybully.healthglance.data.ActivityStats
import com.jollybully.healthglance.data.HeartStats
import com.jollybully.healthglance.data.SleepStats
import com.jollybully.healthglance.data.WorkoutEntry
import com.jollybully.healthglance.data.WorkoutStats
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * The byte layout of the detail-screen series is a contract with detail.c: if
 * these drift, the watch draws the wrong chart silently. Every field is checked
 * against the layout documented in SeriesCodec.
 */
class SeriesCodecTest {

    /** Kotlin bytes are signed; the wire format is unsigned, so read it that way. */
    private fun Byte.u(): Int = this.toInt() and 0xFF

    @Test
    fun `steps are sent in hundreds and clamp at 255`() {
        val hourly = MutableList(24) { 0 }
        hourly[9] = 520
        hourly[10] = 1240
        hourly[11] = 999_999 // a nonsense hour must not wrap
        val bytes = SeriesCodec.stepsSeries(ActivityStats(stepsHourly = hourly))
        assertEquals(24, bytes.size)
        assertEquals(5, bytes[9].u())
        assertEquals(12, bytes[10].u())
        assertEquals(255, bytes[11].u())
        assertEquals(0, bytes[0].u())
    }

    @Test
    fun `short input is padded instead of throwing`() {
        val bytes = SeriesCodec.stepsSeries(ActivityStats(stepsHourly = listOf(300)))
        assertEquals(24, bytes.size)
        assertEquals(3, bytes[0].u())
        assertEquals(0, bytes[23].u())
    }

    @Test
    fun `heart hours keep the peak and pass zero through`() {
        val hourly = MutableList(24) { 0 }
        hourly[12] = 148
        hourly[3] = 49
        val bytes = SeriesCodec.heartSeries(HeartStats(hrHourly = hourly))
        assertEquals(148, bytes[12].u())
        assertEquals(49, bytes[3].u())
        assertEquals(0, bytes[7].u())
    }

    @Test
    fun `sleep slots map to 0-3 and reject anything higher`() {
        val slots = MutableList(24) { 1 }
        slots[0] = 0
        slots[1] = 2
        slots[2] = 3
        slots[3] = 99
        val bytes = SeriesCodec.sleepSeries(SleepStats(stageSlots = slots))
        assertEquals(0, bytes[0].u())
        assertEquals(2, bytes[1].u())
        assertEquals(3, bytes[2].u())
        assertEquals(3, bytes[3].u())
    }

    @Test
    fun `workout week accumulates minutes oldest first`() {
        val history = listOf(
            WorkoutEntry(durationSec = 31 * 60, daysAgo = 0),
            WorkoutEntry(durationSec = 42 * 60, daysAgo = 1),
            WorkoutEntry(durationSec = 30 * 60, daysAgo = 1), // same day adds up
            WorkoutEntry(durationSec = 60 * 60, daysAgo = 9), // outside the week
        )
        val bytes = SeriesCodec.workoutWeek(WorkoutStats(history = history))
        assertEquals(7, bytes.size)
        assertEquals(31, bytes[6].u()) // today is the last slot
        assertEquals(72, bytes[5].u()) // 42 + 30
        assertEquals(0, bytes[0].u())
    }

    @Test
    fun `workout history packs duration distance hr days and sport`() {
        val history = listOf(
            WorkoutEntry(sport = 1, durationSec = 2538, distanceM = 6100, avgHr = 148, daysAgo = 1),
            WorkoutEntry(sport = 2, durationSec = 3732, distanceM = 18_000, avgHr = 121, daysAgo = 3),
        )
        val bytes = SeriesCodec.workoutHistory(WorkoutStats(history = history))
        assertEquals(42, bytes.size)
        // newest (smallest daysAgo) first
        assertEquals(2538 and 0xFF, bytes[0].u())
        assertEquals(2538 shr 8, bytes[1].u())
        assertEquals(61, bytes[2].u()) // 6.1 km
        assertEquals(148, bytes[3].u())
        assertEquals(1, bytes[4].u())
        assertEquals(1, bytes[5].u()) // run
        assertEquals(3732 and 0xFF, bytes[6].u())
        assertEquals(180, bytes[8].u()) // 18.0 km
        assertEquals(2, bytes[11].u()) // ride
    }

    @Test
    fun `zero-length sessions are dropped and the rest zero-padded`() {
        val history = listOf(
            WorkoutEntry(sport = 0, durationSec = 0, daysAgo = 0),
            WorkoutEntry(sport = 0, durationSec = 600, distanceM = 500, avgHr = 120, daysAgo = 2),
        )
        val bytes = SeriesCodec.workoutHistory(WorkoutStats(history = history))
        assertEquals(600 and 0xFF, bytes[0].u())
        assertArrayEquals(ByteArray(36), bytes.copyOfRange(6, 42))
    }

    @Test
    fun `more than seven sessions keep only the seven newest`() {
        val history = (0 until 12).map {
            WorkoutEntry(sport = 0, durationSec = 600 + it, distanceM = 100, avgHr = 120, daysAgo = it)
        }
        val bytes = SeriesCodec.workoutHistory(WorkoutStats(history = history))
        assertEquals(42, bytes.size)
        assertEquals(0, bytes[4].u()) // days ago of the first kept entry
        // the eighth-oldest entry must not be present
        val days = (0 until 7).map { bytes[it * 6 + 4].u() }
        assertEquals(listOf(0, 1, 2, 3, 4, 5, 6), days)
    }

    @Test
    fun `all five keys are always present`() {
        val keys = SeriesCodec.all(
            com.jollybully.healthglance.data.GlanceStats(),
        ).keys
        assertEquals(
            setOf("STEPS_SERIES", "SLEEP_SERIES", "HR_SERIES", "WO_SERIES", "WO_HIST"),
            keys,
        )
    }
}
