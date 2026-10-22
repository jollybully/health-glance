package com.jollybully.healthglance.pebble

import android.util.Log
import com.jollybully.healthglance.data.HealthConnectRepository
import com.jollybully.healthglance.sync.StatsSync
import io.rebble.pebblekit2.client.BasePebbleListenerService
import io.rebble.pebblekit2.common.model.PebbleDictionary
import io.rebble.pebblekit2.common.model.PebbleDictionaryItem
import io.rebble.pebblekit2.common.model.ReceiveResult
import io.rebble.pebblekit2.common.model.WatchIdentifier
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import java.util.UUID

class HealthGlancePebbleService : BasePebbleListenerService() {
    private val sync by lazy { StatsSync(applicationContext) }
    private val sender by lazy { PebbleKitStatsSender(applicationContext) }
    private val repository by lazy { HealthConnectRepository(applicationContext) }

    override fun onAppOpened(watchappUUID: UUID, watch: WatchIdentifier) {
        if (watchappUUID != MessageKeys.WATCH_APP_UUID) return
        Log.i(TAG, "watch app opened on $watch — pushing stats")
        coroutineScope.launch(Dispatchers.IO) {
            sync.sync(listOf(watch))
        }
    }

    override suspend fun onMessageReceived(
        watchappUUID: UUID,
        data: PebbleDictionary,
        watch: WatchIdentifier,
    ): ReceiveResult {
        if (watchappUUID != MessageKeys.WATCH_APP_UUID) return ReceiveResult.Nack

        /* The watch cycles back through days on the detail screens; answer with
         * that day's charts specifically rather than a full re-sync. */
        val dayRequest = (data[MessageKeys.SERIES_DAY] as? PebbleDictionaryItem.UInt32)?.value
        if (dayRequest != null) {
            val offset = dayRequest.toInt().coerceIn(0, 30)
            Log.i(TAG, "SERIES_DAY request for $offset from $watch")
            val (steps, hr) = repository.loadDaySeries(offset)
            sender.sendDaySeries(
                day = offset,
                steps = SeriesCodec.stepsSeries(
                    com.jollybully.healthglance.data.ActivityStats(stepsHourly = steps),
                ),
                heart = SeriesCodec.heartSeries(
                    com.jollybully.healthglance.data.HeartStats(hrHourly = hr),
                ),
                watches = listOf(watch),
            )
            return ReceiveResult.Ack
        }

        if (!data.containsKey(MessageKeys.STATS_REQ)) return ReceiveResult.Ack

        Log.i(TAG, "STATS_REQ from $watch")
        sync.sync(listOf(watch))
        return ReceiveResult.Ack
    }

    override fun onAppClosed(watchappUUID: UUID, watch: WatchIdentifier) {
        if (watchappUUID != MessageKeys.WATCH_APP_UUID) return
        Log.i(TAG, "watch app closed on $watch")
    }

    override fun onDestroy() {
        sync.close()
        runCatching { sender.close() }
        super.onDestroy()
    }

    companion object {
        private const val TAG = "HealthGlance"
    }
}
