package com.jollybully.healthglance.ble

/**
 * Backoff for live-HR reconnect loops (pure, unit-testable).
 */
internal object HrBleReconnect {
    const val BASE_DELAY_MS = 15_000L
    const val MAX_DELAY_MS = 60_000L

    /** @param consecutiveFailures failures since the last successful HR sample (0 → base delay). */
    fun delayMs(consecutiveFailures: Int): Long {
        if (consecutiveFailures <= 0) return BASE_DELAY_MS
        val shift = consecutiveFailures.coerceAtMost(4)
        return (BASE_DELAY_MS * (1L shl shift)).coerceAtMost(MAX_DELAY_MS)
    }
}
