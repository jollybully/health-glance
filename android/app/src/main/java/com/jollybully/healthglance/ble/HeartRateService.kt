package com.jollybully.healthglance.ble

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import android.util.Log
import androidx.core.app.NotificationCompat
import com.jollybully.healthglance.HealthGlanceApp
import com.jollybully.healthglance.MainActivity
import com.jollybully.healthglance.R
import com.jollybully.healthglance.pebble.PebbleKitStatsSender
import com.jollybully.healthglance.pebble.PebbleStatsSender
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

/**
 * Foreground service that owns the BLE link to the heart-rate strap and pushes
 * the smoothed value to the watch.
 *
 * Cadence: AppMessage delivery is ack-gated (one message in flight), so 1 Hz
 * samples are smoothed (median of 5) and published at most every 5 s, or early
 * when the value moves by 3 bpm or more. Pushing every sample would just queue.
 *
 * MIUI aggressively kills background processes: this service is the most likely
 * thing to be killed on the owner's Mi 9T Pro, hence the always-on notification
 * and the reconnect loop rather than any silent failure.
 */
class HeartRateService : Service(), HeartRateClient.Listener {

    companion object {
        private const val TAG = "HealthGlance"
        private const val CHANNEL_ID = "live_hr"
        private const val NOTIFICATION_ID = 41
        private const val PUBLISH_INTERVAL_MS = 5_000L
        private const val PUBLISH_DELTA_BPM = 3
        private const val RECONNECT_DELAY_MS = 15_000L
        private const val SMOOTHING_WINDOW = 5

        const val ACTION_START = "com.jollybully.healthglance.LIVE_HR_START"
        const val ACTION_STOP = "com.jollybully.healthglance.LIVE_HR_STOP"

        fun start(context: Context) {
            val intent = Intent(context, HeartRateService::class.java).setAction(ACTION_START)
            try {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                    context.startForegroundService(intent)
                } else {
                    context.startService(intent)
                }
            } catch (e: Exception) {
                /* e.g. ForegroundServiceStartNotAllowedException from the background */
                Log.e(TAG, "could not start the live HR service", e)
                HealthGlanceApp.record("start service failed: ${HealthGlanceApp.stack(e)}")
                LiveHr.set(
                    LiveHr.Snapshot(
                        state = LiveHr.State.Failed,
                        detail = "Could not start: ${e.javaClass.simpleName}",
                    ),
                )
            }
        }

        fun stop(context: Context) {
            try {
                context.startService(
                    Intent(context, HeartRateService::class.java).setAction(ACTION_STOP),
                )
            } catch (e: Exception) {
                Log.e(TAG, "could not stop the live HR service", e)
            }
        }
    }

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private var client: HeartRateClient? = null
    private var sender: PebbleStatsSender = PebbleKitStatsSender(this)
    private var publishJob: Job? = null
    private var reconnectJob: Job? = null
    private var inForeground = false

    private val window = ArrayDeque<Int>()
    private var lastPublishedBpm = 0
    private var lastPublishAt = 0L
    private var sessionMin = 0
    private var sessionMax = 0
    private var battery = 0
    private var snapshot = LiveHr.Snapshot()

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        try {
            createChannel()
        } catch (e: Exception) {
            Log.e(TAG, "notification channel failed", e)
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_STOP -> {
                stopEverything("Stopped")
                return START_NOT_STICKY
            }
            else -> {
                if (!enterForeground()) {
                    stopSelf()
                    return START_NOT_STICKY
                }
                if (client == null) connect()
                return START_STICKY
            }
        }
    }

    /** Android kills the process if a foreground service has no notification, so a
     *  failure to build one is reported rather than allowed to crash. */
    /** Which of the permissions this service needs are actually granted right now. */
    private fun grantedSummary(): String {
        val names = listOf(
            android.Manifest.permission.BLUETOOTH_SCAN,
            android.Manifest.permission.BLUETOOTH_CONNECT,
            android.Manifest.permission.POST_NOTIFICATIONS,
        )
        return names.joinToString(",") { n ->
            val ok = androidx.core.content.ContextCompat.checkSelfPermission(this, n) ==
                android.content.pm.PackageManager.PERMISSION_GRANTED
            "${n.substringAfterLast('.')}=${if (ok) "y" else "n"}"
        }
    }

    private fun enterForeground(): Boolean {
        if (inForeground) {
            updateNotification()
            return true
        }
        return try {
            val notification = buildNotification(0, LiveHr.State.Connecting)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                try {
                    startForeground(
                        NOTIFICATION_ID,
                        notification,
                        ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE,
                    )
                } catch (e: SecurityException) {
                    /* The typed start is refused when the manifest lacks the matching
                     * FOREGROUND_SERVICE_* permission. Better a plain foreground
                     * service than the app disappearing. */
                    HealthGlanceApp.record("typed startForeground refused: ${e.message}")
                    Log.w(TAG, "typed startForeground refused, retrying untyped", e)
                    startForeground(NOTIFICATION_ID, notification)
                }
            } else {
                startForeground(NOTIFICATION_ID, notification)
            }
            inForeground = true
            HealthGlanceApp.breadcrumb("foreground ok")
            true
        } catch (e: Exception) {
            Log.e(TAG, "startForeground failed", e)
            HealthGlanceApp.record("startForeground failed: ${HealthGlanceApp.stack(e)}")
            snapshot = snapshot.copy(
                state = LiveHr.State.Failed,
                detail = "Notification failed: ${e.javaClass.simpleName}",
            )
            LiveHr.set(snapshot)
            false
        }
    }

    private fun connect() {
        reconnectJob?.cancel()
        window.clear()
        HealthGlanceApp.breadcrumb("connect()")
        try {
            client = HeartRateClient(this, this).also { it.start() }
        } catch (e: SecurityException) {
            /* BLE permission revoked or never granted: the likeliest user-facing
             * failure, and it used to take the whole app down. */
            Log.e(TAG, "BLE permission missing", e)
            HealthGlanceApp.record("BLE permission: ${HealthGlanceApp.stack(e)}")
            client = null
            snapshot = snapshot.copy(
                state = LiveHr.State.Failed,
                detail = "Bluetooth permission needed",
            )
            LiveHr.set(snapshot)
            updateNotification()
        } catch (e: Exception) {
            Log.e(TAG, "could not start the BLE client", e)
            HealthGlanceApp.record("BLE client start failed: ${HealthGlanceApp.stack(e)}")
            client = null
            snapshot = snapshot.copy(
                state = LiveHr.State.Failed,
                detail = "Bluetooth: ${e.message ?: e.javaClass.simpleName}",
            )
            LiveHr.set(snapshot)
            updateNotification()
        }
    }

    private fun stopEverything(detail: String) {
        reconnectJob?.cancel()
        publishJob?.cancel()
        client?.stop()
        client = null
        snapshot = snapshot.copy(state = LiveHr.State.Idle, bpm = 0, detail = detail)
        LiveHr.set(snapshot)
        scope.launch { runCatching { sender.close() } }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
            stopForeground(STOP_FOREGROUND_REMOVE)
        } else {
            @Suppress("DEPRECATION")
            stopForeground(true)
        }
        inForeground = false
        stopSelf()
    }

    /* ---------------- HeartRateClient.Listener ---------------- */

    override fun onScanning() {
        HealthGlanceApp.breadcrumb("scanning")
        snapshot = snapshot.copy(state = LiveHr.State.Scanning, detail = "Looking for a strap")
        LiveHr.set(snapshot)
        updateNotification()
    }

    override fun onConnecting(name: String) {
        HealthGlanceApp.breadcrumb("connecting to $name")
        snapshot = snapshot.copy(state = LiveHr.State.Connecting, deviceName = name, detail = "Connecting")
        LiveHr.set(snapshot)
        updateNotification()
    }

    override fun onStreaming(deviceName: String) {
        HealthGlanceApp.breadcrumb("streaming from $deviceName")
        window.clear()
        sessionMin = 0
        sessionMax = 0
        snapshot = snapshot.copy(
            state = LiveHr.State.Streaming,
            deviceName = deviceName,
            detail = "Streaming",
        )
        LiveHr.set(snapshot)
        updateNotification()
        scope.launch { publish(force = true) }
    }

    override fun onSample(bpm: Int, sensorContact: Boolean?) {
        if (sensorContact == false) {
            // strap is on but not reading a pulse: say so instead of freezing a stale number
            snapshot = snapshot.copy(state = LiveHr.State.Streaming, bpm = 0, detail = "No contact")
            LiveHr.set(snapshot)
            return
        }
        window.addLast(bpm)
        while (window.size > SMOOTHING_WINDOW) window.removeFirst()
        val smoothed = window.sorted()[window.size / 2]
        if (sessionMin == 0 || smoothed < sessionMin) sessionMin = smoothed
        if (smoothed > sessionMax) sessionMax = smoothed
        snapshot = snapshot.copy(
            state = LiveHr.State.Streaming,
            bpm = smoothed,
            min = sessionMin,
            max = sessionMax,
            atMillis = System.currentTimeMillis(),
            batteryPercent = battery,
            detail = "Streaming",
        )
        LiveHr.set(snapshot)
        maybePublish()
    }

    override fun onBattery(percent: Int) {
        battery = percent
        snapshot = snapshot.copy(batteryPercent = percent)
        LiveHr.set(snapshot)
    }

    override fun onDropped(reason: String) {
        HealthGlanceApp.breadcrumb("dropped: $reason")
        client?.let { runCatching { it.stop() } }
        client = null
        snapshot = snapshot.copy(state = LiveHr.State.Retry, detail = reason)
        LiveHr.set(snapshot)
        updateNotification()
        reconnectJob?.cancel()
        reconnectJob = scope.launch {
            delay(RECONNECT_DELAY_MS)
            if (client == null) connect()
        }
    }

    /* ---------------- publishing to the watch ---------------- */

    private fun maybePublish() {
        val now = System.currentTimeMillis()
        val due = now - lastPublishAt >= PUBLISH_INTERVAL_MS
        val moved = kotlin.math.abs(snapshot.bpm - lastPublishedBpm) >= PUBLISH_DELTA_BPM
        if (due || moved) scope.launch { publish(force = false) }
    }

    private suspend fun publish(force: Boolean) {
        if (snapshot.bpm <= 0 && !force) return
        lastPublishedBpm = snapshot.bpm
        lastPublishAt = System.currentTimeMillis()
        val outcome = try {
            sender.sendLiveHr(snapshot.copy(publishedAtMillis = lastPublishAt))
        } catch (e: Exception) {
            Log.w(TAG, "live hr push failed", e)
            null
        }
        Log.i(TAG, "live hr bpm=${snapshot.bpm} state=${snapshot.state} outcome=$outcome")
        updateNotification()
    }

    /* ---------------- notification ---------------- */

    private fun createChannel() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) return
        val manager = getSystemService(NotificationManager::class.java) ?: return
        if (manager.getNotificationChannel(CHANNEL_ID) != null) return
        val channel = NotificationChannel(
            CHANNEL_ID,
            "Live heart rate",
            NotificationManager.IMPORTANCE_LOW,
        ).apply {
            description = "Keeps the strap connected while streaming to the watch"
            setShowBadge(false)
        }
        manager.createNotificationChannel(channel)
    }

    private fun buildNotification(bpm: Int, state: LiveHr.State): Notification {
        val open = PendingIntent.getActivity(
            this,
            0,
            Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
        val stop = PendingIntent.getService(
            this,
            1,
            Intent(this, HeartRateService::class.java).setAction(ACTION_STOP),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
        val text = when (state) {
            LiveHr.State.Streaming -> if (bpm > 0) "$bpm bpm · min ${sessionMin} · max ${sessionMax}" else "Waiting for a pulse"
            LiveHr.State.Retry -> "Reconnecting to the strap"
            LiveHr.State.Scanning -> "Looking for a heart-rate strap"
            LiveHr.State.Failed -> snapshot.detail.ifBlank { "Failed" }
            else -> "Connecting to the strap"
        }
        return NotificationCompat.Builder(this, CHANNEL_ID)
            /* A plain drawable, NOT the launcher's adaptive icon: setSmallIcon
             * with a non-bitmap adaptive icon throws and takes the app down the
             * moment the foreground notification is built. */
            .setSmallIcon(R.drawable.ic_stat_hr)
            .setContentTitle("Live heart rate")
            .setContentText(text)
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .setContentIntent(open)
            .addAction(0, "Stop", stop)
            .build()
    }

    private fun updateNotification() {
        if (!inForeground) return
        try {
            val manager = getSystemService(NotificationManager::class.java) ?: return
            manager.notify(NOTIFICATION_ID, buildNotification(snapshot.bpm, snapshot.state))
        } catch (e: Exception) {
            Log.w(TAG, "notification update failed", e)
        }
    }

    override fun onDestroy() {
        client?.let { runCatching { it.stop() } }
        client = null
        scope.cancel()
        LiveHr.reset("Service stopped")
        super.onDestroy()
    }
}
