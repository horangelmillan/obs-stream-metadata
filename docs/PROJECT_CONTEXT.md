# PROJECT_CONTEXT — obs-stream-metadata (snapshot operativo)

> Snapshot al commit `c6b5b26` (master == origin/master). Punto de partida
> para sesiones nuevas. No reemplaza código ni docs normativas.
> Marcas: [C] confirmado en repo ahora mismo · [D] documentado (E2E/infra
> de sesiones previas, no re-ejecutado aquí) · [I] inferido · [P] pendiente.

## 1. Project Identity

Plugin Qt6 para OBS Studio 32.2.2 (Windows x64) + backend Python que
gestionan **título/descripción de streams** en Twitch, YouTube, Kick y
Facebook (perfil; Page diferida).
Componentes: plugin (`src/`, dock Qt integrado) + backend (`backend/`,
Cloud Run + Neon PostgreSQL). Alcance actual: MVP cerrado + hardening
comercial F-C1/F-C2/F-C4 + Facebook perfil + T-075/T-076; un solo
plugin, dos modos (Independent/Managed). Sin multichat, multistream,
categorías, analytics ni backend remoto distinto del descrito.

## 2. Current Status

- MVP STATUS: CLOSED [D] · §50: GREEN [D] · FUNCTIONAL GAPS: NONE [D]
- BLOCKERS: NONE · HIGH: NONE
- CURRENT DEVELOPMENT CYCLE: ninguno (último: T-076 hecha 2026-10-24 [D]; sin ciclo abierto)
- NEXT REQUIRED TECHNICAL TASK: NONE [D]
- Baselines en este checkout [C, conteo estático sin ejecutar]: backend 250 `def test_*`
  en `backend/tests/` (coherente con "250 OK" documentado en T-075/T-076),
  selfcheck 180 invocaciones `CHECK(` en `src/metadata_selfcheck.cpp`
  (+1 línea `#define`; conteo estático [C]) (último
  documentado: SELFCHECK OK, T-073); tree con cambios documentales sin
  commitear (ver §17) + `staging/` preexistente sin seguimiento (no
  modificado); `master==origin/master==c6b5b26`.

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
| YouTube | SUPPORTED | SUPPORTED | scopes `youtube.force-ssl`; combo con default al activo/próximo (T-076) |
| Kick | SUPPORTED | SUPPORTED | scopes `channel:write channel:read`; redirect con match exacto |
| Twitch | SUPPORTED | SUPPORTED | Independent: DCF sin secret; Managed: auth-code vía backend + Apply server-side (E2E de facto T-063) |
| Facebook (perfil) | SUPPORTED | SUPPORTED | Solo perfil (`publish_video`; DCF-like PKCE BYO-app / adapter + endpoint Managed); crea su propio LiveVideo; Pages diferidas sin fechas (F-072/F-079); estado de la app de Meta y requisitos de acceso para las pruebas del operador, pendientes de confirmar |

## 5. Authentication / OAuth

- YouTube/Kick/Twitch/Facebook Managed: `POST /connect/<p>` → `authorization_url` (state `token_hex(16)` single-use+TTL, PKCE S256; Twitch sin PKCE por docs) → browser → `GET /connect/<p>/callback` → exchange server-side → `GET /connect/<p>/status`. App secrets solo backend; plugin recibe `{provider,status,account}`.
- YouTube/Kick/Twitch Independent: OAuth directo con credenciales BYO
  del usuario (YT/Kick PKCE; Twitch Device Flow).
- Twitch: Device Flow (`id.twitch.tv/oauth2/device` → user_code + `twitch.tv/activate` → polling acotado con `slow_down` → `/validate` + scope check → DPAPI). Client ID distribuible vía `OBS_TWITCH_CLIENT_ID` build-time (default vacío = BYO); jamás secret.
- Refresh: single-flight por cuenta; `invalid_grant` → reconectar. Disconnect: borrado local garantizado + revoke remoto best-effort.
- Secretos de app vs usuario vs tokens estrictamente separados (§9).

## 6. Data / Persistence

- Local (DPAPI, config dir OBS per-user): `Record{display,broadcaster,access,refresh,clientId,secret,connected}` ×4 (tw/yt/kk/fb) + `facebookLiveId` en claro + `backendInstall` + `connectionMode`/`backendBaseUrl`/snapshots en claro (`managed{Twitch,Youtube,Kick,Facebook}{connected,userId,display}`).
- Backend PG (`migrations/001_init.sql` + `schema_migrations`): `installations(id,secreto)`, `sessions(opaca+TTL)`, `transactions(state/verifier,600s,1 uso)`, `connections(installation,provider→account+obtained_at)`, `tokens(access+refresh)`.
- Ownership/lifetime: usuario (Disconnect lo borra; backend borra filas; F-C2 borrado total por instalación + F-C4 purga diaria de sesiones/transacciones expiradas vía `POST /ops/purge`); uninstall elimina payload, NO DPAPI ni filas PG.
- Reconstrucción Managed: snapshot local → `/status` al conectar; `loadStore` sin auto-fetch.

## 7. Security Model

DPAPI CurrentUser; PKCE; state/nonce single-use+TTL; HMAC-SHA256; aislamiento provider/modo (sin fallback); rate-limit (global 600/min + bootstrap/auth/connect por IP/instalación; `AllowAll` vetado en prod); redacción de logs + leak-guard; Secret Manager files-only; HTTPS; SA dedicado con solo Secret Accessor; gates prod fail-fast.
NO hace: WAF/CORS (N/A: sin XHR cross-origin), multi-instancia distribuida, certificaciones (sin claims SOC2/ISO/GDPR).
Sí hace (post-MVP): cifrado aplicacional Fernet de tokens en reposo (F-C1), purga programada diaria (F-C4).
Secretos nunca en: repo, imágenes, logs, respuestas, snapshots, C++ Managed.

## 8. Production Infrastructure [D]

GCP `obs-stream-metadata` (364043334054): Cloud Run `obs-stream-metadata-service`, `us-east5`, 1 CPU/512Mi/timeout 300s, min 0/max 1, 2ª gen, ingress público intencional (frontera en `backend_auth`), SA `obs-stream-metadata-backend` dedicado; Artifact Registry `us-east5`; Neon PG `AWS US East 2 (Ohio)`, branch `production`, DB `neondb`, pooling ON; Secret Manager (DATABASE_URL + OAuth files + TOKEN_ENCRYPTION_KEY + OPS_PURGE_TOKEN en mounts 1-secreto-por-directorio, `SECRET_DIRS`, 10 volúmenes); `PROVIDERS=youtube,kick,twitch,facebook`; rev documentada `00036-cjj` con `/health` 200 y `/version.production` (subagente GCP, verificado 2026-10-24 por lectura; no re-verificado en esta sesión).
Drift conocido [D]: la URL pública vigente es `https://obs-stream-metadata-service-mdurv2xfsa-ul.a.run.app`; `DEPLOYMENT.md`, `tools/deploy.ps1` y el job `obs-stream-metadata-purge` (us-east1) aún referencian la URL anterior `https://obs-stream-metadata-service-364043334054.us-east5.run.app` — no asumir que sigue válida. Callbacks `/connect/{youtube,kick,twitch,facebook}/callback`.
Distinguir [I]: app de Meta en Development sin App Review (solo usuarios con rol) ≠ backend desplegado en Cloud Run.

## 9. Build / Development

Windows + VS2022 + CMake ≥3.28 + OBS SDK 32.2.2 + Qt6 obs-deps (`buildspec.json`, versión `0.1.0`). Targets: `obs-stream-metadata` (DLL), `metadata-selfcheck`, `managed-link-test` (dev, excluido del installer), componente `obs-package` para staging NSIS. Docker: `Dockerfile.backend` (slim, no-root, `PORT`, HEALTHCHECK, SIGTERM). Backend local: `python -m backend.app` (in-memory sin `DATABASE_URL`); `STREAM_META_BACKEND_URL` solo DEV. Env prod: `ENV=production` + `DATABASE_URL` + `PUBLIC_URL` https + `SECRET_DIRS` + `PROVIDERS`. No `dev.env` (ignorado), no secretos en repo.

## 10. Testing

- `metadata-selfcheck.exe`: 180 invocaciones `CHECK(` contadas en fuente [C] (+1 `#define`; último resultado documentado: SELFCHECK OK, T-073 [D]).
- `python -m unittest discover -s backend/tests`: 250 métodos `def test_*` contados en fuente [C] (último resultado documentado: 250 OK, T-076 2026-10-24 [D]; skips PG-sin-servidor por diseño; CI usa servicio `postgres:18`).
- E2E [D]: YT/Kick/Twitch Managed en prod (`connected`, sin tokens), FB Managed perfil (create+ID, status, Apply+read-back, end, delete, restart, Disconnect), Twitch DCF real, install/uninstall reales en OBS normal.
- CI (`ci-phase0.yml`): `phase0-gates` (docs + secret-scan) y `backend-tests` (stdlib + PG service). Negativos: 204/401/429, state expirado/replay/mismatch, wrong-provider, credenciales ausentes, TLS a medias.
- Motor de secret-scan: GNU grep (el workflow); PowerShell/.NET da falsos positivos sobre `*_token=` — no accionable.

## 11. Packaging / Installer

NSIS 3.12, `cmake/windows-installer.nsi.in` (versión de buildspec vía configure_file): `obs-stream-metadata-0.1.0-windows-x64.exe`, payload exacto (DLL + `locale/en-US.ini` + `tls/qschannelbackend.dll`), admin, valida OBS (`bin\64bit\obs64.exe`), guard si OBS abierto, uninstaller + registro (solo payload, nunca `%APPDATA%`). Instalación/upgrade/uninstall reales PASS [D]. Packaging ≠ release pública (no existe como tarea).

## 12. Documentation / Privacy

`docs/PRIVACY.md` (inventario trazable + limitaciones honestas; nota:
declara "Twitch Managed is unsupported", desactualizado frente a T-062 —
ver STATE discrepancias residuales). Findings: F-001…F-086; relevantes
cerrados. Docs normativas: `AGENTS.md` (reglas duraderas + jerarquía
§55), `ARCHITECTURE-BACKEND.md` (§18 matriz —nota: fila Twitch
desactualizada, ver STATE—, §19 env, §20 PG), `DEPLOYMENT.md`
(contrato + estado + migración Neon→Cloud SQL), `SECURITY.md`,
`VALIDATION.md`, `TROUBLESHOOTING.md`, ADR-001…ADR-015.

## 13. Current Backlog

Fuente: `docs/BACKLOG.md` (única autoritativa, ADR-001).
DONE: T-001,10-13,20,30-33,35-41,43-58,60-65,67,68,70-76 (incl. F-C1/F-C2/F-C4, Facebook T-069–T-073, T-074, T-075, T-076).
SATISFIED DE FACTO: T-034 (por T-061), T-062 (por T-063).
STALE/SUPERSEDED: T-042, dupe-T-058.
OPTIONAL/meta: T-002 (en-progreso, informe; no bloquea).
FUTURE: T-059 (licencias/comercial), T-066 (firma).
PENDING REAL: ninguno funcional.
PENDING OPERADOR [P, intención comunicada por el usuario 2026-10-26]:
pruebas de Facebook con usuarios reales y streams reales durante octubre
de 2026 (incl. E2E del port lab→plugin Fase D + resolución del conflicto
de alcance AGENTS.md §§23/51 antes de implementar producto [P]; T-077/T-078
siguen en investigación, BACKLOG canónico; detalle en VALIDATION.md
Fase D y STATE). Relay Fases 2–6 (2026-10-26) [D delegada, no
re-ejecutada aquí]: relay.h/cpp + relay_manager + vista + package.ps1
con relay/MIT; build RelWithDebInfo OK, selfcheck 315 PASS/0 FAIL,
diff-check 0, secret-scan limpio, WhatIf OK; T-078 sigue en
investigación sin cambio de estado [C contra BACKLOG]. Pendientes [P]:
E2E operador instalar→relay→forward→GoLive→LIVE, métricas F-099,
audiencia F-093, build E2E del instalador con descarga real, capturas
visuales. Fix 2026-10-26 [D delegada de la sesión implementadora, no
re-ejecutado aquí; C estático en sesión documental:
`src/relay_manager.cpp:79` usa `obs_get_module_data_path` y
`src/metadata_selfcheck.cpp` contiene 7 checks `relay-find-data-*`]:
`findBinary` buscaba junto al programa pero el instalador pone el
relay en el data path; ahora busca primero en data-path/`relay/`
(causa del "binary missing" del operador con instalador nuevo);
build OK, selfcheck 322/0 con relay-find-data 7/7, diff-check y scan
limpios. T-078 sigue en investigación sin cambio de estado [C contra
BACKLOG]. Pendiente [P]: operador reinstala/reabre OBS → Start relay
→ E2E. Sin afirmar Live/App Review/requisitos (sin evidencia);
separado del despliegue del backend en Cloud Run; requiere confirmación
y alcance explícito antes de ejecutar nada.
NEXT REQUIRED TECHNICAL TASK: NONE.

## 14. Known Findings / Limitations

Ningún BLOCKER/HIGH. INFO: en-US.ini vacío en repo (dock funciona); UI Twitch cachea revoke (tokens invalidados, HTTP 200); contacto privacy designado (`horangelmillan@gmail.com`, F-C2); ramas `origin/feat/*` y `release/*` sin borrar (higiene); `staging/` en raíz preexistente sin seguimiento, no modificado. Deuda futura: monitoreo/backups gestionados, contador distribuido multi-instancia, firma (T-066), licencias (T-059).

## 15. Important Design Decisions (DO NOT VIOLATE WITHOUT EXPLICIT ADR)

Un solo plugin; Independent+Managed; `ConnectionMode != environment`; Twitch Managed soportado (T-062; la mención contraria en §4/§15 antiguos y en ARCH §18/PRIVACY está desactualizada, ver STATE); sin fallback provider/modo; `Account != ManagedConn`; ManagedConn sin tokens; plugin Managed sin secrets; `loadStore` sin auto-fetch; prod = PostgreSQL vía `DATABASE_URL` genérica (nunca `NEON_*`); secrets files-only; `backend_auth` como boundary; SQLite solo DEV/TEST; `PORT`/`0.0.0.0`-prod por entorno con override explícito.

## 16. Known Non-Goals

Multichat/multistream/categorías/tags/thumbnails/analytics/chat-bots/EventSub; cuentas múltiples por proveedor; release pública (no definida); billing/pagos; HA/multi-región/K8s/Redis/réplicas; migración automática Neon→Cloud SQL; monitoring empresarial; integración de Pages de Facebook (diferida, sin fechas; estado de la app de Meta y requisitos de acceso para las pruebas del operador pendientes de confirmar, sin afirmar Live, App Review ni acciones iniciadas); firma del instalador (T-066 aparcada).

## 17. Current Git Baseline [C]

- Baseline al inicio de la tarea documental (2026-10-24) [C]: HEAD =
  origin/master = `c6b5b26`; árbol limpio salvo `staging/`
  preexistente sin seguimiento (no modificado).
- Estado actual del checkout [C]: mismo HEAD; 8 archivos documentales
  modificados sin commitear (`AGENTS.md`, `README.md`, `docs/STATE.md`,
  `docs/PROJECT_CONTEXT.md`, `docs/DEPLOYMENT.md`,
  `docs/VALIDATION.md`, `docs/TOOLS.md`, `docs/WORKFLOW.md`);
  `staging/` preexistente sin seguimiento, no modificado; sin
  commit/push en esta tarea.
- Cadena reciente [C]: `#54` T-075 → `#55` T-076 → `#56` docs T-076 → `#57/#58` subagente GCP.

## 18. How to Use This Document

1. Lee este archivo; 2. lee `AGENTS.md`; 3. revisa `BACKLOG.md`; 4. revisa STATE/FINDINGS si el trabajo los afecta; 5. revisa `docs/notes/` (ideas vivas de mejora; convención en su `README.md`; las notas nuevas van ahí numeradas, nunca en carpetas nuevas); 6. verifica Git; 7. no asumas historial de conversaciones. No reemplaza código ni docs normativas.

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
