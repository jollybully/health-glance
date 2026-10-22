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
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.os.Handler
import android.os.Looper
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
        fun onStreaming(deviceName: String)
        fun onSample(bpm: Int, sensorContact: Boolean?)
        fun onBattery(percent: Int)
        fun onDropped(reason: String)
    }

    companion object {
        private const val TAG = "HealthGlance"
        val HEART_RATE_SERVICE: UUID = UUID.fromString("0000180d-0000-1000-8000-00805f9b34fb")
        val HEART_RATE_MEASUREMENT: UUID = UUID.fromString("00002a37-0000-1000-8000-00805f9b34fb")
        val BATTERY_SERVICE: UUID = UUID.fromString("0000180f-0000-1000-8000-00805f9b34fb")
        val BATTERY_LEVEL: UUID = UUID.fromString("00002a19-0000-1000-8000-00805f9b34fb")
        val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
        private const val SCAN_TIMEOUT_MS = 20_000L
        private val MONITOR_HINTS = listOf(
            "hr", "hrm", "heart", "pulse", "polar", "garmin", "wahoo", "coospo", "magene",
            "fitbit", "amazfit", "coros", "suunto", "tickr", "h9", "h10", "verity",
        )
        private const val PREF = "live_hr"
        private const val PREF_ADDRESS = "last_address"
    }

    private val handler = Handler(Looper.getMainLooper())
    /** Names of nearby BLE devices seen while scanning, for the failure message. */
    @Volatile private var seenNames: MutableSet<String> = java.util.Collections.synchronizedSet(mutableSetOf())
    @Volatile private var seenHrDevices = 0
    private var gatt: BluetoothGatt? = null
    private var scanning = false
    @Volatile private var stopped = true

    private val prefs = context.getSharedPreferences(PREF, Context.MODE_PRIVATE)

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

    /** Any BLE call can throw SecurityException or IllegalStateException; the
     *  service must show a state, never die. */
    private fun drop(reason: String) {
        stopped = true
        runCatching { handler.removeCallbacksAndMessages(null) }
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
                listener.onConnecting(device.name ?: device.address)
                gatt = device.connectGatt(context, false, callback, BluetoothDevice.TRANSPORT_LE)
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
        /* No service filter: a strap that advertises its name but not its services
         * (or only reveals 0x180D after connecting) would never surface under a
         * filter, and the user would be told nothing at all. */
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
            if (!hasHr && !looksLikeMonitor(name)) return  /* not a strap: ignore */
            if (hasHr) seenHrDevices++
            val adapter = (context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager)?.adapter
            if (adapter != null) stopScan(adapter)
            prefs.edit().putString(PREF_ADDRESS, device.address).apply()
            listener.onConnecting(device.name ?: device.address)
            gatt = try {
                device.connectGatt(context, false, callback, BluetoothDevice.TRANSPORT_LE)
            } catch (e: Exception) {
                drop("Connect failed: ${e.message ?: e.javaClass.simpleName}")
                null
            }
        }

        override fun onScanFailed(errorCode: Int) {
            scanning = false
            listener.onDropped("Scan failed ($errorCode)")
        }
    }

    private val callback = object : BluetoothGattCallback() {
        @SuppressLint("MissingPermission")
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            if (stopped) {
                g.close()
                return
            }
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                g.discoverServices()
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                g.close()
                gatt = null
                if (!stopped) listener.onDropped("Strap disconnected")
            }
        }

        @SuppressLint("MissingPermission")
        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            val service = g.getService(HEART_RATE_SERVICE)
            val characteristic = service?.getCharacteristic(HEART_RATE_MEASUREMENT)
            if (characteristic == null) {
                listener.onDropped("No heart-rate service on this device")
                return
            }
            g.setCharacteristicNotification(characteristic, true)
            val cccd = characteristic.getDescriptor(CCCD)
            if (cccd == null) {
                listener.onDropped("Strap will not notify")
                return
            }
            cccd.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
            g.writeDescriptor(cccd)
            readBattery(g)
        }

        override fun onDescriptorWrite(g: BluetoothGatt, descriptor: BluetoothGattDescriptor, status: Int) {
            if (descriptor.uuid != CCCD) return
            if (status == BluetoothGatt.GATT_SUCCESS) {
                listener.onStreaming(g.device?.name ?: g.device?.address ?: "strap")
            } else {
                listener.onDropped("Subscribe failed ($status)")
            }
        }

        @Deprecated("Deprecated in Java")
        override fun onCharacteristicChanged(g: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
            if (characteristic.uuid != HEART_RATE_MEASUREMENT) return
            val value = characteristic.value ?: return
            HeartRateMeasurement.parse(value)?.let { listener.onSample(it.bpm, it.sensorContact) }
        }

        override fun onCharacteristicChanged(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray,
        ) {
            if (characteristic.uuid != HEART_RATE_MEASUREMENT) return
            HeartRateMeasurement.parse(value)?.let { listener.onSample(it.bpm, it.sensorContact) }
        }

        @Deprecated("Deprecated in Java")
        override fun onCharacteristicRead(g: BluetoothGatt, characteristic: BluetoothGattCharacteristic, status: Int) {
            if (characteristic.uuid == BATTERY_LEVEL && status == BluetoothGatt.GATT_SUCCESS) {
                val value = characteristic.value ?: return
                if (value.isNotEmpty()) listener.onBattery(value[0].toInt() and 0xFF)
            }
        }
    }

    @SuppressLint("MissingPermission")
    private fun readBattery(g: BluetoothGatt) {
        val battery = g.getService(BATTERY_SERVICE)?.getCharacteristic(BATTERY_LEVEL) ?: return
        g.readCharacteristic(battery)
    }

    @SuppressLint("MissingPermission")
    fun stop() {
        stopped = true
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
