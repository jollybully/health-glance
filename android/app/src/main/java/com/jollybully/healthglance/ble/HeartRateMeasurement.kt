package com.jollybully.healthglance.ble

/**
 * One Heart Rate Measurement (Bluetooth SIG 0x2A37) decoded.
 *
 * Layout: flags byte, then the heart rate value whose width is chosen by flag
 * bit 0 (0 = uint8, 1 = uint16 little endian), then optional energy expended
 * (bit 3) and optional RR intervals (bit 4). Sensor contact is in bits 1-2:
 * 0/1 = not supported, 2 = supported but no contact, 3 = contact.
 *
 * Pure so it can be unit-tested without a strap: see HeartRateMeasurementTest.
 */
data class HeartRateMeasurement(
    val bpm: Int,
    val sensorContact: Boolean?,
    val contactSupported: Boolean,
    val energyExpendedKj: Int?,
    val rrIntervalsMs: List<Int>,
) {
    companion object {
        const val MAX_PLAUSIBLE_BPM = 250

        fun parse(data: ByteArray): HeartRateMeasurement? {
            if (data.size < 2) return null
            val flags = data[0].toInt() and 0xFF
            val wide = flags and 0x01 != 0
            var index = 1
            val bpm: Int
            if (wide) {
                if (data.size < 3) return null
                bpm = (data[1].toInt() and 0xFF) or ((data[2].toInt() and 0xFF) shl 8)
                index = 3
            } else {
                bpm = data[1].toInt() and 0xFF
                index = 2
            }
            // Straps may encode 8 or 16 bits; either way anything outside this is
            // noise, not a pulse, and must not be shown as one.
            if (bpm <= 0 || bpm > MAX_PLAUSIBLE_BPM) return null

            val contactBits = (flags shr 1) and 0x03
            val contactSupported = contactBits >= 2
            val sensorContact = when (contactBits) {
                2 -> false
                3 -> true
                else -> null
            }

            var energy: Int? = null
            if (flags and 0x08 != 0) {
                if (data.size < index + 2) return HeartRateMeasurement(bpm, sensorContact, contactSupported, null, emptyList())
                energy = (data[index].toInt() and 0xFF) or ((data[index + 1].toInt() and 0xFF) shl 8)
                index += 2
            }

            val rr = mutableListOf<Int>()
            if (flags and 0x10 != 0) {
                while (index + 1 < data.size) {
                    val raw = (data[index].toInt() and 0xFF) or ((data[index + 1].toInt() and 0xFF) shl 8)
                    // RR is in 1/1024 s units
                    rr.add((raw * 1000) / 1024)
                    index += 2
                }
            }
            return HeartRateMeasurement(bpm, sensorContact, contactSupported, energy, rr)
        }
    }
}
