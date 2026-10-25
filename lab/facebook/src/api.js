/* lab/facebook/src/api.js — cliente minimo del lab (sin deps, sin secretos).
 * El token vive solo en el proceso node; el navegador nunca lo ve.
 */
"use strict";

async function post(path, body) {
  const r = await fetch(path, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify(body === undefined ? {} : body),
  });
  return r.json();
}

async function get(path) {
  const r = await fetch(path);
  return r.json();
}

/* Validadores puros (auditar vs backend/adapters/facebook.py). */
function validTitle(t) {
  return typeof t === "string" && t.length >= 1 && t.length <= 254;
}

const api = {
  state: () => get("/api/state"),
  authUrl: () => get("/auth/url"),
  exchange: () => post("/auth/exchange", {}),
  reset: () => post("/api/reset", {}),
  graph: (method, path, body) => post("/api/graph", { method, path, body }),

  createLive: (title, description) =>
    post("/api/graph", {
      method: "POST",
      path: "/me/live_videos",
      body: {
        _lab_create: true,
        title,
        description: description || undefined,
        status: "UNPUBLISHED",
        privacy: { value: "EVERYONE" },
      },
    }),
  liveStatus: (id) =>
    post("/api/graph", {
      method: "GET",
      path: `/${encodeURIComponent(id)}?fields=id,title,description,status,privacy`,
    }),
  updateLive: (id, title, description) =>
    post("/api/graph", {
      method: "POST",
      path: `/${encodeURIComponent(id)}`,
      body: {
        title,
        description: description || undefined,
        privacy: { value: "EVERYONE" },
      },
    }),
  goLive: (id) => post("/api/graph", { method: "POST", path: `/${encodeURIComponent(id)}`, body: { status: "LIVE_NOW" } }),
  endLive: (id) => post("/api/graph", { method: "POST", path: `/${encodeURIComponent(id)}?end_live_video=true`, body: {} }),
  deleteLive: (id) => post("/api/graph", { method: "DELETE", path: `/${encodeURIComponent(id)}` }),
};

// Partido Servidor+Clave para copia MANUAL a Aitum (el lab jamas automatiza Aitum).
function splitIngest(secureUrl) {
  const u = String(secureUrl || "");
  const i = u.lastIndexOf("/");
  if (i <= 8) return { server: u, key: "" };
  return { server: u.slice(0, i), key: u.slice(i + 1) };
}
