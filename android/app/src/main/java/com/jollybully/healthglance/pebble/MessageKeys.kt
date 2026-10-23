package com.jollybully.healthglance.pebble

/**
 * Integer AppMessage keys from pebble/build/js/message_keys.json.
 * Keep in sync with pebble/package.json messageKeys order (append-only).
 */
object MessageKeys {
    const val STATS_REQ: UInt = 10000u
    const val STATS_JSON: UInt = 10001u
    const val STATS_ERR: UInt = 10002u
    const val STATS_URL: UInt = 10003u
    const val STEPS: UInt = 10004u
    const val STEP_GOAL: UInt = 10005u
    const val DIST_M: UInt = 10006u
    const val CALORIES: UInt = 10007u
    const val ACTIVE_MIN: UInt = 10008u
    const val RESTING_HR: UInt = 10009u
    const val SLEEP_MIN: UInt = 10010u
    const val DEEP: UInt = 10011u
    const val LIGHT: UInt = 10012u
    const val REM: UInt = 10013u
    const val AWAKE: UInt = 10014u
    const val SOURCE: UInt = 10015u
    const val UPDATED: UInt = 10016u
    const val SLEEP_START: UInt = 10017u
    const val SLEEP_END: UInt = 10018u
    const val HR_MIN: UInt = 10019u
    const val HR_MAX: UInt = 10020u
    const val ZONE_LOW: UInt = 10021u
    const val ZONE_MID: UInt = 10022u
    const val ZONE_HIGH: UInt = 10023u
    const val STEPS_WEEK: UInt = 10024u
    const val SLEEP_WEEK: UInt = 10025u
    const val STEPS_HOUR: UInt = 10026u
    const val WO_DUR_S: UInt = 10027u
    const val WO_CAL: UInt = 10028u
    const val WO_AVG_HR: UInt = 10029u
    const val WO_PEAK_HR: UInt = 10030u
    const val WO_DIST_M: UInt = 10031u
    const val WO_START: UInt = 10032u
    const val WO_END: UInt = 10033u
    const val WO_TITLE: UInt = 10034u
    const val WO_ZONE_LOW: UInt = 10035u
    const val WO_ZONE_MID: UInt = 10036u
    const val WO_ZONE_HIGH: UInt = 10037u
    const val FLOORS: UInt = 10038u
    const val SPO2: UInt = 10039u
    const val HRV_MS: UInt = 10040u
    /* Live heart rate from a BLE strap via this companion (append-only). */
    const val HR_LIVE: UInt = 10041u
    const val HR_LIVE_TS: UInt = 10042u
    const val HR_LIVE_BATT: UInt = 10043u
    const val HR_LIVE_STATE: UInt = 10044u
    const val HR_LIVE_MIN: UInt = 10045u
    const val HR_LIVE_MAX: UInt = 10046u
    /* Detail-screen series, one byte-array key each (see SeriesCodec). */
    /* Totals for the day the watch asked for: a past day needs its own numbers,
     * not the peak hour and not today's. */

    val WATCH_APP_UUID: java.util.UUID =
        java.util.UUID.fromString("ced3f81d-aaae-4ee4-a49c-885a247644ec")
}
