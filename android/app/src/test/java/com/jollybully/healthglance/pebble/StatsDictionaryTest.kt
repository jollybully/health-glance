package com.jollybully.healthglance.pebble

import com.jollybully.healthglance.data.ActivityStats
import com.jollybully.healthglance.data.GlanceStats
import com.jollybully.healthglance.data.HeartStats
import com.jollybully.healthglance.data.SleepStages
import com.jollybully.healthglance.data.SleepStats
import com.jollybully.healthglance.data.WorkoutStats
import io.rebble.pebblekit2.common.model.PebbleDictionaryItem
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class StatsDictionaryTest {
    @Test
    fun toDictionary_mapsRootAndWorkoutFields() {
        val dict = sampleStats().toDictionary()
        assertEquals(7611u, (dict[MessageKeys.STEPS] as PebbleDictionaryItem.UInt32).value)
        assertEquals(58u, (dict[MessageKeys.RESTING_HR] as PebbleDictionaryItem.UInt32).value)
        assertEquals(52u, (dict[MessageKeys.HR_MIN] as PebbleDictionaryItem.UInt32).value)
        assertEquals("10:00", (dict[MessageKeys.UPDATED] as PebbleDictionaryItem.Text).value)
        assertEquals("23:12", (dict[MessageKeys.SLEEP_START] as PebbleDictionaryItem.Text).value)
        assertEquals("LAST RUN", (dict[MessageKeys.WO_TITLE] as PebbleDictionaryItem.Text).value)
        assertEquals("07:32", (dict[MessageKeys.WO_START] as PebbleDictionaryItem.Text).value)
        assertEquals(2538u, (dict[MessageKeys.WO_DUR_S] as PebbleDictionaryItem.UInt32).value)
        assertEquals(40u, (dict[MessageKeys.WO_ZONE_LOW] as PebbleDictionaryItem.UInt32).value)
        assertEquals(45u, (dict[MessageKeys.WO_ZONE_MID] as PebbleDictionaryItem.UInt32).value)
        assertEquals(15u, (dict[MessageKeys.WO_ZONE_HIGH] as PebbleDictionaryItem.UInt32).value)
        assertTrue(!dict.containsKey(MessageKeys.STEPS_WEEK))
        assertTrue(!dict.containsKey(MessageKeys.STEPS_HOUR))
        assertTrue(!dict.containsKey(MessageKeys.FLOORS))
        assertTrue(!dict.containsKey(MessageKeys.SPO2))
        assertTrue(!dict.containsKey(MessageKeys.ZONE_LOW))
        assertTrue(!dict.containsKey(MessageKeys.SOURCE))
    }

    @Test
    fun toDictionary_includesZeroWorkoutWhenAbsent() {
        val dict = sampleStats().copy(workout = WorkoutStats()).toDictionary()
        assertEquals(0u, (dict[MessageKeys.WO_DUR_S] as PebbleDictionaryItem.UInt32).value)
        assertEquals("", (dict[MessageKeys.WO_TITLE] as PebbleDictionaryItem.Text).value)
    }

    @Test
    fun toRootDictionary_omitsSeriesAndWorkout() {
        val dict = sampleStats().toRootDictionary()
        assertEquals(7611u, (dict[MessageKeys.STEPS] as PebbleDictionaryItem.UInt32).value)
        assertEquals(58u, (dict[MessageKeys.RESTING_HR] as PebbleDictionaryItem.UInt32).value)
        assertTrue(!dict.containsKey(MessageKeys.STEPS_WEEK))
        assertTrue(!dict.containsKey(MessageKeys.STEPS_HOUR))
        assertTrue(!dict.containsKey(MessageKeys.WO_TITLE))
        assertTrue(!dict.containsKey(MessageKeys.FLOORS))
    }

    @Test
    fun packing_roundTripsWeekAndHourly() {
        val week = listOf(5500, 7200, 4000, 8800, 6500, 9500, 7611)
        assertEquals(week, StatsPacking.unpackUInt16LE(StatsPacking.packUInt16LE(week)))
        val hourly = List(24) { (it * 50) % 2000 }
        val decoded = StatsPacking.unpackHourlySteps(StatsPacking.packHourlySteps(hourly))
        for (i in 0 until 24) {
            assertEquals((hourly[i] / 40).coerceAtMost(255) * 40, decoded[i])
        }
    }

    private fun sampleStats() = GlanceStats(
        updatedAt = "2026-07-20T10:00:00Z",
        source = "health-connect",
        activity = ActivityStats(
            steps = 7611,
            stepGoal = 10000,
            distanceKm = 6.5,
            calories = 2345,
            activeMinutes = 158,
            restingHr = 58,
            floors = 14,
            stepsWeek = listOf(5500, 7200, 4000, 8800, 6500, 9500, 7611),
            stepsHourly = List(24) { it * 40 },
        ),
        sleep = SleepStats(
            start = "2026-07-19T23:12:00Z",
            end = "2026-07-20T06:45:00Z",
            minutesAsleep = 435,
            stages = SleepStages(deep = 72, light = 210, rem = 95, awake = 18),
            sleepWeek = listOf(420, 480, 360, 510, 450, 400, 435),
            spo2Percent = 97,
            hrvMs = 42,
        ),
        heart = HeartStats(restingHr = 58, minHr = 52, maxHr = 148, zoneLow = 55, zoneMid = 30, zoneHigh = 15),
        workout = WorkoutStats(
            title = "LAST RUN",
            durationSec = 2538,
            calories = 386,
            avgHr = 142,
            peakHr = 168,
            distanceM = 5200,
            start = "2026-07-20T07:32:00Z",
            end = "2026-07-20T08:14:00Z",
            zoneLow = 40,
            zoneMid = 45,
            zoneHigh = 15,
        ),
    )
}
