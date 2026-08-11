package com.elliotseeds.healthglance.pebble

/** Wire packing for week/hourly series under the 512-byte AppMessage budget. */
object StatsPacking {
    /** 7× UInt16 little-endian (oldest → today). Caps at 65535. */
    fun packUInt16LE(values: List<Int>): ByteArray {
        require(values.size == 7) { "expected 7 values, got ${values.size}" }
        val out = ByteArray(14)
        for (i in 0 until 7) {
            val v = values[i].coerceIn(0, 65_535)
            out[i * 2] = (v and 0xff).toByte()
            out[i * 2 + 1] = ((v shr 8) and 0xff).toByte()
        }
        return out
    }

    fun unpackUInt16LE(bytes: ByteArray): List<Int> {
        val out = MutableList(7) { 0 }
        val n = minOf(7, bytes.size / 2)
        for (i in 0 until n) {
            val lo = bytes[i * 2].toInt() and 0xff
            val hi = bytes[i * 2 + 1].toInt() and 0xff
            out[i] = lo or (hi shl 8)
        }
        return out
    }

    /** 24× UInt8: min(255, steps / 40) per hour of local day. */
    fun packHourlySteps(stepsPerHour: List<Int>): ByteArray {
        require(stepsPerHour.size == 24) { "expected 24 hours, got ${stepsPerHour.size}" }
        return ByteArray(24) { i ->
            (stepsPerHour[i].coerceAtLeast(0) / 40).coerceAtMost(255).toByte()
        }
    }

    fun unpackHourlySteps(bytes: ByteArray): List<Int> {
        val out = MutableList(24) { 0 }
        val n = minOf(24, bytes.size)
        for (i in 0 until n) {
            out[i] = (bytes[i].toInt() and 0xff) * 40
        }
        return out
    }
}
