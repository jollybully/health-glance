const SCOPES = [
  "https://www.googleapis.com/auth/googlehealth.activity_and_fitness.readonly",
  "https://www.googleapis.com/auth/googlehealth.sleep.readonly",
  "https://www.googleapis.com/auth/googlehealth.health_metrics_and_measurements.readonly",
].join(" ");

const TOKEN_KEY = "google_oauth";
const CACHE_KEY = "stats_cache";
const CACHE_TTL_MS = 5 * 60 * 1000;

export function authUrl(env, state) {
  const params = new URLSearchParams({
    client_id: env.GOOGLE_CLIENT_ID,
    redirect_uri: env.OAUTH_REDIRECT_URI,
    response_type: "code",
    access_type: "offline",
    prompt: "consent",
    scope: SCOPES,
    state,
  });
  return `https://accounts.google.com/o/oauth2/v2/auth?${params}`;
}

export async function exchangeCode(env, code) {
  const body = new URLSearchParams({
    code,
    client_id: env.GOOGLE_CLIENT_ID,
    client_secret: env.GOOGLE_CLIENT_SECRET,
    redirect_uri: env.OAUTH_REDIRECT_URI,
    grant_type: "authorization_code",
  });
  const res = await fetch("https://oauth2.googleapis.com/token", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body,
  });
  const data = await res.json();
  if (!res.ok) {
    throw new Error(data.error_description || data.error || "token exchange failed");
  }
  await env.TOKENS.put(
    TOKEN_KEY,
    JSON.stringify({
      refresh_token: data.refresh_token,
      access_token: data.access_token,
      expires_at: Date.now() + (data.expires_in - 60) * 1000,
    }),
  );
  return data;
}

async function getAccessToken(env) {
  const raw = await env.TOKENS.get(TOKEN_KEY);
  if (!raw) throw new Error("Not authorized — visit /auth first");
  const stored = JSON.parse(raw);
  if (stored.access_token && stored.expires_at > Date.now()) {
    return stored.access_token;
  }
  if (!stored.refresh_token) throw new Error("Missing refresh token — re-run /auth");

  const body = new URLSearchParams({
    client_id: env.GOOGLE_CLIENT_ID,
    client_secret: env.GOOGLE_CLIENT_SECRET,
    refresh_token: stored.refresh_token,
    grant_type: "refresh_token",
  });
  const res = await fetch("https://oauth2.googleapis.com/token", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body,
  });
  const data = await res.json();
  if (!res.ok) {
    throw new Error(data.error_description || data.error || "token refresh failed");
  }
  const next = {
    refresh_token: data.refresh_token || stored.refresh_token,
    access_token: data.access_token,
    expires_at: Date.now() + (data.expires_in - 60) * 1000,
  };
  await env.TOKENS.put(TOKEN_KEY, JSON.stringify(next));
  return next.access_token;
}

function todayCivilRange() {
  const now = new Date();
  const y = now.getFullYear();
  const m = now.getMonth() + 1;
  const d = now.getDate();
  return {
    start: {
      date: { year: y, month: m, day: d },
      time: { hours: 0, minutes: 0, seconds: 0, nanos: 0 },
    },
    end: {
      date: { year: y, month: m, day: d },
      time: { hours: 23, minutes: 59, seconds: 59, nanos: 0 },
    },
  };
}

function num(v, fallback = 0) {
  if (v == null) return fallback;
  const n = typeof v === "string" ? parseFloat(v) : Number(v);
  return Number.isFinite(n) ? n : fallback;
}

async function dailyRollUp(token, dataType) {
  const res = await fetch(
    `https://health.googleapis.com/v4/users/me/dataTypes/${dataType}/dataPoints:dailyRollUp`,
    {
      method: "POST",
      headers: {
        Authorization: `Bearer ${token}`,
        Accept: "application/json",
        "Content-Type": "application/json",
      },
      body: JSON.stringify({ range: todayCivilRange(), windowSizeDays: 1 }),
    },
  );
  const data = await res.json();
  if (!res.ok) {
    throw new Error(`${dataType}: ${data.error?.message || JSON.stringify(data)}`);
  }
  return data.rollupDataPoints?.[0] || {};
}

async function listSleep(token) {
  // Last ~36h of sleep sessions ending today / yesterday
  const since = new Date(Date.now() - 36 * 60 * 60 * 1000);
  const iso = since.toISOString().slice(0, 19);
  const filter = encodeURIComponent(`sleep.interval.civil_end_time >= "${iso}"`);
  const res = await fetch(
    `https://health.googleapis.com/v4/users/me/dataTypes/sleep/dataPoints?filter=${filter}&pageSize=5`,
    {
      headers: {
        Authorization: `Bearer ${token}`,
        Accept: "application/json",
      },
    },
  );
  const data = await res.json();
  if (!res.ok) {
    throw new Error(`sleep: ${data.error?.message || JSON.stringify(data)}`);
  }
  const points = data.dataPoints || [];
  // Prefer main overnight sleep
  const main =
    points.find((p) => p.sleep?.metadata?.main) ||
    points.find((p) => p.sleep?.type === "STAGES") ||
    points[0];
  return main?.sleep || null;
}

async function listRestingHr(token) {
  const res = await fetch(
    "https://health.googleapis.com/v4/users/me/dataTypes/daily-resting-heart-rate/dataPoints?pageSize=3",
    {
      headers: {
        Authorization: `Bearer ${token}`,
        Accept: "application/json",
      },
    },
  );
  const data = await res.json();
  if (!res.ok) return null;
  const point = data.dataPoints?.[0];
  return point?.dailyRestingHeartRate || point?.daily_resting_heart_rate || null;
}

function parseSleep(sleep) {
  if (!sleep) {
    return {
      start: null,
      end: null,
      durationMin: 0,
      minutesAsleep: 0,
      stages: { deep: 0, light: 0, rem: 0, awake: 0 },
    };
  }
  const summary = sleep.summary || {};
  const stages = { deep: 0, light: 0, rem: 0, awake: 0 };
  for (const s of summary.stagesSummary || summary.stages_summary || []) {
    const t = (s.type || "").toUpperCase();
    const mins = num(s.minutes);
    if (t === "DEEP") stages.deep = mins;
    else if (t === "LIGHT") stages.light = mins;
    else if (t === "REM") stages.rem = mins;
    else if (t === "AWAKE" || t === "WAKE") stages.awake = mins;
  }
  const minutesAsleep = num(summary.minutesAsleep ?? summary.minutes_asleep);
  const period = num(summary.minutesInSleepPeriod ?? summary.minutes_in_sleep_period);
  const durationMin = period || minutesAsleep + stages.awake;
  return {
    start: sleep.interval?.startTime || sleep.interval?.start_time || null,
    end: sleep.interval?.endTime || sleep.interval?.end_time || null,
    durationMin,
    minutesAsleep,
    stages,
  };
}

function parseActiveMinutes(activeRoll) {
  const am = activeRoll.activeMinutes || activeRoll.active_minutes || {};
  if (am.activeMinutesSum != null || am.active_minutes_sum != null) {
    return num(am.activeMinutesSum ?? am.active_minutes_sum);
  }
  const byLevel =
    am.activeMinutesRollupByActivityLevel ||
    am.active_minutes_rollup_by_activity_level ||
    [];
  let sum = 0;
  for (const row of byLevel) {
    sum += num(row.activeMinutesSum ?? row.active_minutes_sum);
  }
  return sum;
}

export async function fetchLiveStats(env, { bypassCache = false } = {}) {
  if (!bypassCache) {
    const cached = await env.TOKENS.get(CACHE_KEY);
    if (cached) {
      const { expires, payload } = JSON.parse(cached);
      if (expires > Date.now()) return payload;
    }
  }

  const token = await getAccessToken(env);
  const stepGoal = num(env.STEP_GOAL, 10000);

  const [stepsRoll, distRoll, calRoll, activeRoll, sleep, rhr] = await Promise.all([
    dailyRollUp(token, "steps"),
    dailyRollUp(token, "distance"),
    dailyRollUp(token, "total-calories"),
    dailyRollUp(token, "active-minutes"),
    listSleep(token),
    listRestingHr(token),
  ]);

  const dist = distRoll.distance || {};
  let distanceKm = 0;
  if (dist.millimetersSum != null) {
    distanceKm = num(dist.millimetersSum) / 1e6;
  } else if (dist.metersSum != null) {
    distanceKm = num(dist.metersSum) / 1000;
  }

  const calories = Math.round(
    num(calRoll.totalCalories?.kcalSum ?? calRoll.total_calories?.kcal_sum),
  );

  const restingHr = num(
    rhr?.beatsPerMinute ?? rhr?.beats_per_minute ?? rhr?.bpm,
    null,
  );

  const payload = {
    updatedAt: new Date().toISOString(),
    source: "google-health",
    activity: {
      steps: num(stepsRoll.steps?.countSum ?? stepsRoll.steps?.count_sum),
      stepGoal,
      distanceKm: Math.round(distanceKm * 10) / 10,
      calories,
      activeMinutes: parseActiveMinutes(activeRoll),
      restingHr: restingHr === 0 ? null : restingHr,
    },
    sleep: parseSleep(sleep),
  };

  await env.TOKENS.put(
    CACHE_KEY,
    JSON.stringify({ expires: Date.now() + CACHE_TTL_MS, payload }),
  );
  return payload;
}
