package com.jollybully.healthglance.sync

import android.content.Context
import com.jollybully.healthglance.data.ActivityStats
import com.jollybully.healthglance.data.GlanceStats
import com.jollybully.healthglance.data.HeartStats
import com.jollybully.healthglance.data.SleepStages
import com.jollybully.healthglance.data.SleepStats
import com.jollybully.healthglance.data.WorkoutStats
import org.json.JSONArray
import org.json.JSONObject

class SyncCache(context: Context) {
    private val prefs = context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    fun save(stats: GlanceStats, status: String) {
        prefs.edit()
            .putString(KEY_STATS_JSON, encodeStats(stats))
            .putString(KEY_STATUS, status)
            .putLong(KEY_SAVED_AT, System.currentTimeMillis())
            .apply()
    }

    fun saveStatus(status: String) {
        prefs.edit()
            .putString(KEY_STATUS, status)
            .putLong(KEY_SAVED_AT, System.currentTimeMillis())
            .apply()
    }

    fun loadStats(): GlanceStats? {
        val raw = prefs.getString(KEY_STATS_JSON, null) ?: return null
        return try {
            decodeStats(raw)
        } catch (_: Exception) {
            null
        }
    }

    fun loadStatus(): String = prefs.getString(KEY_STATUS, "Ready") ?: "Ready"

    /** What the last series push contained and what the watch transport said. */
    fun saveSeriesInfo(what: String, outcome: String) {
        prefs.edit().putString(KEY_SERIES, "$what · $outcome").apply()
    }

    fun seriesInfo(): String = prefs.getString(KEY_SERIES, "") ?: ""

    /** What the last day-cycle request actually returned. */
    fun saveDayInfo(what: String) {
        prefs.edit().putString(KEY_DAY, what).apply()
    }

    fun dayInfo(): String = prefs.getString(KEY_DAY, "") ?: ""

    fun savedAtMillis(): Long = prefs.getLong(KEY_SAVED_AT, 0L)

    companion object {
        private const val PREFS = "health_glance_sync"
        private const val KEY_STATS_JSON = "stats_json"
        private const val KEY_STATUS = "status"
        private const val KEY_SAVED_AT = "saved_at"
        private const val KEY_SERIES = "series_info"
        private const val KEY_DAY = "day_info"

        fun encodeStats(stats: GlanceStats): String {
            return JSONObject()
                .put("updatedAt", stats.updatedAt)
                .put("source", stats.source)
                .put(
                    "activity",
                    JSONObject()
                        .put("steps", stats.activity.steps)
                        .put("stepGoal", stats.activity.stepGoal)
                        .put("distanceKm", stats.activity.distanceKm)
                        .put("calories", stats.activity.calories)
                        .put("activeMinutes", stats.activity.activeMinutes)
                        .put("restingHr", stats.activity.restingHr)
                        .put("floors", stats.activity.floors)
                        .put("stepsWeek", JSONArray(stats.activity.stepsWeek))
                        .put("stepsHourly", JSONArray(stats.activity.stepsHourly)),
                )
                .put(
                    "sleep",
                    JSONObject()
                        .put("start", stats.sleep.start)
                        .put("end", stats.sleep.end)
                        .put("minutesAsleep", stats.sleep.minutesAsleep)
                        .put("spo2Percent", stats.sleep.spo2Percent)
                        .put("hrvMs", stats.sleep.hrvMs)
                        .put("sleepWeek", JSONArray(stats.sleep.sleepWeek))
                        .put(
                            "stages",
                            JSONObject()
                                .put("deep", stats.sleep.stages.deep)
                                .put("light", stats.sleep.stages.light)
                                .put("rem", stats.sleep.stages.rem)
                                .put("awake", stats.sleep.stages.awake),
                        ),
                )
                .put(
                    "heart",
                    JSONObject()
                        .put("restingHr", stats.heart.restingHr)
                        .put("minHr", stats.heart.minHr)
                        .put("maxHr", stats.heart.maxHr)
                        .put("zoneLow", stats.heart.zoneLow)
                        .put("zoneMid", stats.heart.zoneMid)
                        .put("zoneHigh", stats.heart.zoneHigh),
                )
                .put(
                    "workout",
                    JSONObject()
                        .put("title", stats.workout.title)
                        .put("durationSec", stats.workout.durationSec)
                        .put("calories", stats.workout.calories)
                        .put("avgHr", stats.workout.avgHr)
                        .put("peakHr", stats.workout.peakHr)
                        .put("distanceM", stats.workout.distanceM)
                        .put("start", stats.workout.start)
                        .put("end", stats.workout.end)
                        .put("zoneLow", stats.workout.zoneLow)
                        .put("zoneMid", stats.workout.zoneMid)
                        .put("zoneHigh", stats.workout.zoneHigh),
                )
                .toString()
        }

        fun decodeStats(raw: String): GlanceStats {
            val json = JSONObject(raw)
            val activity = json.optJSONObject("activity") ?: JSONObject()
            val sleep = json.optJSONObject("sleep") ?: JSONObject()
            val stages = sleep.optJSONObject("stages") ?: JSONObject()
            val heart = json.optJSONObject("heart") ?: JSONObject()
            val workout = json.optJSONObject("workout") ?: JSONObject()
            return GlanceStats(
                updatedAt = json.optString("updatedAt"),
                source = json.optString("source", "health-connect"),
                activity = ActivityStats(
                    steps = activity.optLong("steps"),
                    stepGoal = activity.optInt("stepGoal", 10_000),
                    distanceKm = activity.optDouble("distanceKm"),
                    calories = activity.optLong("calories"),
                    activeMinutes = activity.optInt("activeMinutes"),
                    restingHr = activity.optInt("restingHr"),
                    floors = activity.optInt("floors"),
                    stepsWeek = intList(activity.optJSONArray("stepsWeek"), 7),
                    stepsHourly = intList(activity.optJSONArray("stepsHourly"), 24),
                ),
                sleep = SleepStats(
                    start = sleep.optString("start"),
                    end = sleep.optString("end"),
                    minutesAsleep = sleep.optInt("minutesAsleep"),
                    spo2Percent = sleep.optInt("spo2Percent"),
                    hrvMs = sleep.optInt("hrvMs"),
                    sleepWeek = intList(sleep.optJSONArray("sleepWeek"), 7),
                    stages = SleepStages(
                        deep = stages.optInt("deep"),
                        light = stages.optInt("light"),
                        rem = stages.optInt("rem"),
                        awake = stages.optInt("awake"),
                    ),
                ),
                heart = HeartStats(
                    restingHr = heart.optInt("restingHr", activity.optInt("restingHr")),
                    minHr = heart.optInt("minHr"),
                    maxHr = heart.optInt("maxHr"),
                    zoneLow = heart.optInt("zoneLow"),
                    zoneMid = heart.optInt("zoneMid"),
                    zoneHigh = heart.optInt("zoneHigh"),
                ),
                workout = WorkoutStats(
                    title = workout.optString("title"),
                    durationSec = workout.optInt("durationSec"),
                    calories = workout.optInt("calories"),
                    avgHr = workout.optInt("avgHr"),
                    peakHr = workout.optInt("peakHr"),
                    distanceM = workout.optInt("distanceM"),
                    start = workout.optString("start"),
                    end = workout.optString("end"),
                    zoneLow = workout.optInt("zoneLow"),
                    zoneMid = workout.optInt("zoneMid"),
                    zoneHigh = workout.optInt("zoneHigh"),
                ),
            )
        }

        private fun intList(arr: JSONArray?, size: Int): List<Int> {
            val out = MutableList(size) { 0 }
            if (arr == null) return out
            val n = minOf(size, arr.length())
            for (i in 0 until n) out[i] = arr.optInt(i)
            return out
        }
    }
}
