package com.elliotseeds.healthglance.sync

import android.content.Context
import android.util.Log
import com.elliotseeds.healthglance.data.GlanceStats
import com.elliotseeds.healthglance.data.HealthConnectAvailability
import com.elliotseeds.healthglance.data.HealthConnectRepository
import com.elliotseeds.healthglance.pebble.PebbleKitStatsSender
import com.elliotseeds.healthglance.pebble.PebbleStatsSender
import com.elliotseeds.healthglance.pebble.SendOutcome
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

        /* Tiny root-only warm push so the watch paints without a second full payload. */
        val cached = cache.loadStats()
        if (cached != null) {
            try {
                val warm = sender.sendRootStats(cached, watches)
                Log.i(TAG, "cached root push steps=${cached.activity.steps} outcome=$warm")
                delay(350)
            } catch (e: Exception) {
                Log.w(TAG, "cached root push failed", e)
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

    fun close() {
        sender.close()
    }

    companion object {
        private const val TAG = "HealthGlance"
    }
}
