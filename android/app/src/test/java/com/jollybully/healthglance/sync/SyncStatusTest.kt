package com.jollybully.healthglance.sync

import com.jollybully.healthglance.data.ActivityStats
import com.jollybully.healthglance.data.GlanceStats
import com.jollybully.healthglance.data.HealthConnectAvailability
import com.jollybully.healthglance.data.SleepStages
import com.jollybully.healthglance.data.SleepStats
import com.jollybully.healthglance.pebble.SendOutcome
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class SyncStatusTest {
    @Test
    fun mapsCoreAndWatchOutcomes() {
        assertEquals("Need Health Connect permissions", SyncStatus.needPermissions())
        assertEquals("Health Connect unavailable", SyncStatus.fromAvailability(HealthConnectAvailability.Unavailable))
        assertEquals("Core Pebble not reachable", SyncStatus.fromSendOutcome(SendOutcome.NoPebbleApp))
        assertEquals("No connected watch", SyncStatus.fromSendOutcome(SendOutcome.NoConnectedWatches))
        assertEquals(
            "Loaded (watch app closed)",
            SyncStatus.fromSendOutcome(SendOutcome.Ok(emptyMap())),
        )
        assertTrue(SyncStatus.fromSendOutcome(SendOutcome.Failed("boom")).startsWith("Send failed:"))
    }

    @Test
    fun watchErrorsStayShort() {
        val long = "x".repeat(80)
        assertTrue(SyncStatus.watchError(long).length <= 28)
    }
}

class SyncCacheCodecTest {
    @Test
    fun roundTripsStatsJson() {
        val stats = GlanceStats(
            updatedAt = "2026-07-20T12:00:00Z",
            source = "health-connect",
            activity = ActivityStats(steps = 428, stepGoal = 10000, distanceKm = 0.3, calories = 889, activeMinutes = 0, restingHr = 46),
            sleep = SleepStats(
                start = "a",
                end = "b",
                minutesAsleep = 397,
                stages = SleepStages(deep = 73, light = 274, rem = 50, awake = 39),
            ),
        )
        val decoded = SyncCache.decodeStats(SyncCache.encodeStats(stats))
        assertEquals(stats, decoded)
    }
}
