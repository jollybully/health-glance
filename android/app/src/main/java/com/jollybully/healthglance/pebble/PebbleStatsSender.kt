package com.jollybully.healthglance.pebble

import android.content.Context
import android.util.Log
import com.jollybully.healthglance.ble.LiveHr
import com.jollybully.healthglance.data.GlanceStats
import io.rebble.pebblekit2.client.DefaultPebbleSender
import io.rebble.pebblekit2.common.model.PebbleDictionary
import io.rebble.pebblekit2.common.model.PebbleDictionaryItem
import io.rebble.pebblekit2.common.model.TransmissionResult
import io.rebble.pebblekit2.common.model.WatchIdentifier
import kotlin.math.roundToInt

interface PebbleStatsSender {
    suspend fun sendStats(stats: GlanceStats, watches: List<WatchIdentifier>? = null): SendOutcome
    suspend fun sendRootStats(stats: GlanceStats, watches: List<WatchIdentifier>? = null): SendOutcome =
        sendStats(stats, watches)
    suspend fun sendError(message: String, watches: List<WatchIdentifier>? = null): SendOutcome
    suspend fun sendSeries(stats: GlanceStats, watches: List<WatchIdentifier>? = null): SendOutcome
    suspend fun sendDaySeries(
        day: Int,
        steps: ByteArray,
        heart: ByteArray,
        watches: List<WatchIdentifier>? = null,
    ): SendOutcome
    suspend fun sendLiveHr(sample: LiveHr.Snapshot, watches: List<WatchIdentifier>? = null): SendOutcome
    fun close()
}

sealed class SendOutcome {
    data class Ok(val results: Map<WatchIdentifier, TransmissionResult>) : SendOutcome()
    data object NoPebbleApp : SendOutcome()
    data object NoConnectedWatches : SendOutcome()
    data class Failed(val detail: String) : SendOutcome()
}

class LoggingPebbleStatsSender : PebbleStatsSender {
    override suspend fun sendStats(stats: GlanceStats, watches: List<WatchIdentifier>?): SendOutcome {
        Log.i(TAG, "mock sendStats steps=${stats.activity.steps} sleep=${stats.sleep.minutesAsleep}")
        return SendOutcome.Ok(emptyMap())
    }

    override suspend fun sendRootStats(stats: GlanceStats, watches: List<WatchIdentifier>?): SendOutcome {
        Log.i(TAG, "mock sendRootStats steps=${stats.activity.steps}")
        return SendOutcome.Ok(emptyMap())
    }

    override suspend fun sendError(message: String, watches: List<WatchIdentifier>?): SendOutcome {
        Log.i(TAG, "mock sendError $message")
        return SendOutcome.Ok(emptyMap())
    }

    override suspend fun sendSeries(stats: GlanceStats, watches: List<WatchIdentifier>?): SendOutcome {
        Log.i(TAG, "mock sendSeries ${SeriesCodec.describe(stats)}")
        return SendOutcome.Ok(emptyMap())
    }

    override suspend fun sendDaySeries(
        day: Int,
        steps: ByteArray,
        heart: ByteArray,
        watches: List<WatchIdentifier>?,
    ): SendOutcome {
        Log.i(TAG, "mock sendDaySeries day=$day steps=${steps.size} hr=${heart.size}")
        return SendOutcome.Ok(emptyMap())
    }

    override suspend fun sendLiveHr(sample: LiveHr.Snapshot, watches: List<WatchIdentifier>?): SendOutcome {
        Log.i(TAG, "mock sendLiveHr bpm=${sample.bpm}")
        return SendOutcome.Ok(emptyMap())
    }

    override fun close() = Unit

    companion object {
        private const val TAG = "HealthGlance"
    }
}

class PebbleKitStatsSender(context: Context) : PebbleStatsSender {
    private val sender = DefaultPebbleSender(context.applicationContext)

    override suspend fun sendStats(stats: GlanceStats, watches: List<WatchIdentifier>?): SendOutcome {
        Log.i(
            TAG,
            "sendStats steps=${stats.activity.steps} sleep=${stats.sleep.minutesAsleep} rhr=${stats.heart.restingHr} wo=${stats.workout.durationSec}s ${stats.workout.zoneLow}/${stats.workout.zoneMid}/${stats.workout.zoneHigh} ${stats.workout.title}",
        )
        return transmit(stats.toDictionary(), watches)
    }

    override suspend fun sendRootStats(stats: GlanceStats, watches: List<WatchIdentifier>?): SendOutcome {
        Log.i(TAG, "sendRootStats steps=${stats.activity.steps} sleep=${stats.sleep.minutesAsleep} rhr=${stats.heart.restingHr}")
        return transmit(stats.toRootDictionary(), watches)
    }

    /** The detail screens' charts, on their own so a byte-array problem cannot
     *  take the four cards down with it. */
    override suspend fun sendSeries(stats: GlanceStats, watches: List<WatchIdentifier>?): SendOutcome {
        Log.i(TAG, "sendSeries ${SeriesCodec.describe(stats)}")
        return transmit(seriesDictionary(stats), watches)
    }

    /** Answer a climb-back request with one past day's charts. */
    override suspend fun sendDaySeries(
        day: Int,
        steps: ByteArray,
        heart: ByteArray,
        watches: List<WatchIdentifier>?,
    ): SendOutcome {
        Log.i(TAG, "sendDaySeries day=$day steps=${steps.size}B hr=${heart.size}B")
        val dict: PebbleDictionary = mapOf(
            MessageKeys.SERIES_DAY to PebbleDictionaryItem.UInt32(day.toLong().coerceAtLeast(0).toUInt()),
            MessageKeys.STEPS_SERIES to PebbleDictionaryItem.Bytes(steps),
            MessageKeys.HR_SERIES to PebbleDictionaryItem.Bytes(heart),
        )
        return transmit(dict, watches)
    }

    override suspend fun sendLiveHr(sample: LiveHr.Snapshot, watches: List<WatchIdentifier>?): SendOutcome {
        Log.i(TAG, "sendLiveHr bpm=${sample.bpm} state=${sample.state} min=${sample.min} max=${sample.max}")
        return transmit(sample.toLiveHrDictionary(), watches)
    }

    override suspend fun sendError(message: String, watches: List<WatchIdentifier>?): SendOutcome {
        val data: PebbleDictionary = mapOf(
            MessageKeys.STATS_ERR to PebbleDictionaryItem.Text(message.take(28)),
        )
        return transmit(data, watches)
    }

    override fun close() {
        sender.close()
    }

    private suspend fun transmit(
        data: PebbleDictionary,
        watches: List<WatchIdentifier>?,
    ): SendOutcome {
        return try {
            val results = sender.sendDataToPebble(MessageKeys.WATCH_APP_UUID, data, watches)
            Log.i(TAG, "transmit result null=${results == null} size=${results?.size} detail=$results")
            when {
                results == null -> SendOutcome.NoPebbleApp
                results.isEmpty() -> SendOutcome.NoConnectedWatches
                else -> SendOutcome.Ok(results)
            }
        } catch (e: Exception) {
            Log.e(TAG, "sendDataToPebble failed", e)
            SendOutcome.Failed(e.message ?: e.javaClass.simpleName)
        }
    }

    companion object {
        private const val TAG = "HealthGlance"
    }
}

/** Root Activity/Sleep/Heart cards only — small enough for a warm push before full sync. */
fun GlanceStats.toRootDictionary(): PebbleDictionary {
    val distM = (activity.distanceKm * 1000.0).roundToInt().coerceAtLeast(0)
    val rhr = if (heart.restingHr > 0) heart.restingHr else activity.restingHr
    return mapOf(
        MessageKeys.STEPS to PebbleDictionaryItem.UInt32(activity.steps.coerceAtLeast(0)),
        MessageKeys.STEP_GOAL to PebbleDictionaryItem.UInt32(activity.stepGoal.toLong().coerceAtLeast(0)),
        MessageKeys.DIST_M to PebbleDictionaryItem.UInt32(distM.toLong()),
        MessageKeys.CALORIES to PebbleDictionaryItem.UInt32(activity.calories.coerceAtLeast(0)),
        MessageKeys.ACTIVE_MIN to PebbleDictionaryItem.UInt32(activity.activeMinutes.toLong().coerceAtLeast(0)),
        MessageKeys.RESTING_HR to PebbleDictionaryItem.UInt32(rhr.toLong().coerceAtLeast(0)),
        MessageKeys.SLEEP_MIN to PebbleDictionaryItem.UInt32(sleep.minutesAsleep.toLong().coerceAtLeast(0)),
        MessageKeys.DEEP to PebbleDictionaryItem.UInt32(sleep.stages.deep.toLong().coerceAtLeast(0)),
        MessageKeys.LIGHT to PebbleDictionaryItem.UInt32(sleep.stages.light.toLong().coerceAtLeast(0)),
        MessageKeys.REM to PebbleDictionaryItem.UInt32(sleep.stages.rem.toLong().coerceAtLeast(0)),
        MessageKeys.AWAKE to PebbleDictionaryItem.UInt32(sleep.stages.awake.toLong().coerceAtLeast(0)),
        MessageKeys.UPDATED to PebbleDictionaryItem.Text(shortTime(updatedAt)),
        MessageKeys.SLEEP_START to PebbleDictionaryItem.Text(shortTime(sleep.start)),
        MessageKeys.SLEEP_END to PebbleDictionaryItem.Text(shortTime(sleep.end)),
        MessageKeys.HR_MIN to PebbleDictionaryItem.UInt32(heart.minHr.toLong().coerceAtLeast(0)),
        MessageKeys.HR_MAX to PebbleDictionaryItem.UInt32(heart.maxHr.toLong().coerceAtLeast(0)),
    )
}

/** Minimal UI payload: root cards + workout (always includes WO_* so warm push can omit them). */
fun GlanceStats.toDictionary(): PebbleDictionary {
    val dict = toRootDictionary().toMutableMap()
    dict[MessageKeys.WO_DUR_S] = PebbleDictionaryItem.UInt32(workout.durationSec.toLong().coerceAtLeast(0))
    dict[MessageKeys.WO_CAL] = PebbleDictionaryItem.UInt32(workout.calories.toLong().coerceAtLeast(0))
    dict[MessageKeys.WO_AVG_HR] = PebbleDictionaryItem.UInt32(workout.avgHr.toLong().coerceAtLeast(0))
    dict[MessageKeys.WO_DIST_M] = PebbleDictionaryItem.UInt32(workout.distanceM.toLong().coerceAtLeast(0))
    dict[MessageKeys.WO_START] = PebbleDictionaryItem.Text(shortTime(workout.start))
    dict[MessageKeys.WO_END] = PebbleDictionaryItem.Text(shortTime(workout.end))
    dict[MessageKeys.WO_TITLE] = PebbleDictionaryItem.Text(
        if (workout.durationSec > 0) workout.title.take(12).ifBlank { "WORKOUT" } else "",
    )
    dict[MessageKeys.WO_ZONE_LOW] = PebbleDictionaryItem.UInt32(workout.zoneLow.toLong().coerceAtLeast(0))
    dict[MessageKeys.WO_ZONE_MID] = PebbleDictionaryItem.UInt32(workout.zoneMid.toLong().coerceAtLeast(0))
    dict[MessageKeys.WO_ZONE_HIGH] = PebbleDictionaryItem.UInt32(workout.zoneHigh.toLong().coerceAtLeast(0))
    return dict
}

/** Prefer "HH:mm" from an ISO-ish timestamp; fall back to a short trim. */
private fun shortTime(value: String): String {
    if (value.length >= 16 && value[10] == 'T') return value.substring(11, 16)
    return value.take(16)
}

/** Live HR only — six small keys, so the push stays far inside the AppMessage budget. */
fun LiveHr.Snapshot.toLiveHrDictionary(): PebbleDictionary {
    return mapOf(
        MessageKeys.HR_LIVE to PebbleDictionaryItem.UInt32(bpm.coerceAtLeast(0).toLong()),
        MessageKeys.HR_LIVE_TS to PebbleDictionaryItem.UInt32(atMillis.coerceAtLeast(0L) / 1000L),
        MessageKeys.HR_LIVE_BATT to PebbleDictionaryItem.UInt32(batteryPercent.coerceAtLeast(0).toLong()),
        MessageKeys.HR_LIVE_STATE to PebbleDictionaryItem.UInt32(state.code.toLong()),
        MessageKeys.HR_LIVE_MIN to PebbleDictionaryItem.UInt32(min.coerceAtLeast(0).toLong()),
        MessageKeys.HR_LIVE_MAX to PebbleDictionaryItem.UInt32(max.coerceAtLeast(0).toLong()),
    )
}

/**
 * Appends the detail screens' series to a stats dictionary. PebbleKit's byte
 * array item is `Bytes`; a PebbleDictionary is an immutable map, so this returns
 * a merged copy rather than mutating.
 */
/** Byte-array series only — the watch merges them into its detail screens. */
internal fun seriesDictionary(stats: GlanceStats): PebbleDictionary {
    val keys = mapOf(
        MessageKeys.STEPS_SERIES to "STEPS_SERIES",
        MessageKeys.SLEEP_SERIES to "SLEEP_SERIES",
        MessageKeys.HR_SERIES to "HR_SERIES",
        MessageKeys.WO_SERIES to "WO_SERIES",
        MessageKeys.WO_HIST to "WO_HIST",
    )
    val series = SeriesCodec.all(stats)
    val dict = keys.mapNotNull { (key, name) ->
        series[name]?.let { key to PebbleDictionaryItem.Bytes(it) }
    }.toMap()
    return dict + (MessageKeys.SERIES_DAY to PebbleDictionaryItem.UInt32(0u))
}
