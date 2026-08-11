package com.elliotseeds.healthglance.debug

import android.os.Bundle
import android.util.Log
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.elliotseeds.healthglance.HealthGlanceTheme
import com.elliotseeds.healthglance.data.HealthConnectRepository

/**
 * Agent entrypoint (debug builds only):
 *   adb shell am start -a com.elliotseeds.healthglance.action.SEED_HEALTH_DATA \
 *     -n com.elliotseeds.healthglance/.debug.SeedHealthDataActivity
 */
class SeedHealthDataActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val repository = HealthConnectRepository(this)
        setContent {
            HealthGlanceTheme {
                var message by remember { mutableStateOf("Seeding…") }
                LaunchedEffect(Unit) {
                    try {
                        if (!repository.hasWritePermissions()) {
                            message = "FAIL: write permissions not granted"
                            Log.e(TAG, message)
                            return@LaunchedEffect
                        }
                        repository.seedSampleData()
                        val stats = repository.loadStats()
                        message =
                            "OK steps=${stats.activity.steps} sleep=${stats.sleep.minutesAsleep}"
                        Log.i(TAG, message)
                    } catch (e: Exception) {
                        message = "FAIL: ${e.message}"
                        Log.e(TAG, "seed failed", e)
                    }
                }
                Column(
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(24.dp),
                ) {
                    Text(message)
                }
            }
        }
    }

    companion object {
        private const val TAG = "HealthGlance"
    }
}
