package com.jollybully.healthglance.data

/**
 * Pick the most detailed Health Connect writer without hard-coding an app.
 * Wearables that store staged sleep (light/deep/REM) beat generic timers and gym-only apps.
 */
object WriterPicker {
    fun sleepScore(night: SleepNight): Int {
        var score = 0
        if (night.asleepMinutes in 180..900) score += 20
        else if (night.asleepMinutes > 0) score += 5
        val types = listOf(night.deep, night.light, night.rem, night.awake).count { it > 0 }
        score += types * 8
        if (night.rem > 0) score += 15
        if (night.deep > 0) score += 8
        if (night.light > 0) score += 4
        return score
    }

    /** Best last-night among per-origin primaries. */
    fun pickSleepWriter(
        nightsByOrigin: Map<String, SleepNight>,
    ): Pair<String, SleepNight>? {
        if (nightsByOrigin.isEmpty()) return null
        val best = nightsByOrigin.maxWithOrNull(
            compareBy<Map.Entry<String, SleepNight>> { sleepScore(it.value) }
                .thenBy { it.value.end },
        ) ?: return null
        return best.key to best.value
    }

    fun pickNightForWakeDate(
        nights: List<Pair<String, SleepNight>>,
        wake: java.time.LocalDate,
        zone: java.time.ZoneId,
    ): SleepNight? {
        val thatDay = nights.filter { it.second.wakeDate(zone) == wake }
        if (thatDay.isEmpty()) return null
        return thatDay.maxWithOrNull(
            compareBy<Pair<String, SleepNight>> { sleepScore(it.second) }
                .thenBy { it.second.asleepMinutes },
        )?.second
    }

    fun <T> preferOrigin(records: List<T>, origin: String?, packageName: (T) -> String): List<T> {
        if (origin.isNullOrBlank() || records.isEmpty()) return records
        val matched = records.filter { packageName(it) == origin }
        return matched.ifEmpty { records }
    }

    fun richestHrOrigin(
        sampleCounts: Map<String, Int>,
        preferred: String?,
    ): String? {
        if (preferred != null && (sampleCounts[preferred] ?: 0) > 0) return preferred
        return sampleCounts.maxByOrNull { it.value }?.key
    }
}
