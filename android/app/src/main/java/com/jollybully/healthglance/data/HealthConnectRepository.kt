package com.jollybully.healthglance.data

import android.content.Context
import android.util.Log
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
import androidx.health.connect.client.records.metadata.DataOrigin
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


    suspend fun loadStats(stepGoal: Int = 10_000): GlanceStats {
        val client = clientOrNull()
            ?: throw IllegalStateException("Health Connect unavailable")

        val now = ZonedDateTime.now(zone)
        val today = LocalDate.now(zone)
        val dayStart = today.atStartOfDay(zone).toInstant()
        val dayEnd = now.toInstant()
        val sleepLookback = now.minusDays(8).toInstant()

        val sleepRecords = readSleepSessions(client, sleepLookback, dayEnd)
        val (writer, sleep) = chooseSleep(sleepRecords, dayEnd)
        Log.i(TAG, "primary writer=${writer ?: "none"}")

        val activityMetrics = setOf(
            StepsRecord.COUNT_TOTAL,
            DistanceRecord.DISTANCE_TOTAL,
            TotalCaloriesBurnedRecord.ENERGY_TOTAL,
            ActiveCaloriesBurnedRecord.ACTIVE_CALORIES_TOTAL,
            ExerciseSessionRecord.EXERCISE_DURATION_TOTAL,
        )
        val activityAgg = aggregateForWriter(
            client,
            activityMetrics,
            dayStart,
            dayEnd,
            writer,
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

        val hrWindowStart = now.minusHours(24).toInstant()
        val rhrWindowStart = today.minusDays(1).atStartOfDay(zone).toInstant()
        /* Min/max and resting HR are aggregates. Paging every sample (30k+) blocked the fresh push. */
        val restingRecord = loadRestingHr(client, rhrWindowStart, dayEnd, writer)
        val (aggMin, aggMax) = loadHrRange(client, hrWindowStart, dayEnd, emptyList(), writer)
        var restingHr = restingRecord ?: 0
        var hrMin = aggMin
        var hrMax = aggMax
        var zoneLow = 0
        var zoneMid = 0
        var zoneHigh = 0
        if (restingHr == 0 || hrMin == 0 || hrMax == 0) {
            val hrSamples = loadHrSamples(client, hrWindowStart, dayEnd, writer)
            if (restingHr == 0) restingHr = estimateRestingHr(hrSamples)
            if (hrMin == 0 && hrMax == 0) {
                val range = hrRange(hrSamples)
                hrMin = range.first
                hrMax = range.second
            }
            val zones = zonePercents(hrSamples)
            zoneLow = zones.first
            zoneMid = zones.second
            zoneHigh = zones.third
            Log.i(TAG, "HR fallback rhr=$restingHr min=$hrMin max=$hrMax samples=${hrSamples.size}")
        } else {
            Log.i(TAG, "HR rhr=$restingHr min=$hrMin max=$hrMax aggregate")
        }

        val stepsWeek = loadStepsWeek(client, today, writer)
        val floors = loadFloors(client, dayStart, dayEnd, writer)

        val sleepWeek = sleepWeekFrom(sleepRecords, today)
        val workout = loadLastWorkout(client, sleepLookback, dayEnd, writer)
        val history = loadWorkoutHistory(client, sleepLookback, dayEnd, writer, today)

        return GlanceStats(
            updatedAt = now.format(isoFormatter),
            source = writer?.substringAfterLast('.') ?: "health-connect",
            activity = ActivityStats(
                steps = steps,
                stepGoal = stepGoal,
                distanceKm = (distanceMeters / 1000.0 * 10.0).roundToInt() / 10.0,
                calories = calories,
                activeMinutes = activeMinutes,
                restingHr = restingHr,
                floors = floors,
                stepsWeek = stepsWeek,
            ),
            sleep = sleep.copy(
                sleepWeek = sleepWeek,
            ),
            heart = HeartStats(
                restingHr = restingHr,
                minHr = hrMin,
                maxHr = hrMax,
                zoneLow = zoneLow,
                zoneMid = zoneMid,
                zoneHigh = zoneHigh,
            ),
            workout = workout.copy(history = history),
        )
    }



    private fun packageName(): String = context.packageName

    private fun originFilter(writer: String?): Set<DataOrigin> =
        if (writer.isNullOrBlank()) emptySet() else setOf(DataOrigin(writer))

    private suspend fun aggregateForWriter(
        client: HealthConnectClient,
        metrics: Set<androidx.health.connect.client.aggregate.AggregateMetric<*>>,
        start: Instant,
        end: Instant,
        writer: String?,
    ): androidx.health.connect.client.aggregate.AggregationResult {
        val range = TimeRangeFilter.between(start, end)
        if (!writer.isNullOrBlank()) {
            try {
                val preferred = client.aggregate(
                    AggregateRequest(
                        metrics = metrics,
                        timeRangeFilter = range,
                        dataOriginFilter = originFilter(writer),
                    ),
                )
                val steps = preferred[StepsRecord.COUNT_TOTAL] ?: 0L
                if (steps > 0L || preferred[TotalCaloriesBurnedRecord.ENERGY_TOTAL] != null) {
                    Log.i(TAG, "using writer=$writer aggregate steps=$steps")
                    return preferred
                }
            } catch (e: Exception) {
                Log.w(TAG, "writer aggregate failed origin=$writer", e)
            }
        }
        Log.i(TAG, "aggregate across all Health Connect writers")
        return client.aggregate(AggregateRequest(metrics = metrics, timeRangeFilter = range))
    }

    private suspend fun loadHrSamples(
        client: HealthConnectClient,
        start: Instant,
        end: Instant,
        writer: String?,
    ): List<Long> {
        return try {
            val out = mutableListOf<Long>()
            var pageToken: String? = null
            val records = mutableListOf<HeartRateRecord>()
            do {
                val response = client.readRecords(
                    ReadRecordsRequest(
                        recordType = HeartRateRecord::class,
                        timeRangeFilter = TimeRangeFilter.between(start, end),
                        pageToken = pageToken,
                    ),
                )
                records += response.records
                pageToken = response.pageToken
            } while (!pageToken.isNullOrEmpty())
            val counts = records.groupingBy { it.metadata.dataOrigin.packageName }
                .fold(0) { acc, rec -> acc + rec.samples.size }
            val hrOrigin = WriterPicker.richestHrOrigin(counts, writer)
            val chosen = WriterPicker.preferOrigin(records, hrOrigin) { it.metadata.dataOrigin.packageName }
            val origins = chosen.map { it.metadata.dataOrigin.packageName }.toSet()
            out += chosen.flatMap { it.samples }.map { it.beatsPerMinute }
            Log.i(TAG, "HR samples=${out.size} origins=$origins records=${chosen.size}/${records.size}")
            out
        } catch (e: Exception) {
            Log.w(TAG, "HR samples failed", e)
            emptyList()
        }
    }

    private suspend fun loadHrRange(
        client: HealthConnectClient,
        start: Instant,
        end: Instant,
        samples: List<Long>,
        writer: String?,
    ): Pair<Int, Int> {
        try {
            val agg = client.aggregate(
                AggregateRequest(
                    metrics = setOf(HeartRateRecord.BPM_MIN, HeartRateRecord.BPM_MAX),
                    timeRangeFilter = TimeRangeFilter.between(start, end),
                    dataOriginFilter = originFilter(writer),
                ),
            )
            val minBpm = agg[HeartRateRecord.BPM_MIN]?.toInt() ?: 0
            val maxBpm = agg[HeartRateRecord.BPM_MAX]?.toInt() ?: 0
            if (minBpm > 0 || maxBpm > 0) return minBpm to maxBpm
        } catch (e: Exception) {
            Log.w(TAG, "HR aggregate failed", e)
        }
        return hrRange(samples)
    }

    private suspend fun loadRestingHr(
        client: HealthConnectClient,
        start: Instant,
        end: Instant,
        writer: String?,
    ): Int? {
        return try {
            val records = WriterPicker.preferOrigin(
                client.readRecords(
                    ReadRecordsRequest(
                        recordType = RestingHeartRateRecord::class,
                        timeRangeFilter = TimeRangeFilter.between(start, end),
                    ),
                ).records,
                writer,
            ) { it.metadata.dataOrigin.packageName }
            val bpm = records.maxByOrNull { it.time }?.beatsPerMinute?.toInt()
            Log.i(
                TAG,
                "RHR records=${records.size} latest=$bpm origins=" +
                    records.map { it.metadata.dataOrigin.packageName }.toSet(),
            )
            bpm
        } catch (e: Exception) {
            Log.w(TAG, "RHR read failed", e)
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
        writer: String?,
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
                        dataOriginFilter = originFilter(writer),
                    ),
                )
                out[i] = (agg[StepsRecord.COUNT_TOTAL] ?: 0L).toInt().coerceAtLeast(0)
            } catch (_: Exception) {
                out[i] = 0
            }
        }
        return out
    }


    private suspend fun loadFloors(
        client: HealthConnectClient,
        start: Instant,
        end: Instant,
        writer: String?,
    ): Int {
        return try {
            val agg = client.aggregate(
                AggregateRequest(
                    metrics = setOf(FloorsClimbedRecord.FLOORS_CLIMBED_TOTAL),
                    timeRangeFilter = TimeRangeFilter.between(start, end),
                    dataOriginFilter = originFilter(writer),
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
        writer: String?,
    ): Int {
        return try {
            val records = WriterPicker.preferOrigin(
                client.readRecords(
                    ReadRecordsRequest(
                        recordType = OxygenSaturationRecord::class,
                        timeRangeFilter = TimeRangeFilter.between(start, end),
                    ),
                ).records,
                writer,
            ) { it.metadata.dataOrigin.packageName }
            records.maxByOrNull { it.time }?.percentage?.value?.roundToInt()?.coerceIn(0, 100) ?: 0
        } catch (_: Exception) {
            0
        }
    }

    private suspend fun loadHrv(
        client: HealthConnectClient,
        start: Instant,
        end: Instant,
        writer: String?,
    ): Int {
        return try {
            val records = WriterPicker.preferOrigin(
                client.readRecords(
                    ReadRecordsRequest(
                        recordType = HeartRateVariabilityRmssdRecord::class,
                        timeRangeFilter = TimeRangeFilter.between(start, end),
                    ),
                ).records,
                writer,
            ) { it.metadata.dataOrigin.packageName }
            records.maxByOrNull { it.time }?.heartRateVariabilityMillis?.roundToInt()?.coerceAtLeast(0) ?: 0
        } catch (_: Exception) {
            0
        }
    }

    private suspend fun readSleepSessions(
        client: HealthConnectClient,
        start: Instant,
        end: Instant,
    ): List<SleepSessionRecord> {
        return try {
            client.readRecords(
                ReadRecordsRequest(
                    recordType = SleepSessionRecord::class,
                    timeRangeFilter = TimeRangeFilter.between(start, end),
                ),
            ).records
        } catch (e: Exception) {
            Log.w(TAG, "sleep read failed", e)
            emptyList()
        }
    }

    private fun nightsByOrigin(
        records: List<SleepSessionRecord>,
        now: Instant,
    ): Map<String, SleepNight> {
        return records.groupBy { it.metadata.dataOrigin.packageName }
            .mapNotNull { (origin, sessions) ->
                val night = SleepNights.primary(
                    SleepNights.cluster(sessions.map { toSlice(it) }),
                    now,
                ) ?: return@mapNotNull null
                origin to night
            }
            .toMap()
    }

    private fun chooseSleep(
        records: List<SleepSessionRecord>,
        now: Instant,
    ): Pair<String?, SleepStats> {
        if (records.isEmpty()) return null to SleepStats()
        for (session in records.sortedBy { it.startTime }) {
            val mins = Duration.between(session.startTime, session.endTime).toMinutes()
            Log.i(
                TAG,
                "sleep session origin=${session.metadata.dataOrigin.packageName} " +
                    "${session.startTime}..${session.endTime} wall=${mins}m stages=${session.stages.size}",
            )
        }
        val picked = WriterPicker.pickSleepWriter(nightsByOrigin(records, now))
            ?: return null to SleepStats()
        val (origin, night) = picked
        Log.i(
            TAG,
            "sleep writer=$origin ${night.start}..${night.end} asleep=${night.asleepMinutes}m " +
                "D${night.deep}/L${night.light}/R${night.rem}/A${night.awake} score=${WriterPicker.sleepScore(night)}",
        )
        return origin to SleepStats(
            start = night.start.atZone(zone).format(isoFormatter),
            end = night.end.atZone(zone).format(isoFormatter),
            minutesAsleep = night.asleepMinutes,
            stages = SleepStages(
                deep = night.deep,
                light = night.light,
                rem = night.rem,
                awake = night.awake,
            ),
        )
    }

    private fun sleepWeekFrom(
        records: List<SleepSessionRecord>,
        today: LocalDate,
    ): List<Int> {
        val out = MutableList(7) { 0 }
        val nights = records.groupBy { it.metadata.dataOrigin.packageName }
            .flatMap { (origin, sessions) ->
                SleepNights.cluster(sessions.map { toSlice(it) }).map { origin to it }
            }
        for (i in 0..6) {
            val wakeDate = today.minusDays((6 - i).toLong())
            out[i] = WriterPicker.pickNightForWakeDate(nights, wakeDate, zone)?.asleepMinutes ?: 0
        }
        return out
    }

    /**
     * Asleep = Light + Deep + REM (+ generic SLEEPING/UNKNOWN), matching Google Fit / Health.
     * Sum stage seconds then convert once — per-stage ChronoUnit.MINUTES truncates and can
     * under-count by ~10–20m over a typical night.
     */
    private fun toSlice(session: SleepSessionRecord): SleepSessionSlice {
        val origin = session.metadata.dataOrigin.packageName
        if (session.stages.isEmpty()) {
            return SleepSessionSlice(session.startTime, session.endTime, origin = origin)
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
        return SleepSessionSlice(
            start = session.startTime,
            end = session.endTime,
            stages = SleepStageSeconds(
                deepSec = deepSec,
                lightSec = lightSec,
                remSec = remSec,
                awakeSec = awakeSec,
                hasStages = true,
            ),
            origin = origin,
        )
    }


    /**
     * Highest bpm in each local hour of today, for the watch's Heart chart.
     * One paged read of today's samples; hours with no samples stay 0 so the
     * watch draws nothing rather than inventing a value.
     */

    /**
     * The night as 24 equal slots, each mapped to the stage covering its midpoint
     * (0 deep, 1 light, 2 rem, 3 awake) — the shape of the watch's sleep chart.
     */

    /**
     * The last seven sessions (>=2 min), newest first, for the Workouts screen.
     * Distance and average HR are per-session lookups, so the count is capped.
     */
    private suspend fun loadWorkoutHistory(
        client: HealthConnectClient,
        start: Instant,
        end: Instant,
        writer: String?,
        today: LocalDate,
    ): List<WorkoutEntry> {
        val sessions = try {
            client.readRecords(
                ReadRecordsRequest(
                    recordType = ExerciseSessionRecord::class,
                    timeRangeFilter = TimeRangeFilter.between(start, end),
                ),
            ).records
        } catch (e: Exception) {
            Log.w(TAG, "workout history read failed", e)
            return emptyList()
        }
        val fromWriter = WriterPicker.preferOrigin(sessions, writer) { it.metadata.dataOrigin.packageName }
        val usable = fromWriter
            .filter { ChronoUnit.SECONDS.between(it.startTime, it.endTime) >= 120 }
            .sortedByDescending { it.endTime }
            .take(7)
        return usable.map { session ->
            val origin = session.metadata.dataOrigin.packageName
            val durSec = ChronoUnit.SECONDS.between(session.startTime, session.endTime)
                .toInt().coerceAtLeast(0)
            val distM = try {
                client.aggregate(
                    AggregateRequest(
                        metrics = setOf(DistanceRecord.DISTANCE_TOTAL),
                        timeRangeFilter = TimeRangeFilter.between(session.startTime, session.endTime),
                        dataOriginFilter = originFilter(origin),
                    ),
                )[DistanceRecord.DISTANCE_TOTAL]?.inMeters?.roundToInt() ?: 0
            } catch (_: Exception) {
                0
            }
            val avgHr = try {
                val hr = client.readRecords(
                    ReadRecordsRequest(
                        recordType = HeartRateRecord::class,
                        timeRangeFilter = TimeRangeFilter.between(session.startTime, session.endTime),
                    ),
                ).records.flatMap { it.samples }.map { it.beatsPerMinute }
                if (hr.isEmpty()) 0 else hr.average().roundToInt()
            } catch (_: Exception) {
                0
            }
            val daysAgo = ChronoUnit.DAYS.between(
                session.startTime.atZone(zone).toLocalDate(),
                today,
            ).toInt().coerceAtLeast(0)
            WorkoutEntry(
                sport = sportCode(session.exerciseType),
                durationSec = durSec,
                distanceM = distM,
                avgHr = avgHr,
                daysAgo = daysAgo,
            )
        }.also {
            Log.i(TAG, "workout history n=${it.size} " + it.joinToString { e -> "${e.sport}/${e.durationSec}s/${e.daysAgo}d" })
        }
    }

    /** Watch's sport table: 0 walk, 1 run, 2 ride, 3 other. */
    private fun sportCode(exerciseType: Int): Int = when (exerciseType) {
        ExerciseSessionRecord.EXERCISE_TYPE_WALKING,
        ExerciseSessionRecord.EXERCISE_TYPE_HIKING,
        -> 0
        ExerciseSessionRecord.EXERCISE_TYPE_RUNNING,
        ExerciseSessionRecord.EXERCISE_TYPE_RUNNING_TREADMILL,
        -> 1
        ExerciseSessionRecord.EXERCISE_TYPE_BIKING,
        ExerciseSessionRecord.EXERCISE_TYPE_BIKING_STATIONARY,
        -> 2
        else -> 3
    }

    private suspend fun loadLastWorkout(
        client: HealthConnectClient,
        start: Instant,
        end: Instant,
        writer: String?,
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
        val fromWriter = WriterPicker.preferOrigin(sessions, writer) { it.metadata.dataOrigin.packageName }
        val substantial = fromWriter.filter {
            ChronoUnit.SECONDS.between(it.startTime, it.endTime) >= 120
        }
        val chosen = substantial.ifEmpty { fromWriter }
        for (s in chosen.sortedByDescending { it.endTime }.take(8)) {
            val dur = ChronoUnit.SECONDS.between(s.startTime, s.endTime)
            Log.i(
                TAG,
                "workout cand origin=${s.metadata.dataOrigin.packageName} type=${s.exerciseType} " +
                    "title=${s.title} ${s.startTime}..${s.endTime} dur=${dur}s",
            )
        }
        val session = chosen.maxByOrNull { it.endTime } ?: return WorkoutStats()
        val durationSec = ChronoUnit.SECONDS.between(session.startTime, session.endTime).toInt().coerceAtLeast(0)
        val title = WorkoutTitles.from(session.title, session.exerciseType)
        Log.i(
            TAG,
            "workout pick type=${session.exerciseType} rawTitle=${session.title} mapped=$title dur=${durationSec}s",
        )

        val sessionOrigin = session.metadata.dataOrigin.packageName
        val distAgg = try {
            client.aggregate(
                AggregateRequest(
                    metrics = setOf(DistanceRecord.DISTANCE_TOTAL),
                    timeRangeFilter = TimeRangeFilter.between(session.startTime, session.endTime),
                    dataOriginFilter = originFilter(sessionOrigin),
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
                    dataOriginFilter = originFilter(sessionOrigin),
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
                    dataOriginFilter = originFilter(sessionOrigin),
                ),
            )[TotalCaloriesBurnedRecord.ENERGY_TOTAL]?.inKilocalories?.roundToInt() ?: 0
        } catch (_: Exception) {
            0
        }
        val calAgg = if (activeCal > 0) activeCal else totalCal

        val hr = loadHrSamples(client, session.startTime, session.endTime, sessionOrigin)
        val avgHr = if (hr.isEmpty()) 0 else (hr.sum().toDouble() / hr.size).roundToInt()
        val peakHr = if (hr.isEmpty()) 0 else hr.max().toInt()
        val (zl, zm, zh) = zonePercents(hr)

        return WorkoutStats(
            title = title,
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

    companion object {
        private const val TAG = "HealthGlance"
    }
}

/** One past day's charts, as the watch's day cycling needs them. */
