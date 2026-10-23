package com.jollybully.healthglance.ble

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.BluetoothStatusCodes
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.util.Log
import com.jollybully.healthglance.HealthGlanceApp
import java.util.UUID

/**
 * BLE central for a standard Heart Rate Monitor (chest strap or a watch that
 * broadcasts heart rate): Heart Rate Service 0x180D, measurement 0x2A37,
 * optional Battery Service 0x180F/0x2A19.
 *
 * The watch cannot do this itself — PebbleOS only implements GATT services as a
 * server — so the phone is the central and the watch is a display.
 */
class HeartRateClient(
    private val context: Context,
    private val listener: Listener,
) {
    interface Listener {
        fun onScanning()
        fun onConnecting(name: String)
        /** CCCD subscribed; HR notifications may arrive but no valid sample yet. */
        fun onSubscribed(deviceName: String)
        /** First valid HR sample on this session (not sent on CCCD alone). */
        fun onStreaming(deviceName: String)
        fun onSample(bpm: Int, sensorContact: Boolean?)
        fun onBattery(percent: Int)
        fun onDropped(reason: String)
    }

    companion object {
        private const val TAG = "HealthGlance"
        private const val HR_TAG = "HealthGlance/HrBle"
        val HEART_RATE_SERVICE: UUID = UUID.fromString("0000180d-0000-1000-8000-00805f9b34fb")
        val HEART_RATE_MEASUREMENT: UUID = UUID.fromString("00002a37-0000-1000-8000-00805f9b34fb")
        val BATTERY_SERVICE: UUID = UUID.fromString("0000180f-0000-1000-8000-00805f9b34fb")
        val BATTERY_LEVEL: UUID = UUID.fromString("00002a19-0000-1000-8000-00805f9b34fb")
        val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
        private const val SCAN_TIMEOUT_MS = 20_000L
        private const val FIRST_SAMPLE_TIMEOUT_MS = 45_000L
        private const val MAX_HR_NOTIFICATION_LOGS = 10
        private val MONITOR_HINTS = listOf(
            "hr", "hrm", "heart", "pulse", "polar", "garmin", "wahoo", "coospo", "magene",
            "fitbit", "amazfit", "coros", "suunto", "tickr", "h9", "h10", "verity",
        )
        private const val PREF = "live_hr"
        private const val PREF_ADDRESS = "last_address"
    }

    /** Bounded counters for a single GATT session (logged on drop / timeout). */
    data class SessionCounters(
        val sessionGen: Int,
        var rawHrNotifications: Int = 0,
        var parseRejected: Int = 0,
        var contactRejected: Int = 0,
        var acceptedSamples: Int = 0,
        var subscribedAtElapsed: Long = 0L,
        var firstRawAtElapsed: Long = 0L,
        var firstAcceptedAtElapsed: Long = 0L,
    ) {
        fun summary(): String =
            "gen=$sessionGen raw=$rawHrNotifications parseReject=$parseRejected " +
                "contactReject=$contactRejected accepted=$acceptedSamples " +
                "subscribedMs=${subscribedAtElapsed} firstRawMs=${firstRawAtElapsed} " +
                "firstAcceptedMs=${firstAcceptedAtElapsed}"
    }

    private val handler = Handler(Looper.getMainLooper())
    /** Names of nearby BLE devices seen while scanning, for the failure message. */
    @Volatile private var seenNames: MutableSet<String> = java.util.Collections.synchronizedSet(mutableSetOf())
    @Volatile private var seenHrDevices = 0
    private var gatt: BluetoothGatt? = null
    private var scanning = false
    @Volatile private var stopped = true
    @Volatile private var sessionGen = 0
    @Volatile private var activeSessionGen = 0
    @Volatile private var deviceLabel = "strap"
    @Volatile private var streamingAnnounced = false
    @Volatile private var counters = SessionCounters(0)
    @Volatile private var hrNotificationLogs = 0

    private val prefs = context.getSharedPreferences(PREF, Context.MODE_PRIVATE)

    private val callbackIdentity = System.identityHashCode(this)

    private val firstSampleTimeout = Runnable {
        if (stopped || counters.acceptedSamples > 0) return@Runnable
        logSessionSummary("first_sample_timeout")
        val msg = buildString {
            append("No heart rate notifications")
            if (counters.rawHrNotifications == 0) append(" (0 raw callbacks)")
            else append(" (${counters.rawHrNotifications} raw, ${counters.parseRejected} parse rejects)")
            append(" within ${FIRST_SAMPLE_TIMEOUT_MS / 1000}s")
        }
        drop(msg)
    }

    fun hasPermissions(): Boolean {
        val needed = when {
            Build.VERSION.SDK_INT >= Build.VERSION_CODES.S ->
                arrayOf(android.Manifest.permission.BLUETOOTH_SCAN, android.Manifest.permission.BLUETOOTH_CONNECT)
            else ->
                arrayOf(android.Manifest.permission.ACCESS_FINE_LOCATION)
        }
        return needed.all { context.checkSelfPermission(it) == PackageManager.PERMISSION_GRANTED }
    }

    fun requiredPermissions(): Array<String> = when {
        Build.VERSION.SDK_INT >= Build.VERSION_CODES.S -> arrayOf(
            android.Manifest.permission.BLUETOOTH_SCAN,
            android.Manifest.permission.BLUETOOTH_CONNECT,
        )
        else -> arrayOf(android.Manifest.permission.ACCESS_FINE_LOCATION)
    }

    private fun logHr(message: String) {
        Log.i(HR_TAG, message)
    }

    private fun logSessionSummary(reason: String) {
        logHr("session_summary reason=$reason ${counters.summary()} callbackId=$callbackIdentity stopped=$stopped")
    }

    private fun beginSession(label: String) {
        sessionGen++
        activeSessionGen = sessionGen
        streamingAnnounced = false
        hrNotificationLogs = 0
        counters = SessionCounters(sessionGen)
        deviceLabel = label
        handler.removeCallbacks(firstSampleTimeout)
        logHr("session_begin gen=$activeSessionGen callbackId=$callbackIdentity label=$label")
    }

    private fun isCurrentSession(g: BluetoothGatt): Boolean {
        return !stopped && gatt === g && activeSessionGen == counters.sessionGen
    }

    /** Any BLE call can throw SecurityException or IllegalStateException; the
     *  service must show a state, never die. */
    private fun drop(reason: String) {
        stopped = true
        runCatching { handler.removeCallbacksAndMessages(null) }
        logSessionSummary("drop")
        listener.onDropped(reason)
    }

    @SuppressLint("MissingPermission")
    fun start() {
        try {
            startInner()
        } catch (e: SecurityException) {
            HealthGlanceApp.record("scan SecurityException: ${HealthGlanceApp.stack(e)}")
            drop("Bluetooth permission needed")
        } catch (e: Exception) {
            HealthGlanceApp.record("scan failed: ${HealthGlanceApp.stack(e)}")
            drop("Bluetooth: ${e.message ?: e.javaClass.simpleName}")
        }
    }

    @SuppressLint("MissingPermission")
    private fun startInner() {
        stopped = false
        val adapter = (context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager)?.adapter
        if (adapter == null || !adapter.isEnabled) {
            listener.onDropped("Bluetooth is off")
            return
        }
        val known = prefs.getString(PREF_ADDRESS, null)
        if (known != null) {
            val device = try {
                adapter.getRemoteDevice(known)
            } catch (e: IllegalArgumentException) {
                null
            }
            if (device != null) {
                val label = device.name ?: "known_device"
                beginSession(label)
                listener.onConnecting(label)
                gatt = device.connectGatt(context, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
                logHr("connectGatt enqueued gen=$activeSessionGen")
                return
            }
        }
        scan(adapter)
    }

    /** A name hint only; the GATT discovery is the real test. */
    private fun looksLikeMonitor(name: String): Boolean {
        val n = name.lowercase()
        return MONITOR_HINTS.any { n.contains(it) }
    }

    /** What the scan actually saw, for the message when nothing connects. */
    fun nearbySummary(): String {
        val hr = seenHrDevices
        val names = seenNames.toList().take(3)
        return buildString {
            append(if (hr > 0) "$hr heart-rate device(s) advertised" else "no heart-rate service advertised")
            if (names.isNotEmpty()) append("; nearby: ${names.joinToString(", ")}")
        }
    }

    @SuppressLint("MissingPermission")
    private fun scan(adapter: BluetoothAdapter) {
        val scanner = adapter.bluetoothLeScanner
        if (scanner == null) {
            listener.onDropped("No BLE scanner")
            return
        }
        seenNames.clear()
        seenHrDevices = 0
        listener.onScanning()
        val filters: List<ScanFilter> = emptyList()
        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()
        scanning = true
        try {
            scanner.startScan(filters, settings, scanCallback)
        } catch (e: Exception) {
            scanning = false
            drop("Scan failed: ${e.message ?: e.javaClass.simpleName}")
            return
        }
        handler.postDelayed({
            if (scanning) {
                stopScan(adapter)
                listener.onDropped("No strap found (${nearbySummary()})")
            }
        }, SCAN_TIMEOUT_MS)
    }

    @SuppressLint("MissingPermission")
    private fun stopScan(adapter: BluetoothAdapter) {
        if (!scanning) return
        scanning = false
        try {
            adapter.bluetoothLeScanner?.stopScan(scanCallback)
        } catch (e: IllegalStateException) {
            Log.w(TAG, "stopScan failed", e)
        }
    }

    private val scanCallback = object : ScanCallback() {
        @SuppressLint("MissingPermission")
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            if (stopped || gatt != null) return
            val device = result.device ?: return
            val advertised = result.scanRecord?.serviceUuids.orEmpty().toList()
            val hasHr = advertised.contains(android.os.ParcelUuid(HEART_RATE_SERVICE))
            val name = (result.scanRecord?.deviceName ?: device.name ?: "").trim()
            if (name.isNotEmpty()) seenNames.add(name)
            if (!hasHr && !looksLikeMonitor(name)) return
            if (hasHr) seenHrDevices++
            val adapter = (context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager)?.adapter
            if (adapter != null) stopScan(adapter)
            prefs.edit().putString(PREF_ADDRESS, device.address).apply()
            val label = device.name ?: "scanned_device"
            beginSession(label)
            listener.onConnecting(label)
            gatt = try {
                device.connectGatt(context, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
            } catch (e: Exception) {
                drop("Connect failed: ${e.message ?: e.javaClass.simpleName}")
                null
            }
            logHr("scan_connect gen=$activeSessionGen enqueued=${gatt != null}")
        }

        override fun onScanFailed(errorCode: Int) {
            scanning = false
            listener.onDropped("Scan failed ($errorCode)")
        }
    }

    private fun characteristicProps(characteristic: BluetoothGattCharacteristic): String {
        val p = characteristic.properties
        return buildString {
            if (p and BluetoothGattCharacteristic.PROPERTY_READ != 0) append('R')
            if (p and BluetoothGattCharacteristic.PROPERTY_WRITE != 0) append('W')
            if (p and BluetoothGattCharacteristic.PROPERTY_NOTIFY != 0) append('N')
            if (p and BluetoothGattCharacteristic.PROPERTY_INDICATE != 0) append('I')
            if (isEmpty()) append('-')
        }
    }

    private fun handleHrNotification(g: BluetoothGatt, value: ByteArray) {
        if (!isCurrentSession(g)) {
            logHr("ignore_hr_notification stale_gen g=${System.identityHashCode(g)} active=$activeSessionGen")
            return
        }
        counters.rawHrNotifications++
        if (counters.firstRawAtElapsed == 0L) {
            counters.firstRawAtElapsed = SystemClock.elapsedRealtime()
        }
        if (hrNotificationLogs < MAX_HR_NOTIFICATION_LOGS) {
            hrNotificationLogs++
            val flags = if (value.isNotEmpty()) value[0].toInt() and 0xFF else -1
            logHr(
                "hr_raw #$hrNotificationLogs gen=$activeSessionGen len=${value.size} " +
                    "flags=0x${flags.toString(16)} hex=${value.toHexPreview()}",
            )
        }
        val reject = HeartRateMeasurement.parseRejectReason(value)
        val parsed = HeartRateMeasurement.parse(value)
        if (parsed == null) {
            counters.parseRejected++
            logHr("hr_parse_reject gen=$activeSessionGen reason=${reject ?: "unknown"}")
            return
        }
        if (parsed.sensorContact == false) {
            counters.contactRejected++
            logHr("hr_contact_reject gen=$activeSessionGen bpm=${parsed.bpm}")
            listener.onSample(parsed.bpm, parsed.sensorContact)
            return
        }
        counters.acceptedSamples++
        if (counters.firstAcceptedAtElapsed == 0L) {
            counters.firstAcceptedAtElapsed = SystemClock.elapsedRealtime()
            handler.removeCallbacks(firstSampleTimeout)
        }
        logHr("hr_accepted gen=$activeSessionGen bpm=${parsed.bpm} contact=${parsed.sensorContact}")
        if (!streamingAnnounced) {
            streamingAnnounced = true
            listener.onStreaming(deviceLabel)
        }
        listener.onSample(parsed.bpm, parsed.sensorContact)
    }

    private fun ByteArray.toHexPreview(maxBytes: Int = 12): String {
        val n = minOf(size, maxBytes)
        return (0 until n).joinToString("") { i ->
            "%02x".format(this[i].toInt() and 0xFF)
        } + if (size > maxBytes) "…" else ""
    }

    private val gattCallback = object : BluetoothGattCallback() {
        @SuppressLint("MissingPermission")
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            logHr(
                "onConnectionStateChange gen=$activeSessionGen status=$status newState=$newState " +
                    "g=${System.identityHashCode(g)} current=${gatt?.let { System.identityHashCode(it) }}",
            )
            if (!isCurrentSession(g) && gatt !== g) {
                logHr("ignore_connection_state stale_gatt")
                runCatching { g.close() }
                return
            }
            if (stopped) {
                g.close()
                return
            }
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                if (status != BluetoothGatt.GATT_SUCCESS) {
                    drop("Connect failed (GATT $status)")
                    return
                }
                val discovered = g.discoverServices()
                logHr("discoverServices enqueued=$discovered gen=$activeSessionGen")
                if (!discovered) drop("discoverServices() returned false")
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                g.close()
                if (gatt === g) gatt = null
                if (!stopped) listener.onDropped("Strap disconnected (GATT $status)")
            }
        }

        @SuppressLint("MissingPermission")
        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            logHr("onServicesDiscovered gen=$activeSessionGen status=$status")
            if (!isCurrentSession(g)) return
            if (status != BluetoothGatt.GATT_SUCCESS) {
                drop("Service discovery failed ($status)")
                return
            }
            val service = g.getService(HEART_RATE_SERVICE)
            val characteristic = service?.getCharacteristic(HEART_RATE_MEASUREMENT)
            if (characteristic == null) {
                listener.onDropped("No heart-rate service on this device")
                return
            }
            logHr(
                "hr_characteristic props=${characteristicProps(characteristic)} " +
                    "notify=${characteristic.properties and BluetoothGattCharacteristic.PROPERTY_NOTIFY != 0}",
            )
            val notifySet = g.setCharacteristicNotification(characteristic, true)
            logHr("setCharacteristicNotification enable=true returned=$notifySet gen=$activeSessionGen")
            val cccd = characteristic.getDescriptor(CCCD)
            if (cccd == null) {
                listener.onDropped("Strap will not notify")
                return
            }
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                val writeStatus = g.writeDescriptor(cccd, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
                val enqueued = writeStatus == BluetoothStatusCodes.SUCCESS
                logHr(
                    "writeDescriptor_cccd status=$writeStatus enqueued=$enqueued " +
                        "gen=$activeSessionGen api=${Build.VERSION.SDK_INT}",
                )
                if (!enqueued) drop("CCCD write not enqueued ($writeStatus)")
            } else {
                @Suppress("DEPRECATION")
                cccd.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                @Suppress("DEPRECATION")
                val gattRef = g.writeDescriptor(cccd)
                logHr(
                    "writeDescriptor_cccd enqueued=${gattRef != null} gen=$activeSessionGen api=${Build.VERSION.SDK_INT}",
                )
            }
        }

        override fun onDescriptorWrite(g: BluetoothGatt, descriptor: BluetoothGattDescriptor, status: Int) {
            if (descriptor.uuid != CCCD) return
            logHr("onDescriptorWrite gen=$activeSessionGen status=$status cccd=true")
            if (!isCurrentSession(g)) return
            if (status != BluetoothGatt.GATT_SUCCESS) {
                listener.onDropped("Subscribe failed ($status)")
                return
            }
            counters.subscribedAtElapsed = SystemClock.elapsedRealtime()
            deviceLabel = g.device?.name ?: deviceLabel
            listener.onSubscribed(deviceLabel)
            handler.removeCallbacks(firstSampleTimeout)
            handler.postDelayed(firstSampleTimeout, FIRST_SAMPLE_TIMEOUT_MS)
            readBattery(g)
        }

        @Deprecated("Deprecated in Java")
        override fun onCharacteristicChanged(g: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
            if (characteristic.uuid != HEART_RATE_MEASUREMENT) return
            @Suppress("DEPRECATION")
            val value = characteristic.value ?: return
            handleHrNotification(g, value)
        }

        override fun onCharacteristicChanged(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray,
        ) {
            if (characteristic.uuid != HEART_RATE_MEASUREMENT) return
            handleHrNotification(g, value)
        }

        @Deprecated("Deprecated in Java")
        override fun onCharacteristicRead(g: BluetoothGatt, characteristic: BluetoothGattCharacteristic, status: Int) {
            if (characteristic.uuid != BATTERY_LEVEL) return
            if (!isCurrentSession(g)) return
            if (status != BluetoothGatt.GATT_SUCCESS) {
                logHr("battery_read_failed status=$status")
                return
            }
            @Suppress("DEPRECATION")
            val value = characteristic.value ?: return
            if (value.isNotEmpty()) listener.onBattery(value[0].toInt() and 0xFF)
        }

        override fun onCharacteristicRead(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray,
            status: Int,
        ) {
            if (characteristic.uuid != BATTERY_LEVEL) return
            if (!isCurrentSession(g)) return
            if (status != BluetoothGatt.GATT_SUCCESS) {
                logHr("battery_read_failed status=$status")
                return
            }
            if (value.isNotEmpty()) listener.onBattery(value[0].toInt() and 0xFF)
        }
    }

    @SuppressLint("MissingPermission")
    private fun readBattery(g: BluetoothGatt) {
        if (!isCurrentSession(g)) return
        val battery = g.getService(BATTERY_SERVICE)?.getCharacteristic(BATTERY_LEVEL)
        if (battery == null) {
            logHr("battery_characteristic_missing")
            return
        }
        val enqueued = g.readCharacteristic(battery)
        logHr("readBattery enqueued=$enqueued gen=$activeSessionGen")
    }

    @SuppressLint("MissingPermission")
    fun stop() {
        stopped = true
        activeSessionGen = 0
        handler.removeCallbacksAndMessages(null)
        val adapter = (context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager)?.adapter
        if (adapter != null) stopScan(adapter)
        gatt?.let {
            try {
                it.disconnect()
            } catch (e: Exception) {
                Log.w(TAG, "disconnect failed", e)
            }
            it.close()
        }
        gatt = null
    }
}
