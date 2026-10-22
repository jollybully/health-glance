package com.jollybully.healthglance.data

import java.time.Duration
import java.time.Instant
import java.time.LocalDate
import java.time.ZoneId

data class SleepStageSeconds(
    val deepSec: Long = 0,
    val lightSec: Long = 0,
    val remSec: Long = 0,
    val awakeSec: Long = 0,
    val hasStages: Boolean = false,
)

data class SleepSessionSlice(
    val start: Instant,
    val end: Instant,
    val stages: SleepStageSeconds = SleepStageSeconds(),
    val origin: String = "",
)

data class SleepNight(
    val start: Instant,
    val end: Instant,
    val deep: Int,
    val light: Int,
    val rem: Int,
    val awake: Int,
    val asleepMinutes: Int,
) {
    fun wakeDate(zone: ZoneId): LocalDate = end.atZone(zone).toLocalDate()
}

object SleepNights {
    val CLUSTER_GAP: Duration = Duration.ofMinutes(90)
    val PRIMARY_LOOKBACK: Duration = Duration.ofHours(36)

    fun cluster(
        sessions: List<SleepSessionSlice>,
        gap: Duration = CLUSTER_GAP,
    ): List<SleepNight> {
        if (sessions.isEmpty()) return emptyList()
        val sorted = sessions.sortedBy { it.start }
        val nights = mutableListOf<SleepNight>()
        var bucket = mutableListOf(sorted[0])
        for (i in 1 until sorted.size) {
            val session = sorted[i]
            val prevEnd = bucket.maxOf { it.end }
            if (!session.start.isAfter(prevEnd.plus(gap))) {
                bucket.add(session)
            } else {
                nights += merge(bucket)
                bucket = mutableListOf(session)
            }
        }
        nights += merge(bucket)
        return nights
    }

    fun primary(
        nights: List<SleepNight>,
        now: Instant,
        lookback: Duration = PRIMARY_LOOKBACK,
    ): SleepNight? {
        val cutoff = now.minus(lookback)
        val recent = nights.filter { !it.end.isBefore(cutoff) }
        val pool = if (recent.isNotEmpty()) recent else nights
        // Last night, not the longest night in the window (a prior 8h can beat today's 7h).
        val overnight = pool.filter { it.asleepMinutes >= 180 }
        val candidates = overnight.ifEmpty { pool }
        return candidates.maxByOrNull { it.end }
    }

    fun longestByWakeDate(
        nights: List<SleepNight>,
        zone: ZoneId,
    ): Map<LocalDate, SleepNight> {
        val out = mutableMapOf<LocalDate, SleepNight>()
        for (night in nights) {
            val date = night.wakeDate(zone)
            val prev = out[date]
            if (prev == null || night.asleepMinutes > prev.asleepMinutes) {
                out[date] = night
            }
        }
        return out
    }

    fun merge(sessions: List<SleepSessionSlice>): SleepNight {
        val start = sessions.minOf { it.start }
        val end = sessions.maxOf { it.end }
        var deep = 0L
        var light = 0L
        var rem = 0L
        var awake = 0L
        val anyStages = sessions.any { it.stages.hasStages }
        if (anyStages) {
            for (session in sessions) {
                if (session.stages.hasStages) {
                    deep += session.stages.deepSec
                    light += session.stages.lightSec
                    rem += session.stages.remSec
                    awake += session.stages.awakeSec
                } else {
                    light += Duration.between(session.start, session.end).seconds.coerceAtLeast(0)
                }
            }
        }
        val asleepSec = deep + light + rem
        val wallMinutes = Duration.between(start, end).toMinutes().toInt().coerceAtLeast(0)
        val asleepMinutes = if (asleepSec > 0) (asleepSec / 60L).toInt() else wallMinutes
        return SleepNight(
            start = start,
            end = end,
            deep = (deep / 60L).toInt(),
            light = (light / 60L).toInt(),
            rem = (rem / 60L).toInt(),
            awake = (awake / 60L).toInt(),
            asleepMinutes = asleepMinutes,
        )
    }
}
