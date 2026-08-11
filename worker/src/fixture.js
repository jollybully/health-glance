/** Fixture payload matching the watch contract — used when USE_MOCK=true */
export function fixtureStats(stepGoal = 10000) {
  return {
    updatedAt: new Date().toISOString(),
    source: "mock",
    activity: {
      steps: 6421,
      stepGoal,
      distanceKm: 4.8,
      calories: 1820,
      activeMinutes: 42,
      restingHr: 58,
    },
    sleep: {
      start: "2026-07-17T23:12:00Z",
      end: "2026-07-18T06:45:00Z",
      durationMin: 453,
      minutesAsleep: 435,
      stages: {
        deep: 72,
        light: 210,
        rem: 95,
        awake: 18,
      },
    },
  };
}
