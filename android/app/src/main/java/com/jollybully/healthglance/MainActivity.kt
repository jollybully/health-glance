package com.jollybully.healthglance

import android.content.Intent
import android.os.Bundle
import androidx.activity.ComponentActivity
import android.Manifest
import android.os.Build
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.runtime.collectAsState
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.health.connect.client.PermissionController
import com.jollybully.healthglance.ble.HeartRateService
import com.jollybully.healthglance.ble.LiveHr
import com.jollybully.healthglance.data.GlanceStats
import com.jollybully.healthglance.data.HealthConnectAvailability
import com.jollybully.healthglance.data.HealthConnectRepository
import com.jollybully.healthglance.data.HealthPermissions
import com.jollybully.healthglance.sync.StatsRefreshWorker
import com.jollybully.healthglance.sync.StatsSync
import com.jollybully.healthglance.sync.SyncStatus
import kotlinx.coroutines.launch

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        val repository = HealthConnectRepository(this)
        val sync = StatsSync(this, repository = repository)
        setContent {
            HealthGlanceTheme {
                CompanionScreen(
                    repository = repository,
                    sync = sync,
                    onOpenRationale = {
                        startActivity(Intent(this, PermissionsRationaleActivity::class.java))
                    },
                )
            }
        }
    }
}

private val Teal = Color(0xFF00B2A9)
private val Ink = Color(0xFFE8EEF2)
private val Mute = Color(0xFF8A9BAB)
private val Bg = Color(0xFF0C1218)

@Composable
fun HealthGlanceTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = darkColorScheme(
            primary = Teal,
            onPrimary = Color.Black,
            background = Bg,
            onBackground = Ink,
            surface = Bg,
            onSurface = Ink,
            onSurfaceVariant = Mute,
        ),
        content = content,
    )
}

@Composable
fun CompanionScreen(
    repository: HealthConnectRepository,
    sync: StatsSync,
    onOpenRationale: () -> Unit,
) {
    val scope = rememberCoroutineScope()
    val context = androidx.compose.ui.platform.LocalContext.current
    var availability by remember { mutableStateOf(repository.availability()) }
    var hasRead by remember { mutableStateOf(false) }
    var hasWrite by remember { mutableStateOf(false) }
    var stats by remember { mutableStateOf<GlanceStats?>(sync.cachedStats()) }
    var status by remember { mutableStateOf(sync.cachedStatus()) }
    var busy by remember { mutableStateOf(false) }
    val live by LiveHr.state.collectAsState()

    /** BLE permissions differ by OS: SCAN/CONNECT on 12+, fine location below it. */
    fun blePermissions(): Array<String> = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
        arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
    } else {
        arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
    }

    fun hasBlePermissions(): Boolean =
        blePermissions().all {
            androidx.core.content.ContextCompat.checkSelfPermission(context, it) ==
                android.content.pm.PackageManager.PERMISSION_GRANTED
        }

    val bleLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions(),
    ) { granted ->
        HealthGlanceApp.breadcrumb("BLE permission result: $granted")
        if (granted.values.all { it }) {
            try {
                HeartRateService.start(context)
            } catch (e: Throwable) {
                HealthGlanceApp.record("start after permission threw: ${HealthGlanceApp.stack(e)}")
            }
        } else {
            status = "Bluetooth permission denied"
        }
    }

    fun updateWorker(schedule: Boolean) {
        if (schedule) StatsRefreshWorker.schedule(context) else StatsRefreshWorker.cancel(context)
    }

    val permissionLauncher = rememberLauncherForActivityResult(
        PermissionController.createRequestPermissionResultContract(),
    ) {
        scope.launch {
            hasRead = repository.hasReadPermissions()
            hasWrite = if (BuildConfig.DEBUG) repository.hasWritePermissions() else false
            status = if (hasRead) "Permissions granted" else SyncStatus.needPermissions()
            updateWorker(hasRead)
            if (hasRead) {
                busy = true
                status = SyncStatus.reading()
                val result = sync.sync()
                stats = result.stats
                status = result.status
                busy = false
            }
        }
    }

    suspend fun refreshNow() {
        busy = true
        status = SyncStatus.reading()
        val result = sync.sync()
        stats = result.stats
        status = result.status
        busy = false
    }

    LaunchedEffect(Unit) {
        availability = repository.availability()
        hasRead = repository.hasReadPermissions()
        hasWrite = if (BuildConfig.DEBUG) repository.hasWritePermissions() else false
        updateWorker(hasRead)
        if (hasRead) refreshNow()
        else if (stats == null) status = SyncStatus.fromAvailability(availability)
    }

    Scaffold(modifier = Modifier.fillMaxSize()) { padding ->
        Column(
            modifier = Modifier
                .padding(padding)
                .padding(20.dp)
                .fillMaxSize()
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Text(
                text = "Health Glance",
                style = MaterialTheme.typography.headlineMedium,
                fontWeight = FontWeight.Bold,
                color = Teal,
            )
            Text(
                text = "Health Connect → Pebble Time 2",
                style = MaterialTheme.typography.bodyMedium,
                color = Mute,
            )
            Text(
                text = "Reads health data on this phone and sends it only to your paired watch. Nothing is uploaded to our servers.",
                style = MaterialTheme.typography.bodySmall,
                color = Mute,
                modifier = Modifier.clickable(onClick = onOpenRationale),
            )
            Text(
                text = "Privacy details",
                style = MaterialTheme.typography.labelLarge,
                color = Teal,
                modifier = Modifier.clickable(onClick = onOpenRationale),
            )

            Text("Health Connect: ${availabilityLabel(availability)}", color = Ink)
            Text("Read perms: ${if (hasRead) "yes" else "no"}", color = Mute)
            if (BuildConfig.DEBUG) {
                Text("Write perms (seeder): ${if (hasWrite) "yes" else "no"}", color = Mute)
            }
            Text("Status: $status", color = Ink)
            /* The charts are a separate push, so say plainly what was packed and
             * what the watch transport answered. Without this, "no graphs" cannot
             * be told apart from "no data in Health Connect". */
            val seriesInfo = sync.cachedSeriesInfo()
            if (seriesInfo.isNotBlank()) {
                Text("Charts: $seriesInfo", style = MaterialTheme.typography.bodySmall, color = Mute)
            }

            Button(
                onClick = {
                    permissionLauncher.launch(HealthPermissions.requestSet(includeWrite = BuildConfig.DEBUG))
                },
                enabled = availability is HealthConnectAvailability.Available && !busy,
                modifier = Modifier.fillMaxWidth(),
            ) {
                Text("Grant Health Connect permissions")
            }

            Button(
                onClick = { scope.launch { refreshNow() } },
                enabled = hasRead && !busy,
                modifier = Modifier.fillMaxWidth(),
            ) {
                Text(if (busy) "Syncing…" else "Sync & push to watch")
            }

            if (BuildConfig.DEBUG) {
                OutlinedButton(
                    onClick = {
                        scope.launch {
                            busy = true
                            status = "Seeding…"
                            try {
                                if (!hasWrite) {
                                    status = "Need write permissions to seed"
                                    busy = false
                                    return@launch
                                }
                                repository.seedSampleData()
                                status = "Seeded sample data"
                                refreshNow()
                            } catch (e: Exception) {
                                status = e.message ?: "Seed failed"
                                busy = false
                            }
                        }
                    },
                    enabled = availability is HealthConnectAvailability.Available && !busy,
                    modifier = Modifier.fillMaxWidth(),
                ) {
                    Text("Seed emulator sample data")
                }
            }

            Box(modifier = Modifier.height(8.dp))

            Text("Live heart rate", fontWeight = FontWeight.Bold, color = Mute)
            Text(
                "Reads your strap directly over Bluetooth and streams it to the watch. " +
                    "Keeps a notification up while it runs.",
                style = MaterialTheme.typography.bodySmall,
                color = Mute,
            )
            Text(
                when (live.state) {
                    LiveHr.State.Idle -> "Off"
                    LiveHr.State.Scanning -> "Looking for a strap…"
                    LiveHr.State.Connecting -> "Connecting to ${live.deviceName.ifBlank { "strap" }}…"
                    LiveHr.State.Streaming -> if (live.bpm > 0) {
                        "${live.bpm} bpm · min ${live.min} · max ${live.max}"
                    } else {
                        "Connected, waiting for a pulse"
                    }
                    LiveHr.State.Retry -> "Reconnecting… ${live.detail}"
                    LiveHr.State.Failed -> "Failed: ${live.detail}"
                    else -> ""
                },
                color = if (live.isFresh) Teal else Ink,
            )
            if (live.batteryPercent > 0) {
                Text("Strap battery ${live.batteryPercent}%", color = Mute)
            }
            /* Turn a dead end into the next action. */
            if (live.detail.isNotBlank() && live.state != LiveHr.State.Streaming &&
                live.state != LiveHr.State.Idle
            ) {
                Text(live.detail, style = MaterialTheme.typography.bodySmall, color = Ink)
            }
            if (live.state == LiveHr.State.Retry || live.state == LiveHr.State.Failed ||
                live.state == LiveHr.State.Scanning
            ) {
                Text(
                    "Nothing connected. Check the monitor is awake and broadcasting, " +
                        "then tap Connect again. Android Bluetooth must be on, and the " +
                        "monitor must not already be connected to another app.",
                    style = MaterialTheme.typography.bodySmall,
                    color = Mute,
                )
            }
            if (live.state == LiveHr.State.Idle) {
                Text(
                    "Your monitor has to be broadcasting. On a Fitbit that is " +
                        "Settings > Broadcast Heart Rate (or Heart Rate on Equipment) " +
                        "on Charge/Track/Sense models. Chest straps usually broadcast " +
                        "as soon as they are wet and worn.",
                    style = MaterialTheme.typography.bodySmall,
                    color = Mute,
                )
                Button(
                    onClick = {
                        HealthGlanceApp.breadcrumb(
                            "Connect tapped sdk=${Build.VERSION.SDK_INT} " +
                                "ble=${hasBlePermissions()} notif=" +
                                (androidx.core.content.ContextCompat.checkSelfPermission(
                                    context,
                                    Manifest.permission.POST_NOTIFICATIONS,
                                ) == android.content.pm.PackageManager.PERMISSION_GRANTED),
                        )
                        try {
                            if (hasBlePermissions()) {
                                HeartRateService.start(context)
                            } else {
                                HealthGlanceApp.breadcrumb("asking for BLE permissions")
                                bleLauncher.launch(blePermissions())
                            }
                        } catch (e: Throwable) {
                            HealthGlanceApp.record("Connect tap threw: ${HealthGlanceApp.stack(e)}")
                            status = "Connect failed: ${e.javaClass.simpleName}: ${e.message}"
                            LiveHr.set(
                                LiveHr.Snapshot(
                                    state = LiveHr.State.Failed,
                                    detail = "Connect failed: ${e.javaClass.simpleName}",
                                ),
                            )
                        }
                    },
                    modifier = Modifier.fillMaxWidth(),
                ) {
                    Text("Connect heart-rate strap")
                }
            } else {
                OutlinedButton(
                    onClick = { HeartRateService.stop(context) },
                    modifier = Modifier.fillMaxWidth(),
                ) {
                    Text("Stop live heart rate")
                }
            }

            val lastCrash = remember { HealthGlanceApp.lastCrash() }
            if (lastCrash.isNotBlank()) {
                Text("Last crash", fontWeight = FontWeight.Bold, color = Mute)
                Text(
                    lastCrash,
                    style = MaterialTheme.typography.bodySmall,
                    color = Ink,
                )
                OutlinedButton(
                    onClick = {
                        HealthGlanceApp.clearCrash()
                        status = "Crash log cleared"
                    },
                    modifier = Modifier.fillMaxWidth(),
                ) {
                    Text("Clear crash log")
                }
            }
            val trail = remember { HealthGlanceApp.trail() }
            if (trail.isNotBlank()) {
                Text("Live HR trail", fontWeight = FontWeight.Bold, color = Mute)
                Text(trail, style = MaterialTheme.typography.bodySmall, color = Mute)
            }

            Box(modifier = Modifier.height(8.dp))

            val current = stats
            if (current != null) {
                Text("Today", fontWeight = FontWeight.Bold, color = Mute)
                Text("${current.activity.steps} steps", style = MaterialTheme.typography.headlineSmall)
                Text(
                    "Dist ${current.activity.distanceKm} km · " +
                        "Cal ${current.activity.calories} · " +
                        "Active ${current.activity.activeMinutes} m",
                    color = Mute,
                )
                Text(
                    "Resting HR ${current.heart.restingHr} · " +
                        "Min ${current.heart.minHr} · Max ${current.heart.maxHr}",
                    color = Mute,
                )

                Box(modifier = Modifier.height(8.dp))
                Text("Last night", fontWeight = FontWeight.Bold, color = Mute)
                Text(
                    "${current.sleep.minutesAsleep / 60}h ${current.sleep.minutesAsleep % 60}m asleep",
                    style = MaterialTheme.typography.headlineSmall,
                )
                Text(
                    "Deep ${current.sleep.stages.deep} · Light ${current.sleep.stages.light} · " +
                        "REM ${current.sleep.stages.rem} · Awake ${current.sleep.stages.awake}",
                    color = Mute,
                )

                Box(modifier = Modifier.height(8.dp))
                Text("Last workout", fontWeight = FontWeight.Bold, color = Mute)
                val wo = current.workout
                if (wo.durationSec > 0) {
                    val mins = wo.durationSec / 60
                    val secs = wo.durationSec % 60
                    Text(
                        "${wo.title.ifBlank { "WORKOUT" }} · " +
                            "${mins}m ${secs}s · ${wo.calories} cal",
                        color = Ink,
                    )
                } else {
                    Text("None in lookback", color = Mute)
                }

                Text("Source: ${current.source}", color = Mute)
                if (current.sleep.minutesAsleep == 0) {
                    Text(
                        "No sleep sessions in Health Connect. In your tracker app, allow it to write sleep to Health Connect.",
                        color = Mute,
                    )
                }
                if (current.updatedAt.isNotBlank()) {
                    Text("Updated: ${current.updatedAt}", color = Mute)
                }
            }
        }
    }
}

private fun availabilityLabel(availability: HealthConnectAvailability): String {
    return when (availability) {
        HealthConnectAvailability.Available -> "available"
        HealthConnectAvailability.UpdateRequired -> "needs install/update"
        HealthConnectAvailability.Unavailable -> "unavailable"
    }
}
