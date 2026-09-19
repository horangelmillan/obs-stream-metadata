# T-068 Apply Managed Kick Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Apply de título Kick en modo Managed vía backend (`POST /metadata/kick`), contraparte de T-062/Twitch, cerrando el "Kick Managed no tiene Apply path" (`src/metadata_dock.cpp:2096-2098`).

**Architecture:** `KickProvider::apply_metadata` (PATCH `stream_title` + 204, Bearer+UA navegador por F-022) enchufa en la ruta genérica `/metadata/<name>` (`backend/http_server.py:285-293`) sin tocar routing; el dock añade `startManagedKickApply` espejo de `startManagedTwitchApply` y lo enruta en `startApplyNext`. Sin secretos en respuestas/logs (solo IDs/títulos).

**Tech Stack:** Python stdlib + urllib (backend), Qt6/C++ (dock), NSIS existente, `python -m unittest` + `metadata-selfcheck.exe`, E2E humano con canal Kick en directo (F-023: título solo legible en directo).

**Spec:** BACKLOG T-068; causa raíz verificada (Managed Kick → cuenta Independent vacía → 401/AuthRequired); plantillas: `backend/adapters/twitch.py:222-250`, `backend/tests/test_twitch.py:211-314`, `src/metadata_dock.cpp:1281-1349`.

## Global Constraints

- Windows x64 + OBS 32.2.2; Qt6 obs-deps; preset `windows-x64`, RelWithDebInfo.
- Kick: título sí, descripción de stream NO (AGENTS.md §2/§14.6): `description` se acepta y se ignora, jamás escribir `channel_description`.
- Límite título Kick no documentado oficialmente → validar no-vacío en backend y dejar el resto al servidor (400 → INVALID_REQUEST); no inventar cap.
- Tokens/secretos jamás en repo, logs, respuestas ni instalador; patrón secret-scan CI limpio.
- Sin polling; refresh single-flight existente vía `ensure_fresh_token`; 401 → SESSION_EXPIRED (reconnect), 403 → AUTHORIZATION, 429/5xx mapeados.
- Commits en español, atómicos, solo con autorización; PR con template + CI verde + squash; fusión solo con aprobación.
- Mínima modificación: `backend/adapters/kick.py`, `backend/tests/test_kick.py`, `src/metadata_dock.{h,cpp}` (+ docs de cierre). Sin tocar routing, stores, CMake, installer.

---

## File Structure

- Modify: `backend/adapters/kick.py` — `_patch_json` (PATCH + Bearer + UA, espejo de twitch `_patch_json`/`_api_get` con UA F-022) + `KickProvider::apply_metadata`.
- Modify: `backend/tests/test_kick.py` — `MetadataKickTest` (TDD, espejo de `test_twitch.py:211-314` con fakes `kat-1/krt-1`).
- Modify: `src/metadata_dock.h` — declarar `startManagedKickApply()`.
- Modify: `src/metadata_dock.cpp` — implementar `startManagedKickApply()` (espejo Twitch, POST `/metadata/kick` con `{title}`), enrutar en `startApplyNext` (línea ~2100: incluir `P::Kick`; rama `else`: `if (isManaged()) startManagedKickApply()`), actualizar comentario 2096-2098.
- Modify (al cerrar): `docs/VALIDATION.md` (bloque T-068), `docs/BACKLOG.md` (T-068 → `hecha`), `docs/FINDINGS.md` (fila del hueco si se considera hallazgo).

---

### Task 1: `KickProvider::apply_metadata` (TDD)

**Files:**
- Modify: `backend/adapters/kick.py`
- Test: `backend/tests/test_kick.py` (nueva clase `MetadataKickTest`)

**Interfaces:**
- Consumes: `_post_form`/`_api_get` (UA navegador), `classify_kick_error`, `ConnectService.apply_metadata` (genérico, sin cambios), `ErrorCode` (INVALID_REQUEST/SESSION_EXPIRED/AUTHORIZATION/PROVIDER_*).
- Produces: `KickProvider.apply_metadata(access_token: str, data: dict) -> dict` (`{"broadcaster_id","title"}`); errores: `""`/ausente→INVALID_REQUEST, 204→updated, 400→INVALID_REQUEST, 401→SESSION_EXPIRED, 403→AUTHORIZATION, resto→`classify_kick_error`.

- [ ] **Step 1: Test rojo** — añadir a `backend/tests/test_kick.py` clase `MetadataKickTest` espejo de `test_twitch.py:211-314`: `_connected()` con `fake_kick_ok` extendido (PATCH `public/v1/channels` → `(204, {})`), `test_apply_title_204` (`{"title":"Nuevo directo"}` → `status updated`, `result {"broadcaster_id":"99","title":...}`, `description` ignorada), `test_response_carries_no_tokens` (ni `kat-1`/`krt-1` ni `secret` en JSON), `test_validation` (título `""` → INVALID_REQUEST), `test_error_mapping` (400→INVALID_REQUEST, 401→SESSION_EXPIRED, 403→AUTHORIZATION), `test_headers_on_real_path` (spy urlopen: `Authorization: Bearer at-real`, método PATCH), `test_not_connected_is_authentication`.
- [ ] **Step 2: Ver rojo** — Run: `python -m unittest backend.tests.test_kick -v` Expected: FAIL (AttributeError apply_metadata).
- [ ] **Step 3: Implementar** — en `kick.py`: `_patch_json(url, token, payload, transport)` (inyectable como `_post_form`: `transport("PATCH", url, {"token":..., "payload":...})`; urllib real con Bearer + UA + JSON); `apply_metadata`: `title=str(data.get("title",""))`, `description` ignorada; `""`→INVALID_REQUEST; PATCH `CHANNELS_URL {"stream_title": title}`; 200/204→`{"broadcaster_id": <identity?>, "title"}` — NOTA: Kick PATCH no devuelve identidad; usar `broadcaster_id=str(data.get("broadcaster_id",""))` si el plugin lo envía, o `""` si ausente (no inventar llamada extra). Decisión fijada en Task 3 (el dock enviará su `userId` Managed como Twitch).
- [ ] **Step 4: Ver verde** — Run: `python -m unittest discover -s backend/tests` Expected: 0 fallos (base 209 + nuevos).
- [ ] **Step 5: Secret-scan** — Run: patrón CI sobre `backend/adapters/kick.py` + `backend/tests/test_kick.py` Expected: limpio.

### Task 2: Ruta `/metadata/kick` (sin cambios, solo verificación)

**Files:** ninguno (ruta genérica `http_server.py:285-293` + `ConnectService.apply_metadata` `oauth.py:171-176` ya despachan por nombre de proveedor).

- [ ] **Step 1: Test de despacho** — añadir a `MetadataKickTest`: POST real contra servidor local de test (`/metadata/kick` con sesión válida → 200 `updated`; `/metadata/nope` → 400) espejo de `test_twitch.py:317+` (leer ese bloque antes de escribir).
- [ ] **Step 2: Ver verde** — Run: `python -m unittest backend.tests.test_kick -v` Expected: PASS.
- [ ] **Step 3: Si `/metadata/kick` no despacha** (Task en rojo): parar, informar evidencia + hipótesis, esperar visto bueno. No tocar routing sin aprobación.

### Task 3: Dock `startManagedKickApply` + routing

**Files:**
- Modify: `src/metadata_dock.h` (declaración junto a `startManagedTwitchApply`)
- Modify: `src/metadata_dock.cpp` (`startApplyNext` ~2096-2171 + nueva función tras `startManagedTwitchApply`)

**Interfaces:**
- Consumes: `managedAccount(P::Kick)` (`connected`,`userId`), `managedAuth_->apiPost`, `meta::classifyStatus`, `scheduleBackoff`, `finishPlatform/startApplyNext`, `meta::userMessage`.
- Produces: en Managed, Kick va por backend; Independent intacto (PATCH directo `UpKk`).

- [ ] **Step 1: Declaración + routing** — `metadata_dock.h`: `void startManagedKickApply();`. `startApplyNext`: condición línea 2100 pasa a `(p == P::YouTube || p == P::Twitch || p == P::Kick)`; rama `else` (Kick): `if (isManaged()) { startManagedKickApply(); return; }` antes del PATCH directo; actualizar comentario 2096-2098 ("Managed Apply: YouTube, Twitch y Kick vía backend").
- [ ] **Step 2: Implementación** — `startManagedKickApply()` espejo de `startManagedTwitchApply` (`metadata_dock.cpp:1286-1349`): guarda `if (!m.connected || m.userId.isEmpty())` → AuthRequired; body `{title, broadcaster_id: m.userId}` (el backend ignora `broadcaster_id` salvo validación de presencia — coordinar con Task 1: si Task 1 exige `broadcaster_id` no-vacío, aquí siempre viaja; si Task 1 lo hace opcional, igual viaja); POST `/metadata/kick`; réplica Ok/errores/backoff/AuthRequired→snapshot drop + `saveStore()` idéntica a Twitch.
- [ ] **Step 3: Build** — Run: configure/build preset `windows-x64` RelWithDebInfo Expected: 0 errores + `metadata-selfcheck.exe` en verde.
- [ ] **Step 4: Secret-scan** — patrón CI sobre `src/metadata_dock.*` Expected: limpio.

### Task 4: E2E Managed Kick + cierre (requiere operador)

**Files:** ninguno de producto (solo docs de cierre).

- [ ] **Step 1: Pre-requisito deploy** — el E2E Managed exige el backend con `/metadata/kick` donde apunte el instalador del operador (prod → redeploy Cloud Run; local → `python -m backend.app`). Verificar vía elegida ANTES del E2E. Si el deploy lo hace el operador, esperarlo (Task en espera, no en rojo).
- [ ] **Step 2: E2E humano** — 1. Cerrar OBS. 2. Instalar build con T-068. 3. OBS normal → modo Managed → Connect Kick (browser + consent). 4. Cambiar título → Apply → `✓ Kick actualizado`. 5. Read-back en dashboard Kick **con el canal en directo** (F-023: offline el título no es legible). 6. Desinstalar/prueba de persistencia según `docs/INSTALLER.md`.
- [ ] **Step 3: Evidencia** — comando + salida + hash + líneas de log (`managed connected: Kick`, `apply Kick: ok (managed)`).
- [ ] **Step 4: Docs** — `docs/VALIDATION.md` bloque T-068; `docs/BACKLOG.md` T-068 → `hecha`; `docs/FINDINGS.md` fila del hueco (Managed Kick sin Apply) si aplica.
- [ ] **Step 5: No commit.** Informe + PR solo con autorización (rama `feat/t-068-managed-kick-apply`, squash, template).

---

## Self-Review

- Cobertura: connect Managed Kick ya existe (T-046); este plan solo añade Apply. Descripción Kick ignorada en ambos extremos (dock no la envía como requisito; backend la acepta y la ignora, espejo Twitch).
- Sin placeholders: cada step lleva archivo/línea/comando/expected; plantillas citadas con líneas exactas.
- Riesgo `broadcaster_id`: Kick no lo necesita (PATCH va sin query); Task 1/3 coordinados para que viaje pero no sea imprescindible — el backend NO debe exigirlo vacío-fallido si el dock siempre lo manda; decisión final en Task 1 Step 3 con el código delante (Twitch lo exige; Kick puede no exigirlo: documentar la decisión en el docstring).
- Riesgo secret-scan: `_patch_json` no introduce `*_token=` (usa `{"token":...}` en dict de transporte como `_api_get`, ya aceptado en CI).
- Riesgo E2E: canal offline → read-back vacío (F-023); el plan lo exige en directo. Deploy prod pendiente de vía operador (Task 4 Step 1).
