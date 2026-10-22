package com.jollybully.healthglance.data

import androidx.health.connect.client.records.ExerciseSessionRecord

object WorkoutTitles {
    private val TIME_TITLE = Regex("""^\d{1,2}:\d{2}""")

    fun from(title: String?, exerciseType: Int): String {
        val named = title?.trim().orEmpty()
        if (usableTitle(named)) return asciiUpper(named).take(16)
        return typeLabel(exerciseType)
    }

    fun usableTitle(named: String): Boolean {
        if (named.isBlank()) return false
        if (TIME_TITLE.containsMatchIn(named)) return false
        if (named.any { it.code !in 32..126 }) return false
        return named.any { it.isLetter() }
    }

    fun asciiUpper(value: String): String =
        value.map { ch ->
            if (ch.code in 32..126) ch.uppercaseChar() else ' '
        }.joinToString("").trim()

    fun typeLabel(exerciseType: Int): String = when (exerciseType) {
        ExerciseSessionRecord.EXERCISE_TYPE_RUNNING,
        ExerciseSessionRecord.EXERCISE_TYPE_RUNNING_TREADMILL,
        -> "LAST RUN"
        ExerciseSessionRecord.EXERCISE_TYPE_WALKING,
        -> "LAST WALK"
        ExerciseSessionRecord.EXERCISE_TYPE_HIKING,
        -> "LAST HIKE"
        ExerciseSessionRecord.EXERCISE_TYPE_BIKING,
        ExerciseSessionRecord.EXERCISE_TYPE_BIKING_STATIONARY,
        -> "LAST RIDE"
        ExerciseSessionRecord.EXERCISE_TYPE_SWIMMING_OPEN_WATER,
        ExerciseSessionRecord.EXERCISE_TYPE_SWIMMING_POOL,
        -> "LAST SWIM"
        ExerciseSessionRecord.EXERCISE_TYPE_STRENGTH_TRAINING,
        ExerciseSessionRecord.EXERCISE_TYPE_WEIGHTLIFTING,
        -> "LAST LIFT"
        ExerciseSessionRecord.EXERCISE_TYPE_HIGH_INTENSITY_INTERVAL_TRAINING,
        -> "LAST HIIT"
        ExerciseSessionRecord.EXERCISE_TYPE_YOGA,
        -> "LAST YOGA"
        ExerciseSessionRecord.EXERCISE_TYPE_PILATES,
        -> "PILATES"
        ExerciseSessionRecord.EXERCISE_TYPE_ELLIPTICAL,
        -> "ELLIPTICAL"
        ExerciseSessionRecord.EXERCISE_TYPE_ROWING,
        ExerciseSessionRecord.EXERCISE_TYPE_ROWING_MACHINE,
        -> "LAST ROW"
        ExerciseSessionRecord.EXERCISE_TYPE_STAIR_CLIMBING,
        ExerciseSessionRecord.EXERCISE_TYPE_STAIR_CLIMBING_MACHINE,
        -> "STAIRS"
        ExerciseSessionRecord.EXERCISE_TYPE_DANCING,
        -> "DANCING"
        ExerciseSessionRecord.EXERCISE_TYPE_MARTIAL_ARTS,
        ExerciseSessionRecord.EXERCISE_TYPE_BOXING,
        -> "LAST FIGHT"
        ExerciseSessionRecord.EXERCISE_TYPE_SOCCER,
        -> "SOCCER"
        ExerciseSessionRecord.EXERCISE_TYPE_BASKETBALL,
        -> "HOOPS"
        ExerciseSessionRecord.EXERCISE_TYPE_TENNIS,
        ExerciseSessionRecord.EXERCISE_TYPE_TABLE_TENNIS,
        -> "TENNIS"
        ExerciseSessionRecord.EXERCISE_TYPE_GOLF,
        -> "GOLF"
        ExerciseSessionRecord.EXERCISE_TYPE_SURFING,
        -> "SURF"
        ExerciseSessionRecord.EXERCISE_TYPE_SKIING,
        ExerciseSessionRecord.EXERCISE_TYPE_SNOWBOARDING,
        -> "SNOW"
        else -> "WORKOUT"
    }
}
