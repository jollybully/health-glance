package com.jollybully.healthglance

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

class PermissionsRationaleActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            HealthGlanceTheme {
                Column(
                    modifier = Modifier
                        .fillMaxSize()
                        .verticalScroll(rememberScrollState())
                        .padding(24.dp),
                ) {
                    Text(
                        text = "Why Health Glance needs Health Connect",
                        style = MaterialTheme.typography.headlineSmall,
                    )
                    Text(
                        modifier = Modifier.padding(top = 12.dp),
                        text = "Health Glance is a phone companion for your Pebble Time 2. " +
                            "It reads selected health data from Health Connect on this device and " +
                            "sends it over Bluetooth (via the Core Pebble app) to your watch.",
                        style = MaterialTheme.typography.bodyMedium,
                    )
                    Text(
                        modifier = Modifier.padding(top = 16.dp),
                        text = "Data we read",
                        style = MaterialTheme.typography.titleMedium,
                    )
                    Text(
                        modifier = Modifier.padding(top = 8.dp),
                        text = "• Steps\n" +
                            "• Distance\n" +
                            "• Calories (total and active)\n" +
                            "• Heart rate\n" +
                            "• Sleep (including stages)\n" +
                            "• Exercise sessions (for active minutes)\n" +
                            "• Background read (so stats can refresh while the app is closed)",
                        style = MaterialTheme.typography.bodyMedium,
                    )
                    Text(
                        modifier = Modifier.padding(top = 16.dp),
                        text = "Where data goes",
                        style = MaterialTheme.typography.titleMedium,
                    )
                    Text(
                        modifier = Modifier.padding(top = 8.dp),
                        text = "Data stays on your phone. It is only sent to your paired Pebble watch. " +
                            "Nothing is uploaded to our servers, and we do not sell or share your health data.",
                        style = MaterialTheme.typography.bodyMedium,
                    )
                }
            }
        }
    }
}
