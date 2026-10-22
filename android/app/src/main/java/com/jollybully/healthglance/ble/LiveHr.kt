package com.jollybully.healthglance.ble

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

/**
 * Live heart-rate state shared between the BLE service and the companion UI.
 *
 * Deliberately separate from the cached GlanceStats: a live sample must never be
 * written to the stats cache, or a value from last night's session would be
 * served back tomorrow as if it were live.
 */
object LiveHr {
    /** Value pushed to the watch as HR_LIVE_STATE; keep in step with the watch app. */
    enum class State(val code: UInt) {
        Idle(0u),
        Scanning(1u),
        Connecting(2u),
        Streaming(3u),
        Retry(4u),
        Failed(5u),
    }

    data class Snapshot(
        val state: State = State.Idle,
        val bpm: Int = 0,
        val min: Int = 0,
        val max: Int = 0,
        val atMillis: Long = 0L,
        val batteryPercent: Int = 0,
        val deviceName: String = "",
        val detail: String = "",
        val publishedAtMillis: Long = 0L,
    ) {
        val isFresh: Boolean
            get() = state == State.Streaming && bpm > 0
    }

    private val _state = MutableStateFlow(Snapshot())

    val state: StateFlow<Snapshot> = _state

    fun current(): Snapshot = _state.value

    fun set(snapshot: Snapshot) {
        _state.value = snapshot
    }

    fun reset(detail: String = "") {
        _state.value = Snapshot(state = State.Idle, detail = detail)
    }
}
