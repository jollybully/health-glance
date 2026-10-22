package com.jollybully.healthglance.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.time.Duration
import java.time.Instant
import java.time.LocalDate
import java.time.ZoneOffset

class SleepNightsTest {
    private val zone = ZoneOffset.UTC

    @Test
    fun cluster_mergesNinetyMinuteCycles() {
        val start = Instant.parse("2026-10-21T23:00:00Z")
        val slices = (0 until 5).map { i ->
            val s = start.plus(Duration.ofMinutes(90L * i))
            SleepSessionSlice(
                start = s,
                end = s.plus(Duration.ofMinutes(90)),
                stages = SleepStageSeconds(lightSec = 90 * 60, hasStages = true),
            )
        }
        val nights = SleepNights.cluster(slices)
        assertEquals(1, nights.size)
        assertEquals(5 * 90, nights[0].asleepMinutes)
        assertEquals(start, nights[0].start)
        assertEquals(start.plus(Duration.ofMinutes(450)), nights[0].end)
    }

    @Test
    fun primary_prefersMostRecentOvernightEvenIfShorter() {
        val older = SleepNights.merge(
            listOf(
                SleepSessionSlice(
                    Instant.parse("2026-10-20T23:31:00Z"),
                    Instant.parse("2026-10-21T08:15:00Z"),
                    SleepStageSeconds(lightSec = 513 * 60, hasStages = true),
                ),
            ),
        )
        val lastNight = SleepNights.merge(
            listOf(
                SleepSessionSlice(
                    Instant.parse("2026-10-21T23:23:00Z"),
                    Instant.parse("2026-10-22T08:35:00Z"),
                    SleepStageSeconds(lightSec = 472 * 60, hasStages = true),
                ),
            ),
        )
        val primary = SleepNights.primary(
            listOf(older, lastNight),
            Instant.parse("2026-10-22T09:46:00Z"),
        )
        assertEquals(472, primary!!.asleepMinutes)
        assertEquals(Instant.parse("2026-10-21T23:23:00Z"), primary.start)
    }

    @Test
    fun primary_prefersOvernightOverLaterNap() {
        val nightStart = Instant.parse("2026-10-21T23:00:00Z")
        val nightEnd = Instant.parse("2026-10-22T07:00:00Z")
        val napStart = Instant.parse("2026-10-22T14:00:00Z")
        val napEnd = Instant.parse("2026-10-22T15:30:00Z")
        val nights = SleepNights.cluster(
            listOf(
                SleepSessionSlice(
                    nightStart,
                    nightEnd,
                    SleepStageSeconds(lightSec = 8 * 3600, hasStages = true),
                ),
                SleepSessionSlice(
                    napStart,
                    napEnd,
                    SleepStageSeconds(lightSec = 90 * 60, hasStages = true),
                ),
            ),
        )
        assertEquals(2, nights.size)
        val primary = SleepNights.primary(nights, Instant.parse("2026-10-22T16:00:00Z"))
        assertEquals(480, primary!!.asleepMinutes)
        assertEquals(nightStart, primary.start)
    }

    @Test
    fun unstagedSession_usesWallClock() {
        val start = Instant.parse("2026-10-22T00:00:00Z")
        val end = Instant.parse("2026-10-22T07:12:00Z")
        val night = SleepNights.merge(listOf(SleepSessionSlice(start, end)))
        assertEquals(7 * 60 + 12, night.asleepMinutes)
        assertEquals(0, night.deep)
        assertEquals(0, night.light)
    }

    @Test
    fun longestByWakeDate_usesClusterNotFragment() {
        val start = Instant.parse("2026-10-21T23:00:00Z")
        val slices = (0 until 3).map { i ->
            val s = start.plus(Duration.ofMinutes(90L * i))
            SleepSessionSlice(s, s.plus(Duration.ofMinutes(90)))
        }
        val byWake = SleepNights.longestByWakeDate(SleepNights.cluster(slices), zone)
        assertEquals(1, byWake.size)
        val wake = LocalDate.parse("2026-10-22")
        assertTrue(byWake.containsKey(wake))
        assertEquals(270, byWake.getValue(wake).asleepMinutes)
    }
}
