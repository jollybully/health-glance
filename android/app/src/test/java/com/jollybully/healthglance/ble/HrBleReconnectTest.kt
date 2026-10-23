package com.jollybully.healthglance.ble

import org.junit.Assert.assertEquals
import org.junit.Test

class HrBleReconnectTest {

    @Test
    fun `base delay when no failures`() {
        assertEquals(HrBleReconnect.BASE_DELAY_MS, HrBleReconnect.delayMs(0))
    }

    @Test
    fun `exponential backoff caps at max`() {
        assertEquals(30_000L, HrBleReconnect.delayMs(1))
        assertEquals(60_000L, HrBleReconnect.delayMs(2))
        assertEquals(60_000L, HrBleReconnect.delayMs(10))
    }
}
