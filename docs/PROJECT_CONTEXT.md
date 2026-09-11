# PROJECT_CONTEXT — obs-stream-metadata (snapshot operativo)

> Snapshot al commit `b2dae75` (master == origin/master). Punto de partida
> para sesiones nuevas. No reemplaza código ni docs normativas.
> Marcas: [C] confirmado en repo ahora mismo · [D] documentado (E2E/infra
> de sesiones previas, no re-ejecutado aquí) · [I] inferido · [P] pendiente.

## 1. Project Identity

Plugin Qt6 para OBS Studio 32.2.2 (Windows x64) + backend Python que
gestionan **título/descripción de streams** en Twitch, YouTube y Kick.
Componentes: plugin (`src/`, dock Qt integrado) + backend (`backend/`,
Cloud Run + Neon PostgreSQL). Alcance actual: MVP cerrado; un solo
plugin, dos modos (Independent/Managed). Sin multichat, multistream,
categorías, analytics ni backend remoto distinto del descrito.

## 2. Current Status

- MVP STATUS: CLOSED [D] · §50: GREEN [D] · FUNCTIONAL GAPS: NONE [D]
- BLOCKERS: NONE · HIGH: NONE
- CURRENT DEVELOPMENT CYCLE: ninguno (MVP cerrado; ciclo UI/UX futuro e independiente, sin abrir)
- NEXT REQUIRED TECHNICAL TASK: NONE [D]
- Baselines re-verificadas aquí [C]: backend 134 OK (7 skips PG por diseño), selfcheck 125/115, tree CLEAN, `master==origin/master==b2dae75`.

## 3. Architecture

- Plugin: dock Qt6 (`metadata_dock.*`, entrada `dock-poc.cpp`/`plugin-main.c`), frontend API `obs_frontend_add_dock_by_id`.
- Backend: `backend/` stdlib + `psycopg==3.3.5`/`psycopg-binary==3.3.5` (únicas deps externas).
- Comunicación: HTTPS JSON; `POST /auth/*` (bootstrap/sesión HMAC) + `POST/GET /connect/<provider>/*` con bearer; callbacks browser sin bearer (state+PKCE).
- Independent: OAuth directo BYO-app desde el plugin (Twitch DCF, YT/Kick PKCE loopback).
- Managed: OAuth vía backend (`ConnectService` genérico + adapters por proveedor).
- `Account` (credenciales Independent, memoria/DPAPI) ≠ `ManagedConn{connected,userId,display}` (labels, sin secretos).
- Metadata flow: dock → Apply por plataforma (usa `Account` Independent; `ManagedConn` no alimenta Apply) o APIs provider vía backend.
- Logging: `RedactingFilter` + `://user:***@`; solo plataforma/modo/códigos/longitudes/requestId.
- Boundaries: `backend_auth` (HMAC, nonce 1 uso, TTL 1800s, rotación); `managedSupported()`; gates prod T-053; validación `wrong provider`; `TokenStore(provider,user_id)`.

## 4. Provider × Mode

| Provider | Independent | Managed | Notas |
|---|---|---|---|
| YouTube | SUPPORTED | SUPPORTED | scopes `youtube.force-ssl` |
| Kick | SUPPORTED | SUPPORTED | scopes `channel:write channel:read`; redirect con match exacto |
| Twitch | SUPPORTED | **UNSUPPORTED** | DCF sin secret; explícito, sin fallback |

## 5. Authentication / OAuth

- YouTube/Kick Managed: `POST /connect/<p>` → `authorization_url` (state `token_hex(16)` single-use+TTL, PKCE S256) → browser → `GET /connect/<p>/callback` → exchange server-side → `GET /connect/<p>/status`. App secrets solo backend; plugin recibe `{provider,status,account}`.
- YouTube/Kick Independent: PKCE directo con credenciales BYO del usuario.
- Twitch: Device Flow (`id.twitch.tv/oauth2/device` → user_code + `twitch.tv/activate` → polling acotado con `slow_down` → `/validate` + scope check → DPAPI). Client ID distribuible vía `OBS_TWITCH_CLIENT_ID` build-time (default vacío = BYO); jamás secret.
- Refresh: single-flight por cuenta; `invalid_grant` → reconectar. Disconnect: borrado local garantizado + revoke remoto best-effort.
- Secretos de app vs usuario vs tokens estrictamente separados (§9).

## 6. Data / Persistence

- Local (DPAPI, config dir OBS per-user): `Record{display,broadcaster,access,refresh,clientId,secret,connected}` ×3 + `backendInstall` + `connectionMode`/`backendBaseUrl`/snapshots en claro (`managedYoutube/Kick{connected,userId,display}`).
- Backend PG (`migrations/001_init.sql` + `schema_migrations`): `installations(id,secreto)`, `sessions(opaca+TTL)`, `transactions(state/verifier,600s,1 uso)`, `connections(installation,provider→account+obtained_at)`, `tokens(access+refresh)`.
- Ownership/lifetime: usuario (Disconnect lo borra; backend borra filas, sesiones se flaggean); sin purga programada; uninstall elimina payload, NO DPAPI ni filas PG.
- Reconstrucción Managed: snapshot local → `/status` al conectar; `loadStore` sin auto-fetch.

## 7. Security Model

DPAPI CurrentUser; PKCE; state/nonce single-use+TTL; HMAC-SHA256; aislamiento provider/modo (sin fallback); rate-limit (global 600/min + bootstrap/auth/connect por IP/instalación; `AllowAll` vetado en prod); redacción de logs + leak-guard; Secret Manager files-only; HTTPS; SA dedicado con solo Secret Accessor; gates prod fail-fast.
NO hace: cifrado aplicacional propio, purga programada, WAF/CORS (N/A: sin XHR cross-origin), multi-instancia distribuida, certificaciones (sin claims SOC2/ISO/GDPR).
Secretos nunca en: repo, imágenes, logs, respuestas, snapshots, C++ Managed.

## 8. Production Infrastructure [D]

GCP `obs-stream-metadata` (364043334054): Cloud Run `obs-stream-metadata-service`, `us-east5`, 1 CPU/512Mi/timeout 300s, min 0/max 1, 2ª gen, ingress público intencional (frontera en `backend_auth`), SA `obs-stream-metadata-backend` dedicado; Artifact Registry `us-east5`; Neon PG `AWS US East 2 (Ohio)`, branch `production`, DB `neondb`, pooling ON; Secret Manager (`DATABASE_URL` v1 + 4 OAuth files en mounts 1-secreto-por-directorio, `SECRET_DIRS`); PUBLIC_URL `https://obs-stream-metadata-service-364043334054.us-east5.run.app`; callbacks `/connect/{youtube,kick}/callback`; `PROVIDERS=youtube,kick`; rev sana con `/health` 200 y `/version.production`.

## 9. Build / Development

Windows + VS2022 + CMake ≥3.28 + OBS SDK 32.2.2 + Qt6 obs-deps (`buildspec.json`, versión `0.1.0`). Targets: `obs-stream-metadata` (DLL), `metadata-selfcheck`, `managed-link-test` (dev, excluido del installer), componente `obs-package` para staging NSIS. Docker: `Dockerfile.backend` (slim, no-root, `PORT`, HEALTHCHECK, SIGTERM). Backend local: `python -m backend.app` (in-memory sin `DATABASE_URL`); `STREAM_META_BACKEND_URL` solo DEV. Env prod: `ENV=production` + `DATABASE_URL` + `PUBLIC_URL` https + `SECRET_DIRS` + `PROVIDERS`. No `dev.env` (ignorado), no secretos en repo.

## 10. Testing

- `metadata-selfcheck.exe`: 125 PASS (límites, §28, DCF `twid/twpoll`, snapshots, modo, envbind).
- `python -m unittest discover -s backend/tests`: 134 OK (7 skips PG-sin-servidor por diseño; CI usa servicio `postgres:18` → 134 sin skips).
- E2E [D]: YT+Kick Managed en prod (`connected`, sin tokens), Twitch DCF real (prefill, consent, restart DPAPI, revoke 200), install/uninstall reales en OBS normal.
- CI (`ci-phase0.yml`): `phase0-gates` (docs + secret-scan) y `backend-tests` (stdlib + PG service). Negativos: 204/401/429, state expirado/replay/mismatch, wrong-provider, credenciales ausentes, TLS a medias.
- Motor de secret-scan: GNU grep (el workflow); PowerShell/.NET da falsos positivos sobre `*_token=` — no accionable.

## 11. Packaging / Installer

NSIS 3.12, `cmake/windows-installer.nsi.in` (versión de buildspec vía configure_file): `obs-stream-metadata-0.1.0-windows-x64.exe`, payload exacto (DLL + `locale/en-US.ini` + `tls/qschannelbackend.dll`), admin, valida OBS (`bin\64bit\obs64.exe`), guard si OBS abierto, uninstaller + registro (solo payload, nunca `%APPDATA%`). Instalación/upgrade/uninstall reales PASS [D]. Packaging ≠ release pública (no existe como tarea).

## 12. Documentation / Privacy

`docs/PRIVACY.md` (inventario trazable + limitaciones honestas: sesiones flag-no-purge, uninstall≠borrado backend, sin contacto designado). Findings: F-001…F-062; relevantes cerrados; INFO (en-US.ini vacío, UI-cache revoke Twitch, ramas release remotas). Docs normativas: `AGENTS.md` (investigación histórica + §50 done-definition), `ARCHITECTURE-BACKEND.md` (§18 matriz, §19 env, §20 PG), `DEPLOYMENT.md` (contrato + estado + migración Neon→Cloud SQL), `SECURITY.md`, `VALIDATION.md`, `TROUBLESHOOTING.md`, ADR-001…ADR-014.

## 13. Current Backlog

Fuente: `docs/BACKLOG.md` (única autoritativa, ADR-001).
DONE: T-001,10-13,20,30-32,35-41,43-58 (infra completa), T-060, T-061 simile.
SATISFIED DE FACTO: T-034 (por T-061). STALE/SUPERSEDED: T-042, dupe-T-058.
OPTIONAL/meta: T-002, T-033 (gate §50 GREEN evidenciado). FUTURE: T-059 (licencias/comercial).
PENDING REAL: ninguno funcional.
NEXT REQUIRED TECHNICAL TASK: NONE.

## 14. Known Findings / Limitations

Ningún BLOCKER/HIGH. INFO: en-US.ini vacío en repo (dock funciona); UI Twitch cachea revoke (tokens invalidados, HTTP 200); sesiones revocadas/expiradas se flaggean, no se purgan; contacto privacy pendiente del operador; ramas `origin/feat/*` y `release/*` sin borrar (higiene); 5 untracked ajenos en raíz (no tocar). Deuda futura: monitoreo/backups gestionados, purga programada, contador distribuido multi-instancia.

## 15. Important Design Decisions (DO NOT VIOLATE WITHOUT EXPLICIT ADR)

Un solo plugin; Independent+Managed; `ConnectionMode != environment`; Twitch Managed unsupported; sin fallback provider/modo; `Account != ManagedConn`; ManagedConn sin tokens; plugin Managed sin secrets; `loadStore` sin auto-fetch; Apply consume `Account` Independent; prod = PostgreSQL vía `DATABASE_URL` genérica (nunca `NEON_*`); secrets files-only; `backend_auth` como boundary; SQLite solo DEV/TEST; `PORT`/`0.0.0.0`-prod por entorno con override explícito.

## 16. Known Non-Goals

Twitch Managed; multichat/multistream/categorías/tags/thumbnails/analytics/chat-bots/EventSub; cuentas múltiples por proveedor; release pública (no definida); billing/pagos; HA/multi-región/K8s/Redis/réplicas; migración automática Neon→Cloud SQL; monitoring empresarial; purga automática.

## 17. Current Git Baseline [C]

- branch: `master`; HEAD = origin/master = `b2dae75` (`docs: close MVP backlog (#28)`)
- Cadena: `#20` T-055 → `#21` T-056 → `#22` T-057 → `#23` T-058 → `#24` docs E2E → `#25` installer → `#26` T-047 → `#27` T-060 → `#28` cierre MVP.
- working tree CLEAN salvo 5 untracked ajenos (no son proyecto; no tocar).
- Sin cambios locales de proyecto.

## 18. How to Use This Document

1. Lee este archivo; 2. lee `AGENTS.md`; 3. revisa `BACKLOG.md`; 4. revisa STATE/FINDINGS si el trabajo los afecta; 5. verifica Git; 6. no asumas historial de conversaciones. No reemplaza código ni docs normativas.

## 19. Context Recovery Protocol

1–7 como §18; 8. identifica objetivo; 9. verifica dependencias; 10. ejecuta solo lo autorizado; 11. valida (tests+scan); 12. actualiza este archivo si cambia materialmente el estado.

## 20. Context Update Policy

Actualizar ante cambios materiales (arquitectura, MVP/modos, seguridad, persistencia, producción, build/installer, baselines, backlog, ADRs). NO por commits triviales ni info temporal.

## 21. Inconsistencias / UNKNOWN

- BACKLOG contiene estados `superseded`/`duplicada` fuera de su enum declarado (intencional, documentado en filas).
- Ramas remotas `origin/feat/*` históricas sin borrar [I: sin uso activo].
- `en-US.ini` vacío en repo [C] con dock funcional [C].
- Costes free-tier GCP/Neon: modelo documentado sin precios (ver DEPLOYMENT).
- UNKNOWN: nada bloqueante para operar el proyecto.
