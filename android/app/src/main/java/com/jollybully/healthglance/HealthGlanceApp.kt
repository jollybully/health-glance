package com.jollybully.healthglance

import android.app.Application
import android.content.Context
import android.util.Log
import java.io.PrintWriter
import java.io.StringWriter
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Crash and breadcrumb recorder.
 *
 * The live heart-rate path can take the whole process down (a foreground service
 * that fails, a BLE call that throws on a binder thread). Without a recorder the
 * only symptom is "the app quits", which is not diagnosable. This writes the
 * stack trace plus the last few steps to SharedPreferences so the UI can show it
 * on the next launch.
 */
class HealthGlanceApp : Application() {

    override fun onCreate() {
        super.onCreate()
        instance = this
        val previous = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { thread, error ->
            try {
                record("crash on ${thread.name}: ${error.javaClass.name}: ${error.message}\n${stack(error)}")
            } catch (ignored: Throwable) {
                Log.e(TAG, "could not record the crash", ignored)
            }
            previous?.uncaughtException(thread, error)
        }
        breadcrumb("app started")
    }

    companion object {
        private const val TAG = "HealthGlance"
        private const val PREFS = "health_glance_diag"
        private const val KEY_CRASH = "last_crash"
        private const val KEY_TRAIL = "trail"

        @Volatile
        private var instance: HealthGlanceApp? = null

        private fun prefs(): android.content.SharedPreferences? =
            instance?.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

        /** One line per step, kept short, so a silent stop shows where it stopped. */
        fun breadcrumb(step: String) {
            val p = prefs() ?: return
            val stamp = SimpleDateFormat("HH:mm:ss", Locale.UK).format(Date())
            val trail = p.getString(KEY_TRAIL, "") ?: ""
            val lines = (trail.split('\n').filter { it.isNotBlank() } + "$stamp $step").takeLast(16)
            p.edit().putString(KEY_TRAIL, lines.joinToString("\n")).apply()
        }

        fun record(detail: String) {
            val p = prefs() ?: return
            val stamp = SimpleDateFormat("dd/MM HH:mm:ss", Locale.UK).format(Date())
            p.edit().putString(KEY_CRASH, "$stamp\n$detail").apply()
            Log.e(TAG, "recorded: $detail")
        }

        fun lastCrash(): String = prefs()?.getString(KEY_CRASH, "") ?: ""

        fun trail(): String = prefs()?.getString(KEY_TRAIL, "") ?: ""

        fun clearCrash() {
            prefs()?.edit()?.remove(KEY_CRASH)?.apply()
        }

        fun stack(error: Throwable): String {
            val sw = StringWriter()
            error.printStackTrace(PrintWriter(sw))
            /* the top frames are what matter; the rest is noise on a phone screen */
            return sw.toString().lines().filter { it.isNotBlank() }.take(14).joinToString("\n")
        }
    }
}
