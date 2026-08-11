package com.elliotseeds.healthglance.sync

import com.elliotseeds.healthglance.data.HealthConnectAvailability
import com.elliotseeds.healthglance.pebble.SendOutcome

/**
 * Human-readable sync status for the companion UI and watch STATS_ERR (≤28 chars when truncated).
 */
object SyncStatus {
    fun fromAvailability(availability: HealthConnectAvailability): String {
        return when (availability) {
            HealthConnectAvailability.Available -> "Ready"
            HealthConnectAvailability.UpdateRequired -> "Health Connect needs update"
            HealthConnectAvailability.Unavailable -> "Health Connect unavailable"
        }
    }

    fun needPermissions(): String = "Need Health Connect permissions"

    fun reading(): String = "Reading Health Connect…"

    fun fromSendOutcome(outcome: SendOutcome): String {
        return when (outcome) {
            is SendOutcome.Ok -> if (outcome.results.isEmpty()) {
                "Loaded (watch app closed)"
            } else {
                "Pushed to watch"
            }
            SendOutcome.NoPebbleApp -> "Core Pebble not reachable"
            SendOutcome.NoConnectedWatches -> "No connected watch"
            is SendOutcome.Failed -> "Send failed: ${outcome.detail}".take(48)
        }
    }

    fun watchError(message: String): String = message.take(28)

    fun failed(detail: String?): String = (detail ?: "Refresh failed").take(48)
}
