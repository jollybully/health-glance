package com.jollybully.healthglance.data

import org.junit.Assert.assertEquals
import org.junit.Test
import java.time.Instant
import java.time.LocalDate
import java.time.ZoneOffset

class WriterPickerTest {
    private val zone = ZoneOffset.UTC

    @Test
    fun pickSleepWriter_prefersStagedRemOverGenericTimer() {
        val fitbit = SleepNights.merge(
            listOf(
                SleepSessionSlice(
                    Instant.parse("2026-10-21T23:23:00Z"),
                    Instant.parse("2026-10-22T08:35:00Z"),
                    SleepStageSeconds(
                        deepSec = 70 * 60,
                        lightSec = 295 * 60,
                        remSec = 105 * 60,
                        awakeSec = 82 * 60,
                        hasStages = true,
                    ),
                    origin = "com.fitbit.FitbitMobile",
                ),
            ),
        )
        val sleepAsAndroid = SleepNights.merge(
            listOf(
                SleepSessionSlice(
                    Instant.parse("2026-10-21T00:00:00Z"),
                    Instant.parse("2026-10-21T09:01:00Z"),
                    SleepStageSeconds(
                        deepSec = 174 * 60,
                        lightSec = 169 * 60,
                        remSec = 0,
                        awakeSec = 196 * 60,
                        hasStages = true,
                    ),
                    origin = "com.urbandroid.sleep",
                ),
            ),
        )
        val picked = WriterPicker.pickSleepWriter(
            mapOf(
                "com.urbandroid.sleep" to sleepAsAndroid,
                "com.fitbit.FitbitMobile" to fitbit,
            ),
        )
        assertEquals("com.fitbit.FitbitMobile", picked!!.first)
        assertEquals(470, picked.second.asleepMinutes)
    }

    @Test
    fun pickSleepWriter_usesOnlyAvailableTracker() {
        val samsung = SleepNights.merge(
            listOf(
                SleepSessionSlice(
                    Instant.parse("2026-10-21T23:00:00Z"),
                    Instant.parse("2026-10-22T07:00:00Z"),
                    SleepStageSeconds(lightSec = 8 * 3600, remSec = 90 * 60, hasStages = true),
                    origin = "com.sec.android.app.shealth",
                ),
            ),
        )
        val picked = WriterPicker.pickSleepWriter(mapOf("com.sec.android.app.shealth" to samsung))
        assertEquals("com.sec.android.app.shealth", picked!!.first)
    }

    @Test
    fun pickNightForWakeDate_canUseDifferentWriterPerDay() {
        val monday = "com.garmin.android.apps.connectmobile" to SleepNights.merge(
            listOf(
                SleepSessionSlice(
                    Instant.parse("2026-10-19T23:00:00Z"),
                    Instant.parse("2026-10-20T07:00:00Z"),
                    SleepStageSeconds(lightSec = 7 * 3600, remSec = 60 * 60, hasStages = true),
                ),
            ),
        )
        val tuesday = "com.urbandroid.sleep" to SleepNights.merge(
            listOf(
                SleepSessionSlice(
                    Instant.parse("2026-10-20T23:00:00Z"),
                    Instant.parse("2026-10-21T07:00:00Z"),
                    SleepStageSeconds(lightSec = 6 * 3600, hasStages = true),
                ),
            ),
        )
        val tue = WriterPicker.pickNightForWakeDate(
            listOf(monday, tuesday),
            LocalDate.parse("2026-10-21"),
            zone,
        )
        assertEquals(360, tue!!.asleepMinutes)
    }

    @Test
    fun preferOrigin_fallsBackWhenWriterHasNoRecords() {
        data class Rec(val pkg: String)
        val recs = listOf(Rec("com.concept2.ergdata"))
        assertEquals(recs, WriterPicker.preferOrigin(recs, "com.fitbit.FitbitMobile") { it.pkg })
        assertEquals(
            listOf(Rec("com.fitbit.FitbitMobile")),
            WriterPicker.preferOrigin(
                listOf(Rec("com.concept2.ergdata"), Rec("com.fitbit.FitbitMobile")),
                "com.fitbit.FitbitMobile",
            ) { it.pkg },
        )
    }
}
