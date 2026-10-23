package com.jollybully.healthglance.ble

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The strap's measurement characteristic is the one part of the BLE path that can
 * be proved without hardware, so the flag permutations are all covered here:
 * 8- vs 16-bit bpm, sensor contact, energy expended, RR intervals, and short or
 * nonsense packets that must be rejected rather than shown as a heart rate.
 */
class HeartRateMeasurementTest {

    @Test
    fun `uint8 bpm without extras`() {
        val m = HeartRateMeasurement.parse(byteArrayOf(0x00, 72))
        assertEquals(72, m?.bpm)
        assertNull("contact not supported", m?.sensorContact)
        assertNull(m?.energyExpendedKj)
        assertTrue(m?.rrIntervalsMs!!.isEmpty())
    }

    @Test
    fun `uint16 bpm when flag bit 0 is set`() {
        // 180 bpm = 0x00B4 little endian; straps that always send 16 bits still work
        val m = HeartRateMeasurement.parse(byteArrayOf(0x01, 0xB4.toByte(), 0x00))
        assertEquals(180, m?.bpm)
        // and a wide encoding of a normal rate agrees with the narrow one
        assertEquals(72, HeartRateMeasurement.parse(byteArrayOf(0x01, 0x48, 0x00))?.bpm)
    }

    @Test
    fun `sensor contact states`() {
        // bits 1-2: 2 = supported, no contact; 3 = contact
        assertEquals(false, HeartRateMeasurement.parse(byteArrayOf(0x04, 61))?.sensorContact)
        assertEquals(true, HeartRateMeasurement.parse(byteArrayOf(0x06, 61))?.sensorContact)
        assertNull(HeartRateMeasurement.parse(byteArrayOf(0x00, 61))?.sensorContact)
    }

    @Test
    fun `energy expended is read when present`() {
        // flags 0x08, bpm 80, energy 0x01F4 = 500 kJ
        val m = HeartRateMeasurement.parse(byteArrayOf(0x08, 80, 0xF4.toByte(), 0x01))
        assertEquals(80, m?.bpm)
        assertEquals(500, m?.energyExpendedKj)
    }

    @Test
    fun `rr intervals are converted from 1-1024s to ms`() {
        // flags 0x10, bpm 60, two RR values: 1024 (=1000 ms) and 512 (=500 ms)
        val m = HeartRateMeasurement.parse(
            byteArrayOf(0x10, 60, 0x00, 0x04, 0x00, 0x02),
        )
        assertEquals(60, m?.bpm)
        assertEquals(listOf(1000, 500), m?.rrIntervalsMs)
    }

    @Test
    fun `16bit bpm with contact and rr together`() {
        val m = HeartRateMeasurement.parse(
            byteArrayOf(
                (0x01 or 0x06 or 0x10).toByte(), // wide + contact + rr
                0xB4.toByte(), 0x00, // 180 bpm
                0x00, 0x04, // 1000 ms
            ),
        )
        assertEquals(180, m?.bpm)
        assertEquals(true, m?.sensorContact)
        assertEquals(listOf(1000), m?.rrIntervalsMs)
    }

    @Test
    fun `short packets are rejected`() {
        assertNull(HeartRateMeasurement.parse(byteArrayOf()))
        assertNull(HeartRateMeasurement.parse(byteArrayOf(0x00)))
        // wide flag set but only one value byte
        assertNull(HeartRateMeasurement.parse(byteArrayOf(0x01, 0x2C.toByte())))
    }

    @Test
    fun `zero and implausible rates are rejected`() {
        assertNull(HeartRateMeasurement.parse(byteArrayOf(0x00, 0)))
        // above the plausible ceiling, in both encodings
        assertNull(HeartRateMeasurement.parse(byteArrayOf(0x01, 0xE8.toByte(), 0x03))) // 1000
        assertNull(HeartRateMeasurement.parse(byteArrayOf(0x00, 0xFB.toByte()))) // 251 narrow
    }

    @Test
    fun `parse reject reasons`() {
        assertEquals("empty_packet", HeartRateMeasurement.parseRejectReason(byteArrayOf()))
        assertEquals("too_short", HeartRateMeasurement.parseRejectReason(byteArrayOf(0x00)))
        assertEquals("wide_bpm_incomplete", HeartRateMeasurement.parseRejectReason(byteArrayOf(0x01, 0x2C)))
        assertEquals("bpm_zero", HeartRateMeasurement.parseRejectReason(byteArrayOf(0x00, 0)))
        assertEquals(null, HeartRateMeasurement.parseRejectReason(byteArrayOf(0x00, 72)))
    }

    @Test
    fun `truncated rr block keeps the bpm`() {
        val m = HeartRateMeasurement.parse(byteArrayOf(0x10, 61, 0x00))
        assertEquals(61, m?.bpm)
        assertFalse("no complete RR pair", m!!.rrIntervalsMs.isNotEmpty())
    }
}
