import { fixtureStats } from "./fixture.js";
import { authUrl, exchangeCode, fetchLiveStats } from "./google-health.js";

function json(data, status = 200, extra = {}) {
  return new Response(JSON.stringify(data), {
    status,
    headers: {
      "Content-Type": "application/json",
      "Cache-Control": "no-store",
      ...extra,
    },
  });
}

function html(body, status = 200) {
  return new Response(body, {
    status,
    headers: { "Content-Type": "text/html; charset=utf-8" },
  });
}

function requireApiKey(request, env) {
  const key = env.PEBBLE_API_KEY;
  if (!key) return false;
  const auth = request.headers.get("Authorization") || "";
  const headerKey = request.headers.get("X-Api-Key") || "";
  if (auth === `Bearer ${key}`) return true;
  if (headerKey === key) return true;
  const url = new URL(request.url);
  if (url.searchParams.get("key") === key) return true;
  return false;
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);

    if (url.pathname === "/") {
      return html(`<!doctype html>
<html><head><title>Pebble × Google Health</title></head>
<body style="font-family:system-ui;max-width:40rem;margin:2rem auto;padding:0 1rem">
  <h1>Pebble × Google Health</h1>
  <p>Personal Google Health proxy for Pebble Time 2 (reference only — not used by Health Glance).</p>
  <ul>
    <li><a href="/auth">Connect Google Health</a></li>
    <li><code>GET /stats</code> — requires API key</li>
    <li>Mock mode: <strong>${env.USE_MOCK === "true" ? "ON" : "OFF"}</strong></li>
  </ul>
</body></html>`);
    }

    if (url.pathname === "/auth") {
      if (!env.GOOGLE_CLIENT_ID || !env.OAUTH_REDIRECT_URI) {
        return html(
          "<p>Set GOOGLE_CLIENT_ID and OAUTH_REDIRECT_URI (see .dev.vars.example).</p>",
          500,
        );
      }
      const state = crypto.randomUUID();
      await env.TOKENS.put(`oauth_state:${state}`, "1", { expirationTtl: 600 });
      return Response.redirect(authUrl(env, state), 302);
    }

    if (url.pathname === "/oauth/callback") {
      const code = url.searchParams.get("code");
      const state = url.searchParams.get("state");
      const err = url.searchParams.get("error");
      if (err) return html(`<p>OAuth error: ${err}</p>`, 400);
      if (!code || !state) return html("<p>Missing code/state</p>", 400);
      const ok = await env.TOKENS.get(`oauth_state:${state}`);
      if (!ok) return html("<p>Invalid or expired state</p>", 400);
      await env.TOKENS.delete(`oauth_state:${state}`);
      try {
        await exchangeCode(env, code);
        return html(
          "<p>Connected. You can close this tab. Watch will use <code>/stats</code>.</p>",
        );
      } catch (e) {
        return html(`<p>Token exchange failed: ${e.message}</p>`, 500);
      }
    }

    // pebbleproxy historically produced "//stats" when path already had a leading slash
    if (url.pathname === "/stats" || url.pathname === "//stats") {
      if (!requireApiKey(request, env)) {
        return json({ error: "unauthorized" }, 401);
      }
      try {
        if (env.USE_MOCK === "true") {
          return json(fixtureStats(Number(env.STEP_GOAL) || 10000));
        }
        const bypassCache = url.searchParams.get("refresh") === "1";
        const stats = await fetchLiveStats(env, { bypassCache });
        return json(stats);
      } catch (e) {
        return json({ error: e.message || String(e) }, 500);
      }
    }

    return json({ error: "not found" }, 404);
  },
};
