package com.jollybully.healthglance.data

import androidx.health.connect.client.records.ExerciseSessionRecord
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class WorkoutTitlesTest {
    @Test
    fun rejectsClockTimeTitles() {
        assertFalse(WorkoutTitles.usableTitle("4:35"))
        assertFalse(WorkoutTitles.usableTitle("14:20"))
        assertFalse(WorkoutTitles.usableTitle(""))
        assertFalse(WorkoutTitles.usableTitle("💪"))
        assertTrue(WorkoutTitles.usableTitle("Morning run"))
    }

    @Test
    fun mapsTypeWhenTitleIsATime() {
        assertEquals(
            "LAST WALK",
            WorkoutTitles.from("4:35", ExerciseSessionRecord.EXERCISE_TYPE_WALKING),
        )
        assertEquals(
            "LAST RUN",
            WorkoutTitles.from("  ", ExerciseSessionRecord.EXERCISE_TYPE_RUNNING),
        )
        assertEquals(
            "LAST LIFT",
            WorkoutTitles.from(null, ExerciseSessionRecord.EXERCISE_TYPE_STRENGTH_TRAINING),
        )
        assertEquals(
            "MORNING RUN",
            WorkoutTitles.from("Morning run", ExerciseSessionRecord.EXERCISE_TYPE_WALKING),
        )
        assertEquals(
            "WORKOUT",
            WorkoutTitles.from("4:35", ExerciseSessionRecord.EXERCISE_TYPE_OTHER_WORKOUT),
        )
    }
}
