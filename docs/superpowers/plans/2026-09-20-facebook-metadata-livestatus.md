# Facebook metadata + live-status Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Soporte Facebook (Page + perfil, modos Independent y Managed) para título/descripción vía LiveVideo API, más indicador de en-vivo y botón on/off si la investigación lo avala — sin tocar Aitum/RTMP/keys (§51).

**Architecture:** `FacebookProvider` (adapter OAuth + `apply_metadata`/`list_resources`) enchufa en `ConnectService` genérico y ruta `/metadata/<name>` existente; el dock añade rutas Independent directa y Managed por backend, selector Page/perfil, indicador de estado y botón on/off. App secret solo backend; BYO-app en Independent.

**Tech Stack:** Meta Graph API v25/v26 + Live Video API (`POST /{live-video-id}` update, `/{id}/live_videos` list, `status` lifecycle, `end_live_video`), Python stdlib backend, Qt6/C++ dock, `python -m unittest` + `metadata-selfcheck.exe`.

**Spec:** `docs/notes/005-nuevas-plataformas-facebook-tiktok-x.md` (idea aceptada solo Facebook; TikTok/X quedan fuera) + respuestas del operador 2026-09-20 (metadata sí; on/off investigado sin forzar; warnings de elegibilidad en UI; ambos modos; activos E2E parciales).

## Global Constraints

- Windows x64 + OBS Studio 32.2.2; Qt6 vía obs-deps; preset `windows-x64`, RelWithDebInfo.
- Título FB máx 254 caracteres; descripción soportada (a diferencia de Twitch/Kick).
- §15 inviolable sin ADR: un solo plugin; Independent+Managed; `Account != ManagedConn`; ManagedConn sin tokens; secrets backend-only; sin fallback provider/modo; `loadStore` sin auto-fetch.
- §51: multistream/RTMP/keys/Aitum fuera de alcance — solo estado del LiveVideo por API.
- Tokens FB sin `refresh_token` (long-lived 60 días / page tokens): adaptar `TokenStore`/refresh sin romper el contrato de otros providers.
- App Review + posible business verification para terceros; test users en modo dev para E2E temprano.
- Cero secretos/tokens en repo, logs, respuestas, instalador o `dist/`; patrón secret-scan CI limpio.
- Sin polling innecesario; 401→refresh/reconnect, 403→scopes, 429 sin reintento agresivo, 5xx backoff.
- Commits en español, atómicos, solo con autorización; PR con template + CI verde + squash; merge solo con aprobación.
- Una fase por sesión; gate en verde con comando+salida antes de la siguiente; al cerrar fase, preguntar antes de dar el prompt de la siguiente.
- Mínima modificación por fase; solo archivos de su alcance + docs de cierre.

---

## Base investigada (2026-09-20, re-verificar en FB-0 antes de codificar)

```text
UPDATE:   POST /{live-video-id} (title ≤254 + description), sin crear broadcasts
LIST:     GET /{page-id|user-id}/live_videos ; READ: GET /{live-video-id}?fields=status,title,description
GO/STOP:  crear con status=LIVE_NOW ; POST /{live-video-id}?end_live_video=true
PERMISOS: perfil → publish_video ; Page → token admin + pages_read_engagement + pages_manage_posts (+pages_show_list)
ELEGIB.:  cuenta 60+ días (200/1363120) ; Page/perfil-pro 100+ seguidores (200/1363144) → mensajes UI
OAUTH:    code-exchange exige app secret → Managed backend / Independent BYO-app
TOKENS:   user 60 días, page longevos, sin refresh_token clásico
E2E HOY:  cuenta 60+ días OK ; sin page 100+ ; Meta app por crear → FB-0 define vía (test users / update sin live / perfil)
```

---

### FB-0: Investigación (sin código)

**Files:** ninguno de producto. Produce: brief en este plan o `docs/T0FB-RESEARCH.md` si el volumen lo exige + decisiones.
**Skills/MCP:** `context7` (Meta docs), `websearch`/`webfetch` (docs oficiales fechadas), `codebase-memory` (grafo del proyecto).

- [ ] **Step 0: Inventario exhaustivo del proyecto (primero, sin omitir nada)** — leer TODOS los archivos y código del repo (`src/`, `backend/`, `cmake/`, `tools/`, raíz, config, tests) y las docs normativas (`AGENTS.md`, `ARCHITECTURE-BACKEND.md`, `PROJECT_CONTEXT.md`, `BACKLOG.md`, `STATE.md`, `FINDINGS.md`, `VALIDATION.md`, ADRs, `DEPLOYMENT.md`, `SECURITY.md`, notas). Apoyarse en `codebase-memory` (`get_architecture`, `search_graph`, `trace_path`) para el mapa real de módulos, el patrón por proveedor (adapter/puertos/`ConnectService`/ruta genérica), la matriz Independent×Managed y los boundaries (`backend_auth`, secretos, `TokenStore`). Producir mapa escrito (módulos, flujos OAuth/metadata por proveedor y modo, límites conocidos, deuda) y usarlo para alinear cada step 1–7 con la realidad del código; toda discrepancia doc-vs-código va a `FINDINGS.md` como candidata. Gate del step: mapa completo + cero archivos sin leer (verificable por lista).
- [ ] **Step 2: Endpoints** — update/list/read confirmados en docs vigentes; params update en vivo (cuidado `LIVE_VIDEO__EDIT_API_NOT_ALLOWED`); lifecycle `status`.
- [ ] **Step 3: Go-live/stop + Aitum** — ¿encender por API con ingesta RTMP externa activa? ¿apagar exige reconectar/reconfigurar en Aitum? Vía recomendada sin reprocesos; si no existe, alternativas; si no hay, se descarta sin forzar.
- [ ] **Step 4: Tokens** — expiración user/page, canje long-lived, estrategia sin refresh para `TokenStore`.
- [ ] **Step 5: Errores** — catálogo (elegibilidad, permisos, 429, 5xx) → mensajes UI propuestos.
- [ ] **Step 6: E2E viable** — camino con activos actuales + qué falta (page/app/review).
- [ ] **Step 7: Decisiones** — on/off viable Y/N, Page+perfil confirmado, Independent/Managed confirmado, brief aprobado por el operador (gate). El brief debe citar para cada decisión el archivo/línea del proyecto que la sustenta (del Step 0) + la fuente Meta con fecha.

### FB-1: PoC offline + sondas manuales (estilo T-030)

**Files:** `poc/f0fb/` o runners `tools/f0fb_*.py` (evidencia, no producto) + `docs/T0FB-POC.md`.
**Skills/MCP:** `test-driven-development` donde aplique, `systematic-debugging`.

- [ ] **Step 1: Validadores** — título 1–254, descripción, payloads update, merge mínima.
- [ ] **Step 2: Sondas vivas manuales** — update + read-back, lectura `status`, go-live/stop solo si FB-0 lo avaló; runners redactados, revoke al final.
- [ ] **Step 3: Gate** — 28/28 offline + triple LIVE PASS con read-back (o parcial documentado si E2E bloqueado por activos).

### FB-2: Independent (dock directo BYO-app)

**Files:** `src/metadata*` (provider Facebook: cuenta, payload, classify, refresh/long-lived, revoke) + `metadata_selfcheck.cpp` + docs.
- [ ] **Step 1: Cuenta + OAuth directo** (sin secret embebido; BYO-app del usuario).
- [ ] **Step 2: Apply + selector Page/perfil + read-back**.
- [ ] **Step 3: Selfchecks + build 0 errores + scan limpio**.
- [ ] **Step 4: E2E manual operador + gate**.

### FB-3: Managed (adapter + endpoint + dock)

**Files:** `backend/adapters/facebook.py` + `backend/tests/test_facebook.py` (TDD) + dock managed + `ARCHITECTURE-BACKEND.md` §18 si aplica.
- [ ] **Step 1: Test rojo** — `MetadataFacebookTest` + HTTP roundtrip (espejo `test_twitch.py`/`test_kick.py`).
- [ ] **Step 2: Adapter** — exchange/refresh-adaptado/revoke/identity/`list_resources`/`apply_metadata`; errores mapeados (401→SESSION_EXPIRED, 403→AUTHORIZATION, elegibilidad→mensajes).
- [ ] **Step 3: Suite verde** — `discover -s backend/tests` 0 fallos + scan limpio (ruta genérica, sin cambios de routing).
- [ ] **Step 4: Dock managed** — `startManagedFacebookApply` + routing + build + selfcheck.
- [ ] **Step 5: Deploy `tools/deploy.ps1` + E2E operador + gate**.

### FB-4: UI y cierre

**Files:** dock (indicador en-vivo + botón on/off + warnings elegibilidad) + `VALIDATION.md` + `BACKLOG.md` + `FINDINGS.md` + ADR de proveedor + `INSTALLER.md` si aplica.
- [ ] **Step 1: Indicador + on/off** — solo si FB-0/FB-1 lo avalaron; si no, se documenta el descarte.
- [ ] **Step 2: Warnings** — 200/1363120/1363144 y permisos como mensajes UI.
- [ ] **Step 3: Packaging** — `tools/package.ps1` sin cambios previstos; rebuild local+commercial + install/uninstall.
- [ ] **Step 4: Cierre** — VALIDATION/BACKLOG/FINDINGS/ADR + PR con template + CI + squash + aprobación.
- [ ] **Step 5: No commit.** Preguntar siguiente ciclo (TikTok) solo si el operador lo pide.

---

## Self-Review

- Cobertura nota 005: Facebook sí; TikTok/X explícitamente fuera de este plan.
- Streaming/RTMP/Aitum: fuera (§51); solo estado LiveVideo por API + análisis de interplay en FB-0.
- Sin placeholders: cada fase lleva archivos, comandos y gates; plantillas citadas (T-030/T-062/T-068).
- Riesgos: app review/business verification (FB-0), tokens sin refresh (FB-3), E2E sin page 100+ (FB-0 define vía), go-live con Aitum (FB-0 decide, FB-4 condicionado).
