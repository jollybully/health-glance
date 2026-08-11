package com.elliotseeds.healthglance

import android.content.Intent
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
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
import com.elliotseeds.healthglance.data.GlanceStats
import com.elliotseeds.healthglance.data.HealthConnectAvailability
import com.elliotseeds.healthglance.data.HealthConnectRepository
import com.elliotseeds.healthglance.data.HealthPermissions
import com.elliotseeds.healthglance.sync.StatsRefreshWorker
import com.elliotseeds.healthglance.sync.StatsSync
import com.elliotseeds.healthglance.sync.SyncStatus
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
                if (current.activity.restingHr > 0) {
                    Text("Resting HR ${current.activity.restingHr} bpm", color = Mute)
                }

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
                Text("Source: ${current.source}", color = Mute)
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
