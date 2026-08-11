package com.elliotseeds.healthglance.data

import android.content.Context
import androidx.health.connect.client.HealthConnectClient
import androidx.health.connect.client.records.ActiveCaloriesBurnedRecord
import androidx.health.connect.client.records.DistanceRecord
import androidx.health.connect.client.records.ExerciseSessionRecord
import androidx.health.connect.client.records.FloorsClimbedRecord
import androidx.health.connect.client.records.HeartRateRecord
import androidx.health.connect.client.records.HeartRateVariabilityRmssdRecord
import androidx.health.connect.client.records.OxygenSaturationRecord
import androidx.health.connect.client.records.RestingHeartRateRecord
import androidx.health.connect.client.records.SleepSessionRecord
import androidx.health.connect.client.records.StepsRecord
import androidx.health.connect.client.records.TotalCaloriesBurnedRecord
import androidx.health.connect.client.records.metadata.Metadata
import androidx.health.connect.client.request.AggregateGroupByDurationRequest
import androidx.health.connect.client.request.AggregateRequest
import androidx.health.connect.client.request.ReadRecordsRequest
import androidx.health.connect.client.time.TimeRangeFilter
import androidx.health.connect.client.units.Energy
import androidx.health.connect.client.units.Length
import androidx.health.connect.client.units.Percentage
import java.time.Duration
import java.time.Instant
import java.time.LocalDate
import java.time.ZoneId
import java.time.ZonedDateTime
import java.time.format.DateTimeFormatter
import java.time.temporal.ChronoUnit
import kotlin.math.roundToInt
import kotlin.math.roundToLong

sealed class HealthConnectAvailability {
    data object Available : HealthConnectAvailability()
    data object UpdateRequired : HealthConnectAvailability()
    data object Unavailable : HealthConnectAvailability()
}

class HealthConnectRepository(private val context: Context) {
    private val zone: ZoneId = ZoneId.systemDefault()
    private val isoFormatter: DateTimeFormatter = DateTimeFormatter.ISO_OFFSET_DATE_TIME

    fun availability(): HealthConnectAvailability {
        return when (HealthConnectClient.getSdkStatus(context)) {
            HealthConnectClient.SDK_AVAILABLE -> HealthConnectAvailability.Available
            HealthConnectClient.SDK_UNAVAILABLE_PROVIDER_UPDATE_REQUIRED ->
                HealthConnectAvailability.UpdateRequired
            else -> HealthConnectAvailability.Unavailable
        }
    }

    fun clientOrNull(): HealthConnectClient? {
        return if (availability() is HealthConnectAvailability.Available) {
            HealthConnectClient.getOrCreate(context)
        } else {
            null
        }
    }

    suspend fun hasReadPermissions(): Boolean {
        val client = clientOrNull() ?: return false
        val granted = client.permissionController.getGrantedPermissions()
        return granted.containsAll(HealthPermissions.coreReadPermissions)
    }

    suspend fun hasWritePermissions(): Boolean {
        val client = clientOrNull() ?: return false
        val granted = client.permissionController.getGrantedPermissions()
        return granted.containsAll(HealthPermissions.writePermissions)
    }

    suspend fun loadStats(stepGoal: Int = 10_000): GlanceStats {
        val client = clientOrNull()
            ?: throw IllegalStateException("Health Connect unavailable")

        val now = ZonedDateTime.now(zone)
        val today = LocalDate.now(zone)
        val dayStart = today.atStartOfDay(zone).toInstant()
        val dayEnd = now.toInstant()
        val weekStart = today.minusDays(6).atStartOfDay(zone).toInstant()
        val sleepLookback = now.minusDays(8).toInstant()

        val activityAgg = client.aggregate(
            AggregateRequest(
                metrics = setOf(
                    StepsRecord.COUNT_TOTAL,
                    DistanceRecord.DISTANCE_TOTAL,
                    TotalCaloriesBurnedRecord.ENERGY_TOTAL,
                    ActiveCaloriesBurnedRecord.ACTIVE_CALORIES_TOTAL,
                    ExerciseSessionRecord.EXERCISE_DURATION_TOTAL,
                ),
                timeRangeFilter = TimeRangeFilter.between(dayStart, dayEnd),
            ),
        )

        val steps = activityAgg[StepsRecord.COUNT_TOTAL] ?: 0L
        val distanceMeters = activityAgg[DistanceRecord.DISTANCE_TOTAL]?.inMeters ?: 0.0
        val totalCalories = activityAgg[TotalCaloriesBurnedRecord.ENERGY_TOTAL]
            ?.inKilocalories
            ?.roundToLong()
            ?: 0L
        val activeCalories = activityAgg[ActiveCaloriesBurnedRecord.ACTIVE_CALORIES_TOTAL]
            ?.inKilocalories
            ?.roundToLong()
            ?: 0L
        val calories = if (totalCalories > 0) totalCalories else activeCalories
        val activeMinutes = ((activityAgg[ExerciseSessionRecord.EXERCISE_DURATION_TOTAL]
            ?.toMinutes()
            ?: 0L)).toInt().coerceAtLeast(0)

        val hrSamples = loadHrSamples(client, dayStart, dayEnd)
        val restingHr = loadRestingHr(client, dayStart, dayEnd) ?: estimateRestingHr(hrSamples)
        val (hrMin, hrMax) = hrRange(hrSamples)
        val (zoneLow, zoneMid, zoneHigh) = zonePercents(hrSamples)

        val stepsWeek = loadStepsWeek(client, today, stepGoal)
        val stepsHourly = loadHourlySteps(client, dayStart, dayEnd)
        val floors = loadFloors(client, dayStart, dayEnd)

        val sleep = loadLatestSleep(client, sleepLookback, dayEnd)
        val sleepWeek = loadSleepWeek(client, today)
        val spo2 = loadSpo2(client, sleepLookback, dayEnd)
        val hrv = loadHrv(client, sleepLookback, dayEnd)
        val workout = loadLastWorkout(client, sleepLookback, dayEnd)

        return GlanceStats(
            updatedAt = now.format(isoFormatter),
            source = "health-connect",
            activity = ActivityStats(
                steps = steps,
                stepGoal = stepGoal,
                distanceKm = (distanceMeters / 1000.0 * 10.0).roundToInt() / 10.0,
                calories = calories,
                activeMinutes = activeMinutes,
                restingHr = restingHr,
                floors = floors,
                stepsWeek = stepsWeek,
                stepsHourly = stepsHourly,
            ),
            sleep = sleep.copy(
                sleepWeek = sleepWeek,
                spo2Percent = spo2,
                hrvMs = hrv,
            ),
            heart = HeartStats(
                restingHr = restingHr,
                minHr = hrMin,
                maxHr = hrMax,
                zoneLow = zoneLow,
                zoneMid = zoneMid,
                zoneHigh = zoneHigh,
            ),
            workout = workout,
        )
    }

    suspend fun seedSampleData() {
        val client = clientOrNull()
            ?: throw IllegalStateException("Health Connect unavailable")

        val now = Instant.now()
        val today = LocalDate.now(zone)
        val dayStart = today.atStartOfDay(zone).toInstant()
        val meta = Metadata.manualEntry()
        val records = mutableListOf<androidx.health.connect.client.records.Record>()

        // Multi-day steps (oldest → yesterday). Today comes from hourly buckets below.
        val weekCounts = listOf(5500, 7200, 4000, 8800, 6500, 9500, 7611)
        for (dayOffset in 0..5) {
            val date = today.minusDays((6 - dayOffset).toLong())
            val start = date.atStartOfDay(zone).toInstant().plus(8, ChronoUnit.HOURS)
            val end = start.plus(6, ChronoUnit.HOURS)
            records += StepsRecord(
                startTime = start,
                endTime = end,
                startZoneOffset = zone.rules.getOffset(start),
                endZoneOffset = zone.rules.getOffset(end),
                count = weekCounts[dayOffset].toLong(),
                metadata = meta,
            )
        }

        // Hourly-ish samples today for histogram (overwrite-ish via separate windows)
        val hourly = listOf(
            0, 0, 0, 0, 0, 0, 80, 180, 320, 520, 780, 1240,
            960, 680, 860, 600, 420, 500, 720, 380, 260, 120, 40, 0,
        )
        for (h in hourly.indices) {
            val count = hourly[h]
            if (count <= 0) continue
            val start = dayStart.plus(h.toLong(), ChronoUnit.HOURS)
            val end = start.plus(50, ChronoUnit.MINUTES)
            if (end.isAfter(now)) continue
            records += StepsRecord(
                startTime = start,
                endTime = end,
                startZoneOffset = zone.rules.getOffset(start),
                endZoneOffset = zone.rules.getOffset(end),
                count = count.toLong(),
                metadata = meta,
            )
        }

        val midMorning = dayStart.plus(3, ChronoUnit.HOURS)
        val afternoon = dayStart.plus(8, ChronoUnit.HOURS)
        records += DistanceRecord(
            startTime = midMorning,
            endTime = afternoon,
            startZoneOffset = zone.rules.getOffset(midMorning),
            endZoneOffset = zone.rules.getOffset(afternoon),
            distance = Length.kilometers(6.5),
            metadata = meta,
        )
        records += TotalCaloriesBurnedRecord(
            startTime = dayStart,
            endTime = now,
            startZoneOffset = zone.rules.getOffset(dayStart),
            endZoneOffset = zone.rules.getOffset(now),
            energy = Energy.kilocalories(2345.0),
            metadata = meta,
        )
        records += ActiveCaloriesBurnedRecord(
            startTime = midMorning,
            endTime = afternoon,
            startZoneOffset = zone.rules.getOffset(midMorning),
            endZoneOffset = zone.rules.getOffset(afternoon),
            energy = Energy.kilocalories(520.0),
            metadata = meta,
        )
        records += FloorsClimbedRecord(
            startTime = midMorning,
            endTime = afternoon,
            startZoneOffset = zone.rules.getOffset(midMorning),
            endZoneOffset = zone.rules.getOffset(afternoon),
            floors = 14.0,
            metadata = meta,
        )

        // Broad HR range for today + resting samples
        val hrSamples = mutableListOf(
            HeartRateRecord.Sample(dayStart.plus(1, ChronoUnit.HOURS), 52),
            HeartRateRecord.Sample(dayStart.plus(2, ChronoUnit.HOURS), 58),
            HeartRateRecord.Sample(dayStart.plus(4, ChronoUnit.HOURS), 72),
            HeartRateRecord.Sample(dayStart.plus(10, ChronoUnit.HOURS), 95),
            HeartRateRecord.Sample(dayStart.plus(12, ChronoUnit.HOURS), 110),
            HeartRateRecord.Sample(dayStart.plus(14, ChronoUnit.HOURS), 88),
            HeartRateRecord.Sample(dayStart.plus(18, ChronoUnit.HOURS), 64),
        )
        val runStart = dayStart.plus(7, ChronoUnit.HOURS).plus(32, ChronoUnit.MINUTES)
        val runEnd = runStart.plus(42, ChronoUnit.MINUTES).plus(18, ChronoUnit.SECONDS)
        hrSamples += listOf(
            HeartRateRecord.Sample(runStart.plus(5, ChronoUnit.MINUTES), 128),
            HeartRateRecord.Sample(runStart.plus(15, ChronoUnit.MINUTES), 142),
            HeartRateRecord.Sample(runStart.plus(25, ChronoUnit.MINUTES), 155),
            HeartRateRecord.Sample(runStart.plus(35, ChronoUnit.MINUTES), 168),
            HeartRateRecord.Sample(runStart.plus(40, ChronoUnit.MINUTES), 148),
        )
        records += HeartRateRecord(
            startTime = dayStart.plus(1, ChronoUnit.HOURS),
            endTime = dayStart.plus(18, ChronoUnit.HOURS),
            startZoneOffset = zone.rules.getOffset(dayStart),
            endZoneOffset = zone.rules.getOffset(dayStart.plus(18, ChronoUnit.HOURS)),
            samples = hrSamples,
            metadata = meta,
        )

        records += ExerciseSessionRecord(
            startTime = runStart,
            endTime = runEnd,
            startZoneOffset = zone.rules.getOffset(runStart),
            endZoneOffset = zone.rules.getOffset(runEnd),
            exerciseType = ExerciseSessionRecord.EXERCISE_TYPE_RUNNING,
            title = "Morning run",
            metadata = meta,
        )
        records += ActiveCaloriesBurnedRecord(
            startTime = runStart,
            endTime = runEnd,
            startZoneOffset = zone.rules.getOffset(runStart),
            endZoneOffset = zone.rules.getOffset(runEnd),
            energy = Energy.kilocalories(386.0),
            metadata = meta,
        )
        records += DistanceRecord(
            startTime = runStart,
            endTime = runEnd,
            startZoneOffset = zone.rules.getOffset(runStart),
            endZoneOffset = zone.rules.getOffset(runEnd),
            distance = Length.kilometers(5.2),
            metadata = meta,
        )

        // 7 nights of sleep
        val nightMins = listOf(420, 480, 360, 510, 450, 400, 435)
        for (dayOffset in 0..6) {
            val wakeDate = today.minusDays((6 - dayOffset).toLong())
            val sleepEnd = wakeDate.atStartOfDay(zone).toInstant().plus(30, ChronoUnit.MINUTES)
            val asleep = nightMins[dayOffset]
            val sleepStart = sleepEnd.minus(asleep.toLong() + 18L, ChronoUnit.MINUTES)
            val stages = if (dayOffset == 6) {
                listOf(
                    SleepSessionRecord.Stage(sleepStart, sleepStart.plus(18, ChronoUnit.MINUTES), SleepSessionRecord.STAGE_TYPE_AWAKE),
                    SleepSessionRecord.Stage(sleepStart.plus(18, ChronoUnit.MINUTES), sleepStart.plus(90, ChronoUnit.MINUTES), SleepSessionRecord.STAGE_TYPE_LIGHT),
                    SleepSessionRecord.Stage(sleepStart.plus(90, ChronoUnit.MINUTES), sleepStart.plus(162, ChronoUnit.MINUTES), SleepSessionRecord.STAGE_TYPE_DEEP),
                    SleepSessionRecord.Stage(sleepStart.plus(162, ChronoUnit.MINUTES), sleepStart.plus(257, ChronoUnit.MINUTES), SleepSessionRecord.STAGE_TYPE_REM),
                    SleepSessionRecord.Stage(sleepStart.plus(257, ChronoUnit.MINUTES), sleepEnd.minus(18, ChronoUnit.MINUTES), SleepSessionRecord.STAGE_TYPE_LIGHT),
                    SleepSessionRecord.Stage(sleepEnd.minus(18, ChronoUnit.MINUTES), sleepEnd, SleepSessionRecord.STAGE_TYPE_AWAKE),
                )
            } else {
                emptyList()
            }
            records += SleepSessionRecord(
                startTime = sleepStart,
                endTime = sleepEnd,
                startZoneOffset = zone.rules.getOffset(sleepStart),
                endZoneOffset = zone.rules.getOffset(sleepEnd),
                stages = stages,
                metadata = meta,
            )
        }

        val vitalsAt = dayStart.plus(1, ChronoUnit.HOURS)
        records += OxygenSaturationRecord(
            time = vitalsAt,
            zoneOffset = zone.rules.getOffset(vitalsAt),
            percentage = Percentage(97.0),
            metadata = meta,
        )
        records += HeartRateVariabilityRmssdRecord(
            time = vitalsAt,
            zoneOffset = zone.rules.getOffset(vitalsAt),
            heartRateVariabilityMillis = 42.0,
            metadata = meta,
        )

        client.insertRecords(records)
    }

    private suspend fun loadHrSamples(
        client: HealthConnectClient,
        start: Instant,
        end: Instant,
    ): List<Long> {
        return try {
            client.readRecords(
                ReadRecordsRequest(
                    recordType = HeartRateRecord::class,
                    timeRangeFilter = TimeRangeFilter.between(start, end),
                ),
            ).records.flatMap { it.samples }.map { it.beatsPerMinute }
        } catch (_: Exception) {
            emptyList()
        }
    }

    private suspend fun loadRestingHr(
        client: HealthConnectClient,
        start: Instant,
        end: Instant,
    ): Int? {
        return try {
            val records = client.readRecords(
                ReadRecordsRequest(
                    recordType = RestingHeartRateRecord::class,
                    timeRangeFilter = TimeRangeFilter.between(start, end),
                ),
            ).records
            records.maxByOrNull { it.time }?.beatsPerMinute?.toInt()
        } catch (_: Exception) {
            null
        }
    }

    private fun estimateRestingHr(bpm: List<Long>): Int {
        val filtered = bpm.filter { it in 35..110 }
        if (filtered.isEmpty()) return 0
        return filtered.sorted()[filtered.size / 10].toInt()
    }

    private fun hrRange(bpm: List<Long>): Pair<Int, Int> {
        if (bpm.isEmpty()) return 0 to 0
        return bpm.min().toInt() to bpm.max().toInt()
    }

    /** Fixed bpm bins: low <100, mid 100–140, high >140. */
    private fun zonePercents(bpm: List<Long>): Triple<Int, Int, Int> {
        if (bpm.isEmpty()) return Triple(0, 0, 0)
        var low = 0
        var mid = 0
        var high = 0
        for (v in bpm) {
            when {
                v < 100 -> low++
                v <= 140 -> mid++
                else -> high++
            }
        }
        val total = bpm.size.toDouble()
        val l = ((low / total) * 100).roundToInt()
        val m = ((mid / total) * 100).roundToInt()
        val h = (100 - l - m).coerceAtLeast(0)
        return Triple(l, m, h)
    }

    private suspend fun loadStepsWeek(
        client: HealthConnectClient,
        today: LocalDate,
        @Suppress("UNUSED_PARAMETER") stepGoal: Int,
    ): List<Int> {
        val out = MutableList(7) { 0 }
        for (i in 0..6) {
            val date = today.minusDays((6 - i).toLong())
            val start = date.atStartOfDay(zone).toInstant()
            val end = if (i == 6) Instant.now() else date.plusDays(1).atStartOfDay(zone).toInstant()
            try {
                val agg = client.aggregate(
                    AggregateRequest(
                        metrics = setOf(StepsRecord.COUNT_TOTAL),
                        timeRangeFilter = TimeRangeFilter.between(start, end),
                    ),
                )
                out[i] = (agg[StepsRecord.COUNT_TOTAL] ?: 0L).toInt().coerceAtLeast(0)
            } catch (_: Exception) {
                out[i] = 0
            }
        }
        return out
    }

    private suspend fun loadHourlySteps(
        client: HealthConnectClient,
        dayStart: Instant,
        dayEnd: Instant,
    ): List<Int> {
        val out = MutableList(24) { 0 }
        try {
            val groups = client.aggregateGroupByDuration(
                AggregateGroupByDurationRequest(
                    metrics = setOf(StepsRecord.COUNT_TOTAL),
                    timeRangeFilter = TimeRangeFilter.between(dayStart, dayEnd),
                    timeRangeSlicer = Duration.ofHours(1),
                ),
            )
            for (group in groups) {
                val hour = group.startTime.atZone(zone).hour
                if (hour in 0..23) {
                    out[hour] = (group.result[StepsRecord.COUNT_TOTAL] ?: 0L).toInt().coerceAtLeast(0)
                }
            }
        } catch (_: Exception) {
            // leave zeros
        }
        return out
    }

    private suspend fun loadFloors(
        client: HealthConnectClient,
        start: Instant,
        end: Instant,
    ): Int {
        return try {
            val agg = client.aggregate(
                AggregateRequest(
                    metrics = setOf(FloorsClimbedRecord.FLOORS_CLIMBED_TOTAL),
                    timeRangeFilter = TimeRangeFilter.between(start, end),
                ),
            )
            (agg[FloorsClimbedRecord.FLOORS_CLIMBED_TOTAL] ?: 0.0).roundToInt().coerceAtLeast(0)
        } catch (_: Exception) {
            0
        }
    }

    private suspend fun loadSpo2(
        client: HealthConnectClient,
        start: Instant,
        end: Instant,
    ): Int {
        return try {
            val records = client.readRecords(
                ReadRecordsRequest(
                    recordType = OxygenSaturationRecord::class,
                    timeRangeFilter = TimeRangeFilter.between(start, end),
                ),
            ).records
            records.maxByOrNull { it.time }?.percentage?.value?.roundToInt()?.coerceIn(0, 100) ?: 0
        } catch (_: Exception) {
            0
        }
    }

    private suspend fun loadHrv(
        client: HealthConnectClient,
        start: Instant,
        end: Instant,
    ): Int {
        return try {
            val records = client.readRecords(
                ReadRecordsRequest(
                    recordType = HeartRateVariabilityRmssdRecord::class,
                    timeRangeFilter = TimeRangeFilter.between(start, end),
                ),
            ).records
            records.maxByOrNull { it.time }?.heartRateVariabilityMillis?.roundToInt()?.coerceAtLeast(0) ?: 0
        } catch (_: Exception) {
            0
        }
    }

    private suspend fun loadSleepWeek(
        client: HealthConnectClient,
        today: LocalDate,
    ): List<Int> {
        val out = MutableList(7) { 0 }
        val rangeStart = today.minusDays(7).atStartOfDay(zone).toInstant()
        val rangeEnd = today.plusDays(1).atStartOfDay(zone).toInstant()
        val sessions = try {
            client.readRecords(
                ReadRecordsRequest(
                    recordType = SleepSessionRecord::class,
                    timeRangeFilter = TimeRangeFilter.between(rangeStart, rangeEnd),
                ),
            ).records
        } catch (_: Exception) {
            return out
        }

        // Bucket by wake date (session end local date), keep longest per night
        val byWake = mutableMapOf<LocalDate, Int>()
        for (session in sessions) {
            val wake = session.endTime.atZone(zone).toLocalDate()
            val mins = asleepMinutes(session)
            byWake[wake] = maxOf(byWake[wake] ?: 0, mins)
        }
        for (i in 0..6) {
            val wakeDate = today.minusDays((6 - i).toLong())
            out[i] = byWake[wakeDate] ?: 0
        }
        return out
    }

    /**
     * Asleep = Light + Deep + REM (+ generic SLEEPING/UNKNOWN), matching Google Fit / Health.
     * Sum stage seconds then convert once — per-stage ChronoUnit.MINUTES truncates and can
     * under-count by ~10–20m over a typical night.
     */
    private fun asleepMinutes(session: SleepSessionRecord): Int =
        sleepStageBreakdown(session).asleepMinutes

    private data class SleepStageBreakdown(
        val deep: Int,
        val light: Int,
        val rem: Int,
        val awake: Int,
        val asleepMinutes: Int,
    )

    private fun sleepStageBreakdown(session: SleepSessionRecord): SleepStageBreakdown {
        if (session.stages.isEmpty()) {
            val mins = Duration.between(session.startTime, session.endTime).toMinutes()
                .toInt()
                .coerceAtLeast(0)
            return SleepStageBreakdown(deep = 0, light = 0, rem = 0, awake = 0, asleepMinutes = mins)
        }

        var deepSec = 0L
        var lightSec = 0L
        var remSec = 0L
        var awakeSec = 0L
        for (stage in session.stages) {
            val secs = Duration.between(stage.startTime, stage.endTime).seconds.coerceAtLeast(0)
            when (stage.stage) {
                SleepSessionRecord.STAGE_TYPE_DEEP -> deepSec += secs
                SleepSessionRecord.STAGE_TYPE_LIGHT,
                SleepSessionRecord.STAGE_TYPE_SLEEPING,
                SleepSessionRecord.STAGE_TYPE_UNKNOWN,
                -> lightSec += secs
                SleepSessionRecord.STAGE_TYPE_REM -> remSec += secs
                SleepSessionRecord.STAGE_TYPE_AWAKE,
                SleepSessionRecord.STAGE_TYPE_OUT_OF_BED,
                SleepSessionRecord.STAGE_TYPE_AWAKE_IN_BED,
                -> awakeSec += secs
            }
        }

        val asleepSec = deepSec + lightSec + remSec
        val asleepMinutes = if (asleepSec > 0) {
            (asleepSec / 60L).toInt()
        } else {
            Duration.between(session.startTime, session.endTime).toMinutes().toInt().coerceAtLeast(0)
        }

        return SleepStageBreakdown(
            deep = (deepSec / 60L).toInt(),
            light = (lightSec / 60L).toInt(),
            rem = (remSec / 60L).toInt(),
            awake = (awakeSec / 60L).toInt(),
            asleepMinutes = asleepMinutes,
        )
    }

    private suspend fun loadLatestSleep(
        client: HealthConnectClient,
        start: Instant,
        end: Instant,
    ): SleepStats {
        val response = client.readRecords(
            ReadRecordsRequest(
                recordType = SleepSessionRecord::class,
                timeRangeFilter = TimeRangeFilter.between(start, end),
            ),
        )
        if (response.records.isEmpty()) return SleepStats()

        // Primary sleep = longest asleep session on the most recent wake date (Fit-style).
        val latestWake = response.records.maxOf { it.endTime.atZone(zone).toLocalDate() }
        val session = response.records
            .filter { it.endTime.atZone(zone).toLocalDate() == latestWake }
            .maxByOrNull { asleepMinutes(it) }
            ?: return SleepStats()

        val breakdown = sleepStageBreakdown(session)

        return SleepStats(
            start = session.startTime.atZone(zone).format(isoFormatter),
            end = session.endTime.atZone(zone).format(isoFormatter),
            minutesAsleep = breakdown.asleepMinutes,
            stages = SleepStages(
                deep = breakdown.deep,
                light = breakdown.light,
                rem = breakdown.rem,
                awake = breakdown.awake,
            ),
        )
    }

    private suspend fun loadLastWorkout(
        client: HealthConnectClient,
        start: Instant,
        end: Instant,
    ): WorkoutStats {
        val sessions = try {
            client.readRecords(
                ReadRecordsRequest(
                    recordType = ExerciseSessionRecord::class,
                    timeRangeFilter = TimeRangeFilter.between(start, end),
                ),
            ).records
        } catch (_: Exception) {
            return WorkoutStats()
        }
        val session = sessions.maxByOrNull { it.endTime } ?: return WorkoutStats()
        val durationSec = ChronoUnit.SECONDS.between(session.startTime, session.endTime).toInt().coerceAtLeast(0)

        val distAgg = try {
            client.aggregate(
                AggregateRequest(
                    metrics = setOf(DistanceRecord.DISTANCE_TOTAL),
                    timeRangeFilter = TimeRangeFilter.between(session.startTime, session.endTime),
                ),
            )[DistanceRecord.DISTANCE_TOTAL]?.inMeters?.roundToInt() ?: 0
        } catch (_: Exception) {
            0
        }

        val activeCal = try {
            client.aggregate(
                AggregateRequest(
                    metrics = setOf(ActiveCaloriesBurnedRecord.ACTIVE_CALORIES_TOTAL),
                    timeRangeFilter = TimeRangeFilter.between(session.startTime, session.endTime),
                ),
            )[ActiveCaloriesBurnedRecord.ACTIVE_CALORIES_TOTAL]?.inKilocalories?.roundToInt() ?: 0
        } catch (_: Exception) {
            0
        }
        val totalCal = try {
            client.aggregate(
                AggregateRequest(
                    metrics = setOf(TotalCaloriesBurnedRecord.ENERGY_TOTAL),
                    timeRangeFilter = TimeRangeFilter.between(session.startTime, session.endTime),
                ),
            )[TotalCaloriesBurnedRecord.ENERGY_TOTAL]?.inKilocalories?.roundToInt() ?: 0
        } catch (_: Exception) {
            0
        }
        val calAgg = if (activeCal > 0) activeCal else totalCal

        val hr = loadHrSamples(client, session.startTime, session.endTime)
        val avgHr = if (hr.isEmpty()) 0 else (hr.sum().toDouble() / hr.size).roundToInt()
        val peakHr = if (hr.isEmpty()) 0 else hr.max().toInt()
        val (zl, zm, zh) = zonePercents(hr)

        return WorkoutStats(
            title = workoutTitle(session),
            durationSec = durationSec,
            calories = calAgg.coerceAtLeast(0),
            avgHr = avgHr,
            peakHr = peakHr,
            distanceM = distAgg.coerceAtLeast(0),
            start = session.startTime.atZone(zone).format(isoFormatter),
            end = session.endTime.atZone(zone).format(isoFormatter),
            zoneLow = zl,
            zoneMid = zm,
            zoneHigh = zh,
        )
    }

    private fun workoutTitle(session: ExerciseSessionRecord): String {
        val named = session.title?.trim().orEmpty()
        if (named.isNotEmpty()) return named.take(16).uppercase()
        return when (session.exerciseType) {
            ExerciseSessionRecord.EXERCISE_TYPE_RUNNING,
            ExerciseSessionRecord.EXERCISE_TYPE_RUNNING_TREADMILL,
            -> "LAST RUN"
            ExerciseSessionRecord.EXERCISE_TYPE_WALKING -> "LAST WALK"
            ExerciseSessionRecord.EXERCISE_TYPE_BIKING,
            ExerciseSessionRecord.EXERCISE_TYPE_BIKING_STATIONARY,
            -> "LAST RIDE"
            ExerciseSessionRecord.EXERCISE_TYPE_SWIMMING_OPEN_WATER,
            ExerciseSessionRecord.EXERCISE_TYPE_SWIMMING_POOL,
            -> "LAST SWIM"
            else -> "WORKOUT"
        }
    }
}
