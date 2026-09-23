# ADR-015 — Facebook metadata + live-status (cierre FB-4)

- **Fecha:** 2026-09-23 · **Tareas:** T-069…T-073 · **Estado:** decidido e implementado (código + docs + packaging; E2E operador pendiente).
- **Relación:** cierra el plan `docs/superpowers/plans/2026-09-20-facebook-metadata-livestatus.md` (FB-0…FB-4). No modifica §15 (un solo plugin, Independent+Managed, `Account != ManagedConn`, ManagedConn sin tokens, secrets backend-only, sin fallback, `loadStore` sin auto-fetch). No toca §51 (cero Aitum/RTMP/keys/streaming/multistream: solo estado del LiveVideo por API).

## 1. Contexto

FB-0 investigó OAuth/endpoints/go-live/tokens/errores/E2E (D1–D14, `docs/T0FB-RESEARCH.md`). FB-1 validó offline 28/28 + sondas vivas: B' update+privacy PASS, C go-live/stop/delete PASS, A/H1 negativas (preview web no gestionable, F-065/F-066). FB-2 implementó Independent BYO-app PKCE (F-071…F-075, Page diferida por Consumer F-072). FB-3 implementó Managed adapter + endpoint + dock (F-076…F-080, perfil-only por Consumer F-079). Quedaban: indicador/on-off, warnings, creación-por-dock, packaging y cierre.

## 2. Decisión

Facebook queda como 4ª plataforma permanente (perfil; Page cuando la app del servicio sea None/Business + App Review, código `_page_token` ya listo):

- Metadata título 1–254 + descripción SÍ (a diferencia de Twitch/Kick) + `privacy EVERYONE`, por `POST /{live-video-id}` (Independent directo) y `POST /metadata/facebook` (Managed server-side, ruta genérica sin cambios).
- Ciclo de vida ID-céntrico (F-074/F-075): el dock crea su propio objeto (`POST /{me|page}/live_videos`), persiste el ID en claro (`secure::Data.facebookLiveId`, no-sensible), muestra indicador por `status` (`FbLiveState`: Live/Preview/Ended/Unknown), ON (`status=LIVE_NOW`) solo sobre ID existente/creado-por-dock + confirmación explícita, OFF (`end_live_video`) + DELETE limpieza con confirmación. Sin polling auto; solo a petición.
- Errores: `100/33` (objeto web/ID desconocido) es NotFound gestionable (“Create one from the dock”), no BadRequest de título (F-080/F-081). Elegibilidad `1363120` (60+ días) / `1363144` (100+ seguidores) / `10` (permisos/review) como warnings específicos; `190`→reconnect (D8, sin refresh clásico).
- Tokens FB sin `refresh_token`: `TokenPair(access,"",expires)` user ~60d, page derivados en memoria jamás persistidos; `ensure_fresh_token`→`SESSION_EXPIRED`→reconnect. Revoke `DELETE /me/permissions` best-effort (D10).

## 3. Alternativas descartadas

- Gestionar el preview web de herramientas externas (H1 refutada F-065/F-066, también en Managed F-080).
- Listado como fuente de verdad (vacío con objetos legibles por ID, F-074/F-075; best-effort + ID pegable + creación-por-dock).
- Nuevas rutas HTTP por operación (se usa `op` sobre la ruta genérica existente; `http_server.py` intacto).
- Polling de status (cuota/complejidad; solo manual, como YouTube `Refresh`).

## 4. Consecuencias

- `ARCHITECTURE-BACKEND.md` §18 fila Facebook → implementado + validado (código) / E2E operador pendiente en esta rama.
- `PRIVACY.md` + inventario: fila Facebook (perfil id+name, live IDs en tránsito + `facebookLiveId` local, tokens como YT/Kick Managed).
- Page/App Review (D12/D14): sin bloqueo para perfil; Managed-terceros exige app Live + Advanced (coste cero, verificación de negocio; sin autónomo el techo es Independent).
- E2E operador requerido antes de merge (procedimiento en `VALIDATION.md` T-073).
