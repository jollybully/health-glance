package com.jollybully.healthglance.sync

import android.content.Context
import android.util.Log
import androidx.work.CoroutineWorker
import androidx.work.ExistingPeriodicWorkPolicy
import androidx.work.PeriodicWorkRequestBuilder
import androidx.work.WorkManager
import androidx.work.WorkerParameters
import java.util.concurrent.TimeUnit

class StatsRefreshWorker(
    appContext: Context,
    params: WorkerParameters,
) : CoroutineWorker(appContext, params) {
    override suspend fun doWork(): Result {
        val sync = StatsSync(applicationContext)
        return try {
            val result = sync.sync()
            Log.i(TAG, "periodic sync: ${result.status}")
            Result.success()
        } catch (e: Exception) {
            Log.e(TAG, "periodic sync failed", e)
            Result.retry()
        } finally {
            sync.close()
        }
    }

    companion object {
        private const val TAG = "HealthGlance"
        const val UNIQUE_NAME = "health_glance_stats_refresh"

        fun schedule(context: Context) {
            val request = PeriodicWorkRequestBuilder<StatsRefreshWorker>(30, TimeUnit.MINUTES)
                .build()
            WorkManager.getInstance(context).enqueueUniquePeriodicWork(
                UNIQUE_NAME,
                ExistingPeriodicWorkPolicy.KEEP,
                request,
            )
            Log.i(TAG, "scheduled periodic stats refresh")
        }

        fun cancel(context: Context) {
            WorkManager.getInstance(context).cancelUniqueWork(UNIQUE_NAME)
            Log.i(TAG, "cancelled periodic stats refresh")
        }
    }
}
