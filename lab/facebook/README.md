# lab/facebook — Lab throwaway Facebook (Fase 2, F-088)

**Throwaway, no merge.** Espacio aislado de validación local del objetivo
T-077: título (1–254) + descripción + audiencia `EVERYONE` sobre un
LiveVideo **propio** creado por el lab (`POST /me/live_videos`), con
ingesta **manual** en Aitum por el operador y publicación explícita.

## Reglas del lab

- Node **stdlib-only** (`http`, `crypto`, `fs`; `fetch` nativo). Cero
  dependencias en `package.json` (`npm install` no descarga nada).
- React mínimo solo vía CDN en `index.html` (UMD, sin build); `src/`
  usa `React.createElement` (sin JSX transform, sin vite). Si algún día
  se añade vite, es **opcional** y debe documentarse aquí.
- **Sin imports** de `src/`, `backend/`, `poc/` ni `tools/`. Los
  validadores puros se **reimplementan** aquí (auditar contra
  `backend/adapters/facebook.py`, no importar).
- **Aitum es manual**: el operador copia Servidor+Clave desde el panel
  del lab a Aitum. El lab **jamás** automatiza Aitum/RTMP (AGENTS
  §§23/51 intactos).
- Secretos: el `.env` real **jamás se commitea** (ya ignorado por regla
  `.env` en `.gitignore`). Solo `.env.example` con valores vacíos.
- Token y `secure_stream_url` **solo en memoria** del proceso node;
  jamás a disco ni a logs. La UI muestra la URL de ingesta **una vez**
  (botón mostrar-una-vez) y no la persiste.

## Puertos

- Lab: **3002**. Callback: `http://localhost:3002/fb-cb`.
- El dock usa `kFbPort` **3001** (F-071). El lab usa 3002 para
  **no colisionar** con el dock.

## Uso

1. Copia `.env.example` a `.env` y rellena `FB_APP_ID` (solo App ID;
   el lab no usa `FB_APP_SECRET`: el canje es sin secreto + PKCE).
2. `npm run dev` (o `node server.js`). Abre `http://localhost:3002/`.
3. **Connect** → autoriza en facebook.com → vuelve al callback local.
4. **Create** → crea tu LiveVideo (`UNPUBLISHED`, `privacy EVERYONE`).
   Copia Servidor+Clave **manualmente** a Aitum.
5. **Status / Update / GoLive / End / Delete** según necesites.

## Endpoints locales (server.js)

- `GET /` — UI del lab (`index.html`).
- `GET /auth/url` — construye `dialog/oauth` + PKCE S256 + `state`;
  guarda `{state, verifier}` en memoria y devuelve `{url, state}`.
- `GET /fb-cb?code=...&state=...` — valida `state` de un uso, captura
  `code` en memoria, responde HTML mínimo (sin exponer nada sensible).
- `POST /auth/exchange` — canjea `code` + `verifier` contra
  `graph.facebook.com/oauth/access_token` **SIN secret**; guarda el
  token solo en memoria.
- `POST /api/graph` — proxy genérico: `{method, path, params, body}`.
  Inyecta `Authorization: Bearer <token-en-memoria>`. Nunca loguea el
  token ni la ingesta.
- `POST /api/reset` — borra estado en memoria (token/code/verifier,
  ingesta). Botón "Desconectar/limpiar" en la UI.

## Validadores puros (reimplementados, auditados vs backend)

- `title`: 1–254 chars (`FB_TITLE_MAX`, backend `facebook.py`).
- `privacy`: siempre `{"value":"EVERYONE"}` en create/update.
- `classify`: `100/33` → not-manageable · `190` → reconnect ·
  `1363120/1363144` → elegibilidad · resto según backend.

## Gate Fase 2b

El steward debe añadir `node_modules` a `.gitignore` (aunque hoy no hay
dependencias, blinda futuros `npm install` accidentales).
