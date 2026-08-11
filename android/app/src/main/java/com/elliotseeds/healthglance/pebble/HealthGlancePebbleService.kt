package com.elliotseeds.healthglance.pebble

import android.util.Log
import com.elliotseeds.healthglance.sync.StatsSync
import io.rebble.pebblekit2.client.BasePebbleListenerService
import io.rebble.pebblekit2.common.model.PebbleDictionary
import io.rebble.pebblekit2.common.model.ReceiveResult
import io.rebble.pebblekit2.common.model.WatchIdentifier
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import java.util.UUID

class HealthGlancePebbleService : BasePebbleListenerService() {
    private val sync by lazy { StatsSync(applicationContext) }

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
        super.onDestroy()
    }

    companion object {
        private const val TAG = "HealthGlance"
    }
}
