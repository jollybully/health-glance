package com.jollybully.healthglance.sync

import android.content.Context
import android.util.Log
import com.jollybully.healthglance.data.GlanceStats
import com.jollybully.healthglance.data.HealthConnectAvailability
import com.jollybully.healthglance.data.HealthConnectRepository
import com.jollybully.healthglance.pebble.PebbleKitStatsSender
import com.jollybully.healthglance.pebble.SeriesCodec
import com.jollybully.healthglance.pebble.PebbleStatsSender
import com.jollybully.healthglance.pebble.SendOutcome
import io.rebble.pebblekit2.common.model.WatchIdentifier
import kotlinx.coroutines.delay

data class SyncResult(
    val stats: GlanceStats?,
    val status: String,
    val outcome: SendOutcome? = null,
)

class StatsSync(
    context: Context,
    private val repository: HealthConnectRepository = HealthConnectRepository(context),
    private val sender: PebbleStatsSender = PebbleKitStatsSender(context),
    private val cache: SyncCache = SyncCache(context),
) {
    @Volatile private var lastSyncAt = 0L

    suspend fun sync(watches: List<WatchIdentifier>? = null): SyncResult {
        val now = System.currentTimeMillis()
        /* onAppOpened + watch STATS_REQ often fire together — one sync is enough. */
        if (now - lastSyncAt < 2_000L) {
            Log.i(TAG, "sync debounced")
            return SyncResult(stats = cache.loadStats(), status = cache.loadStatus())
        }
        lastSyncAt = now

        /* Cached snapshot of all four cards, before Health Connect is queried. */
        val cached = cache.loadStats()
        if (cached != null) {
            try {
                val warm = sender.sendStats(cached, watches)
                Log.i(TAG, "cached push steps=${cached.activity.steps} outcome=$warm")
                delay(350)
            } catch (e: Exception) {
                Log.w(TAG, "cached push failed", e)
            }
        }

        val availability = repository.availability()
        if (availability !is HealthConnectAvailability.Available) {
            val status = SyncStatus.fromAvailability(availability)
            cache.saveStatus(status)
            return SyncResult(stats = cached, status = status)
        }

        if (!repository.hasReadPermissions()) {
            val status = SyncStatus.needPermissions()
            cache.saveStatus(status)
            sender.sendError(SyncStatus.watchError(status), watches)
            return SyncResult(stats = cached, status = status)
        }

        return try {
            val stats = repository.loadStats()
            val outcome = sender.sendStats(stats, watches)
            /* Separate push: the charts are a bonus, never a reason for the deck
             * to fail. AppMessage is ACK-gated, so give the first one a moment. */
            delay(250)
            try {
                val seriesOutcome = sender.sendSeries(stats, watches)
                cache.saveSeriesInfo(SeriesCodec.describe(stats), seriesOutcome.toString())
                Log.i(TAG, "series push ${SeriesCodec.describe(stats)} outcome=$seriesOutcome")
            } catch (e: Exception) {
                Log.w(TAG, "series push failed", e)
                cache.saveSeriesInfo("series push failed: ${e.message}", "failed")
            }
            val status = SyncStatus.fromSendOutcome(outcome)
            cache.save(stats, status)
            Log.i(TAG, "sync status=$status steps=${stats.activity.steps}")
            SyncResult(stats = stats, status = status, outcome = outcome)
        } catch (e: Exception) {
            Log.e(TAG, "sync failed", e)
            val status = SyncStatus.failed(e.message)
            cache.saveStatus(status)
            if (cached == null) {
                sender.sendError(SyncStatus.watchError(status), watches)
            }
            SyncResult(stats = cached, status = status)
        }
    }

    fun cachedStats(): GlanceStats? = cache.loadStats()

    fun cachedStatus(): String = cache.loadStatus()

    /** What the last series push packed, and what the watch transport answered. */
    fun cachedSeriesInfo(): String = cache.seriesInfo()

    fun close() {
        sender.close()
    }

    companion object {
        private const val TAG = "HealthGlance"
    }
}
