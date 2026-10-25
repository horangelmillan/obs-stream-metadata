"use strict";
/* lab/facebook/server.js — Lab throwaway T-077 (Fase 2, F-088).
 *
 * Node stdlib-only: http + crypto + fs (+ fetch nativo Node 22).
 * Sin dependencias, sin imports de src/ ni backend/.
 *
 * - GET  /auth/url      dialog/oauth + PKCE S256 + state (memoria)
 * - GET  /fb-cb         valida state (un uso), captura code (memoria)
 * - POST /auth/exchange graph oauth/access_token SIN secret (memoria)
 * - POST /api/graph     proxy Bearer en memoria (token jamas a disco/logs)
 * - POST /api/reset     borra estado en memoria
 *
 * secure_stream_url: solo transito en memoria; la UI la muestra una vez.
 */

const http = require("node:http");
const crypto = require("node:crypto");
const fs = require("node:fs");
const path = require("node:path");

const GRAPH_VERSION = "v26.0";
const DIALOG_URL = `https://www.facebook.com/${GRAPH_VERSION}/dialog/oauth`;
const TOKEN_URL = `https://graph.facebook.com/${GRAPH_VERSION}/oauth/access_token`;
const GRAPH_URL = `https://graph.facebook.com/${GRAPH_VERSION}`;
const SCOPES = ["publish_video"];
const FB_TITLE_MAX = 254;
const LAB_PORT = 3002;

/* --- .env manual (sin deps): APP_ID con alias + PORT. --- */
function loadEnvFile(file) {
  try {
    const raw = fs.readFileSync(file, "utf8");
    for (const line of raw.split(/\r?\n/)) {
      const t = line.trim();
      if (!t || t.startsWith("#") || !t.includes("=")) continue;
      const i = t.indexOf("=");
      const k = t.slice(0, i).trim();
      const v = t.slice(i + 1).trim().replace(/^["']|["']$/g, "");
      if (k && process.env[k] === undefined) process.env[k] = v;
    }
  } catch {
    /* sin .env: APP_ID puede venir del entorno */
  }
}
loadEnvFile(path.join(__dirname, ".env"));

/* Alias config: FB_APP_ID gana; si vacio, fallback a FB_URL_APP (trim).
 * Se loguea SOLO nombre de variable + longitud, jamas el valor. */
function pickEnv(...names) {
  for (const n of names) {
    const v = (process.env[n] || "").trim();
    if (v) return { name: n, value: v };
  }
  return { name: null, value: "" };
}
const APP_ID_LOOKED_AT = ["FB_APP_ID", "FB_URL_APP"];
const appIdCfg = pickEnv(...APP_ID_LOOKED_AT);
const APP_ID = appIdCfg.value;
const APP_ID_SOURCE = appIdCfg.name;
const PORT = parseInt(process.env.PORT || String(LAB_PORT), 10) || LAB_PORT;

/* --- estado SOLO en memoria (jamas disco/logs) --- */
const mem = {
  state: null,      // { value, verifier, used }
  code: null,       // authorization code capturado (un uso)
  token: null,      // user access token (solo memoria)
  liveId: null,     // ultimo LiveVideo creado
  ingestShown: false,
};

function b64url(buf) {
  return Buffer.from(buf)
    .toString("base64")
    .replace(/\+/g, "-")
    .replace(/\//g, "_")
    .replace(/=+$/, "");
}

/* --- validadores puros (reimplementados; auditar vs backend/adapters/facebook.py) --- */
function validTitle(t) {
  return typeof t === "string" && t.length >= 1 && t.length <= FB_TITLE_MAX;
}

/** Clasifica error Meta -> codigo lab (100/33, 190, 1363120/44, resto). */
function classify(status, payload) {
  const p = payload && typeof payload === "object" ? payload : {};
  const err = p.error && typeof p.error === "object" ? p.error : {};
  const code = Number(err.code ?? p.code ?? 0) || 0;
  const sub = Number(err.error_subcode ?? p.error_subcode ?? 0) || 0;
  if (code === 100 && sub === 33) return "not-manageable:33";
  if (code === 190) return "reconnect";
  if (code === 1363120 || code === 1363144) return `eligibility:${code}`;
  if (code === 10) return "forbidden";
  if (code === 613 || code === 4 || code === 17) return "rate";
  if (status === 400) return "invalid";
  if (status === 401) return "reconnect";
  if (status === 403) return "forbidden";
  if (status === 404) return "not-found";
  if (status === 429) return "rate";
  if (status >= 500) return `provider:${status}`;
  return `provider:${code || status}`;
}

function sendJson(res, status, obj) {
  const body = JSON.stringify(obj);
  res.writeHead(status, {
    "content-type": "application/json; charset=utf-8",
    "content-length": Buffer.byteLength(body),
  });
  res.end(body);
}

function readBody(req) {
  return new Promise((resolve, reject) => {
    let chunks = "";
    req.on("data", (c) => {
      chunks += c;
      if (chunks.length > 1 << 20) reject(new Error("body-too-large"));
    });
    req.on("end", () => resolve(chunks));
    req.on("error", reject);
  });
}

function serveFile(res, file, contentType) {
  fs.readFile(path.join(__dirname, file), (err, data) => {
    if (err) {
      res.writeHead(404);
      res.end("not found");
      return;
    }
    res.writeHead(200, { "content-type": contentType });
    res.end(data);
  });
}

/* Log seguro: nunca token/code/verifier/ingesta. */
function safeLog(...args) {
  console.log(...args);
}

const server = http.createServer(async (req, res) => {
  try {
    const url = new URL(req.url || "/", `http://localhost:${PORT}`);

    if (req.method === "GET" && url.pathname === "/") {
      serveFile(res, "index.html", "text/html; charset=utf-8");
      return;
    }
    if (req.method === "GET" && url.pathname.startsWith("/src/")) {
      const name = url.pathname.slice("/src/".length).replace(/[^A-Za-z0-9._-]/g, "");
      const ct = name.endsWith(".css") ? "text/css; charset=utf-8" : "text/javascript; charset=utf-8";
      serveFile(res, path.join("src", name), ct);
      return;
    }

    /* GET /auth/url — dialog/oauth + PKCE S256 + state */
    if (req.method === "GET" && url.pathname === "/auth/url") {
      if (!APP_ID) {
        sendJson(res, 500, { ok: false, error: "FB_APP_ID-missing", lookedAt: APP_ID_LOOKED_AT });
        return;
      }
      const verifier = b64url(crypto.randomBytes(32));
      const challenge = b64url(crypto.createHash("sha256").update(verifier).digest());
      const state = b64url(crypto.randomBytes(16));
      mem.state = { value: state, verifier, used: false };
      mem.code = null;
      const redirect = `http://localhost:${PORT}/fb-cb`;
      const authUrl =
        DIALOG_URL +
        "?" +
        new URLSearchParams({
          response_type: "code",
          client_id: APP_ID,
          redirect_uri: redirect,
          scope: SCOPES.join(","),
          state,
          code_challenge: challenge,
          code_challenge_method: "S256",
        }).toString();
      safeLog("[lab] auth url built (state + verifier en memoria)");
      sendJson(res, 200, { ok: true, url: authUrl, state });
      return;
    }

    /* GET /fb-cb — valida state (un uso), captura code */
    if (req.method === "GET" && url.pathname === "/fb-cb") {
      const code = url.searchParams.get("code") || "";
      const state = url.searchParams.get("state") || "";
      const fbErr = url.searchParams.get("error") || "";
      if (fbErr) {
        res.writeHead(200, { "content-type": "text/html; charset=utf-8" });
        res.end("<h1>Lab Facebook</h1><p>Autorizacion denegada por el usuario.</p>");
        return;
      }
      if (!mem.state || mem.state.used || state !== mem.state.value || !code) {
        res.writeHead(400, { "content-type": "text/html; charset=utf-8" });
        res.end("<h1>Lab Facebook</h1><p>state invalido o code ausente.</p>");
        return;
      }
      mem.state.used = true;
      mem.code = code;
      safeLog("[lab] code capturado (state validado, un uso)");
      res.writeHead(200, { "content-type": "text/html; charset=utf-8" });
      res.end(
        "<h1>Lab Facebook</h1><p>Code capturado. Vuelve a la pesta&ntilde;a del lab y pulsa <b>Exchange</b>.</p>"
      );
      return;
    }

    /* POST /auth/exchange — canje SIN secret (code + verifier + client_id) */
    if (req.method === "POST" && url.pathname === "/auth/exchange") {
      if (!mem.code || !mem.state) {
        sendJson(res, 400, { ok: false, error: "no-code" });
        return;
      }
      const params = new URLSearchParams({
        client_id: APP_ID,
        redirect_uri: `http://localhost:${PORT}/fb-cb`,
        code: mem.code,
        code_verifier: mem.state.verifier,
      });
      mem.code = null; // un uso
      let status = 0;
      let payload = {};
      try {
        const r = await fetch(`${TOKEN_URL}?${params.toString()}`, { method: "GET" });
        status = r.status;
        payload = await r.json().catch(() => ({}));
      } catch (e) {
        sendJson(res, 502, { ok: false, error: "transport", detail: String(e).slice(0, 120) });
        return;
      }
      if (status !== 200 || !payload.access_token) {
        sendJson(res, 502, { ok: false, error: classify(status, payload) });
        return;
      }
      mem.token = String(payload.access_token);
      safeLog("[lab] exchange ok (token en memoria, longitud %d)", mem.token.length);
      sendJson(res, 200, { ok: true, connected: true });
      return;
    }

    /* POST /api/graph — proxy con Bearer en memoria */
    if (req.method === "POST" && url.pathname === "/api/graph") {
      if (!mem.token) {
        sendJson(res, 401, { ok: false, error: "not-connected" });
        return;
      }
      let input = {};
      try {
        input = JSON.parse(await readBody(req));
      } catch {
        sendJson(res, 400, { ok: false, error: "bad-json" });
        return;
      }
      const method = String(input.method || "GET").toUpperCase();
      const rel = String(input.path || "");
      if (!/^\/[A-Za-z0-9_./?=&%,-]*$/.test(rel) || rel.includes("..")) {
        sendJson(res, 400, { ok: false, error: "bad-path" });
        return;
      }
      const target = GRAPH_URL + rel;
      let status = 0;
      let payload = {};
      let fwdBody = input.body;
      if (fwdBody !== undefined && typeof fwdBody === "object" && fwdBody !== null) {
        // Flag solo-lab: se valida localmente y NUNCA se envia a Meta.
        const { _lab_create, persistent, ...rest } = fwdBody;
        if (persistent === true) rest.persistent_stream_key_status = "ENABLE";
        if (_lab_create === true) {
          const title = String(rest.title || "");
          if (!validTitle(title)) {
            sendJson(res, 400, { ok: false, error: "invalid-title" });
            return;
          }
        }
        fwdBody = rest;
      }
      try {
        const opts = { method, headers: { Authorization: "Bearer " + mem.token } };
        if (fwdBody !== undefined) {
          opts.headers["content-type"] = "application/json";
          opts.body = JSON.stringify(fwdBody);
        }
        const r = await fetch(target, opts);
        status = r.status;
        const text = await r.text();
        payload = text.trim() ? JSON.parse(text) : {};
      } catch (e) {
        sendJson(res, 502, { ok: false, error: "transport", detail: String(e).slice(0, 120) });
        return;
      }
      /* secure_stream_url solo transito: se reenvia al cliente, jamas a logs/disco. */
      if (status !== 200 || (payload && payload.error)) {
        sendJson(res, 502, { ok: false, error: classify(status, payload) });
        return;
      }
      if (rel.includes("/live_videos") && method === "POST" && input.body &&
          input.body._lab_create === true) {
        const title = String(input.body.title || "");
        if (!validTitle(title)) {
          sendJson(res, 400, { ok: false, error: "invalid-title" });
          return;
        }
      }
      if (payload.id && payload.secure_stream_url) mem.liveId = String(payload.id);
      else if (payload.id && rel.includes("live_videos")) mem.liveId = String(payload.id);
      mem.ingestShown = false;
      sendJson(res, 200, { ok: true, status, data: payload });
      return;
    }

    /* POST /api/reset — borra memoria */
    if (req.method === "POST" && url.pathname === "/api/reset") {
      mem.state = null;
      mem.code = null;
      mem.token = null;
      mem.liveId = null;
      mem.ingestShown = false;
      safeLog("[lab] estado en memoria borrado");
      sendJson(res, 200, { ok: true });
      return;
    }

    /* GET /api/state — estado no sensible para la UI */
    if (req.method === "GET" && url.pathname === "/api/state") {
      sendJson(res, 200, {
        ok: true,
        connected: !!mem.token,
        hasCode: !!mem.code,
        liveId: mem.liveId,
      });
      return;
    }

    sendJson(res, 404, { ok: false, error: "not-found" });
  } catch (e) {
    sendJson(res, 500, { ok: false, error: "internal" });
  }
});

server.listen(PORT, "127.0.0.1", () => {
  console.log(`[lab-facebook] http://localhost:${PORT}/ (callback http://localhost:${PORT}/fb-cb)`);
  safeLog(`[lab] APP_ID desde ${APP_ID_SOURCE || "ninguna"} (longitud ${APP_ID.length})`);
});
