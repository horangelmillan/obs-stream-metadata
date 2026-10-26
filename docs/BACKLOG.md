# BACKLOG — tareas (mantenido por agentes)

Formato: `T-### | estado | fase | descripción | aceptación | links`.
Estados: pendiente / en-progreso / bloqueada / hecha / deuda / investigación / futuro.

| ID | Estado | Fase | Tarea | Aceptación | Links |
|---|---|---|---|---|---|
| T-001 | hecha | 0 | Harness Phase 0: docs + CI + git | PR mergeado, CI verde | ADR-001/002/003 |
| T-002 | en-progreso | 0 | Informe final 13 puntos Phase 0 | informe entregado al usuario | — |
| T-010 | hecha | 1 | Definir fases 1-8 reales (alcance OBS/Qt/OAuth) | `PHASES.md` actualizado + ADR | F-001 |
| T-011 | hecha | 1 | Confirmar toolchain Windows (VS2022/CMake3.28+/Qt6/OBS SDK) | evidencia versiones | F-002 |
| T-012 | hecha | 1 | Instalar toolchain según ADR-005 + validar (build template en P1) | VS17.14/MSVC19.44/ATL/SDK22621/CMake3.31.6/Git2.49.0 verificados; Qt no manual; configure CMake ok | ADR-005 |
| T-013 | hecha | 1 | Bootstrap P1: buildspec 32.2.2 + compilar + cargar en OBS 32.2.2 | configure ok + DLL x64 + load/unload en log ×2 + sin crash | ADR-007, F-009/F-010/F-011/F-012 |
| T-020 | hecha | 2 | PoC dock Qt6 mínimo (add_dock_by_id, sin OAuth) | dock registrado + visible + geometría restaurada + 5 ciclos limpios en OBS 32.2.2 | F-013, F-014, AGENTS §5-7 |
| T-030 | hecha | 3 | PoCs OAuth + título por proveedor (Twitch/YouTube/Kick) + desc YouTube | lógica offline PASS 28/28 + triple LIVE PASS (Twitch/YouTube/Kick) con read-back | AGENTS §10-14, docs/T030-POC.md, F-015–F-024 |
| T-031 | hecha | 4 | Dock unificado: título/desc + apply por plataforma + resultado independiente | matriz viva §19 en verde (T1–T20: OAuth+updates reales, parcial, validación, 401, async, persistencia) + fixes F-026/F-027/F-028 | F-001, F-004, F-025–F-028 |
| T-032 | hecha | 5 | Hardening: storage seguro OS, revoke en Disconnect, backoff 429/5xx, fin de env-vars, distribución secrets F-017/F-019 | sobrevive reinicio sin plaintext; revocación verificada por proveedor; 429 sin reintento agresivo; cero secretos en repo/logs; matriz T1–T20 sin regresión | AGENTS §16-18, §26-29, ADR-008, F-029–F-032 |
| T-033 | hecha | 6 | Testing integración + negativos AGENTS §39-40 | gate §50 GREEN (matriz 22 criterios PASS, T-050/51/52, E2E YT/Kick/Twitch, 125 selfcheck + 134 backend) | AGENTS §39-40, §50 |
| T-034 | hecha | 7 | Packaging/release instalador plugin | satisfecha de facto por T-061 (NSIS .exe + install/upgrade/uninstall reales PASS, PR #25) | template wiki Distribute |
| T-035 | hecha | 6 | Prueba empírica YouTube secretless (Desktop + PKCE + loopback + `youtube.force-ssl`, sin secret) | FAIL: `400 invalid_request: client_secret is missing` con auth real; secret exigido en esta configuración | F-034 → F-038 |
| T-036 | hecha | 6 | Prueba empírica Kick secretless (auth + PKCE + localhost, sin secret) + control con secret | FAIL secretless (`400` vacío) + control `200` con secret; secreto aislado como causa | F-036 → F-039 |
| T-037 | hecha | 6 | Investigación distribución Client ID Google (política open-source/release privada/backend) | Sin permiso oficial para secreto en binario público; release-privada no resuelve extracción; backend como vía conforme | F-035 → F-040 |
| T-038 | hecha | 6 | Decisión arquitectura final OAuth distribuido | ADR-009 MODIFY + `ARCHITECTURE-BACKEND.md` | F-033–F-041 |
| T-039 | hecha | 6 | ADR-009 arquitectura OAuth final | `docs/DECISIONS/ADR-009-centralized-backend-architecture.md` aprobado en T-038 | T-038 |
| T-040 | hecha | 0 | Normalizar `core.autocrlf` y convención `master` vs `main` | ADR + config | — |
| T-041 | hecha | 6 | [Independent] UX modal: selector Independent/Managed + persistencia/migración modo (sin wiring Managed) | selector + `connection_mode` + legacy→Independent idempotente + 89 selfchecks | ADR-012 |
| T-042 | superseded | 7 | [Infra] Arquitectura backend Kick — SUPERSEDED en la práctica por T-046 (adapter + live PASS); se conserva por historia, cierre formal pendiente | alcance conceptual aprobado | F-036 |
| T-043 | hecha | 7 | Backend foundation (servicio HTTPS + DB mínima + secret manager + dominio/TLS) | fundación stdlib `backend/` en verde: 27 tests, health/ready/version, kernel/ports/adapters aislados, sin secretos | ADR-009 |
| T-044 | hecha | 7 | Auth plugin↔backend (identidad instalación + sesiones cortas + rate-limit, sin secreto permanente) | ADR-010 + `/auth/*` + cliente Qt async + 43 tests + build/DLL/selfcheck en verde | ADR-009, ADR-010 |
| T-045 | hecha | 7 | Adapter OAuth YouTube en backend (exchange/refresh/revoke + verificación Google) | ADR-011 + live PASS (connect/callback/exchange/identidad/disconnect) + 57 tests | ADR-009, ADR-011 |
| T-046 | hecha | 7 | Adapter OAuth Kick en backend (exchange/refresh/revoke) | ConnectService genérico + live PASS (connect/callback/exchange/identidad/disconnect) + 69 tests | ADR-009 |
| T-047 | hecha | 7 | Twitch directo endurecido (Client ID distribuido/build-time + DCF, sin secret) | `OBS_TWITCH_CLIENT_ID` + `twitchClientId`/`classifyTwPoll` + 10 selfchecks; DCF real E2E (prefill, consent, `Connected as`, restart, revoke 200); Twitch Managed sigue unsupported | F-015, F-033 |
| T-048 | hecha | 6 | [Managed] UX modo Administrado: wiring `backend_auth.*` + connect-vía-backend + status | Managed YT/Kick live PASS C++→backend→provider + 70 tests | ADR-012 |
| T-049 | hecha | 6 | [Common] Modelo modal: `meta::ConnectionMode` + default Independent + strings canónicos + 7 checks (sin UI/wiring/persistencia) | selfcheck 75/75 + backend 69/69 + build/scan en verde | ADR-012 |
| T-050 | hecha | 6 | Matriz Provider × Mode central (ARCHITECTURE-BACKEND §18, vigente) | 6 celdas con estado real; Twitch Managed = pendiente, sin falso soporte | ADR-012 |
| T-051 | hecha | 7 | Persistencia ManagedConn (snapshots `managed_youtube/kick` en claro + reconstrucción vía `/status`) | roundtrip/migración/idempotencia + backendInstall intacto + 18 CHECKs (17 ejecutados en Windows, 1 solo-no-Windows) | ADR-012 |
| T-052 | hecha | 6 | Batería cross-mode (aislamiento provider/modo/secretos/sesión/persistencia + checklist operador) | 3 tests backend nuevos + reutiliza batería T-051 + lives YT/Kick PASS; GUI-clicks operador PENDING | ADR-012 |
| T-053 | hecha | 7 | [Infrastructure] Separación identidad DEV/PROD (env explícito + gates prod + binding instalación↔backend) | env inválido fail-fast, prod rechaza DEVELOPMENT_ONLY + HTTP, `/version.env`, mismatch→re-bootstrap; 15 backend + 9 selfchecks nuevos | ADR-012 |
| T-054 | hecha | 7 | [Infrastructure] Endurecimiento producción backend (SQLite 0600 + secretos por fichero + TLS builtin + gates + DEPLOYMENT.md + ADR-013) | production-capable con salvedad (no deployed); 104 backend + 115 selfcheck en verde; prod-smoke local OK | ADR-012, ADR-013 |
| T-055 | hecha | 7 | [Infrastructure] PostgreSQL portable + Cloud Run/Neon (DATABASE_URL genérica, migrations, pgstores, pool, Dockerfile, ADR-014) | 116 backend (12 PG reales) + 115 selfcheck en verde; despliegue pendiente operador | ADR-012, ADR-014 |
| T-057 | hecha | 7 | [Infrastructure] Contrato Secret Manager ↔ FileSecretStore (volúmenes, SECRET_DIR, verificación boot) | rev-00002 documentada; 5 tests nuevos; 126 backend en verde; reintento pendiente | ADR-012, ADR-014 |
| T-059 | futuro | 7 | [Commercial] Diseño suscripción/licencias (OPEN: proveedor pagos, planes, expiración, grace, límites) | decisión documentada, sin implementar pagos | ADR-012 |
| T-060 | hecha | 7 | [Security] Privacy policy + inventario de datos Managed (retención, revoke, eliminación, logs, incidentes) | `docs/PRIVACY.md` (inventario trazable a código; sin secretos ni claims legales) | ADR-012 |
| T-058 | hecha | 7 | [Infrastructure] Múltiples directorios de secretos (CompositeSecretStore + SECRET_DIRS, Cloud Run 1-secreto-por-directorio) | 8 tests nuevos; 134 backend en verde; compat single-DIR intacta | ADR-012, ADR-014 |
| T-058 | duplicada | 7 | [Security] Privacy policy + inventario de datos Managed (retención, revoke, eliminación, logs, incidentes) | DUPLICADA obsoleta de T-060 (la tarea real es T-060, hecha); se conserva por historia | ADR-012 |
| T-056 | hecha | 7 | [Infrastructure] Bind production 0.0.0.0 (Cloud Run) + estado de despliegue | host por entorno + override explícito + 5 tests; 121 backend en verde; despliegue pendiente de reintento | ADR-012 |
| T-061 | hecha | 7 | [Installer] Windows NSIS .exe + install real en OBS (encargo nombrado T-057; ID T-061 por colisión con T-057 existente) | NSIS 3.12 + install/upgrade/uninstall reales PASS + PR #25 mergeada | — |
| T-062 | hecha | 8 | [Commercial] FASE 2.1-C.1 + Apply Managed Twitch (diagnóstico incompatibleParameters, mapper robusto, endpoints metadata twitch/youtube, .exe FASE21D) | satisfecha de facto por T-063 (sonda prod Apply YT/Kick/Twitch OK 2026-09-17; implementación PR #32) | docs/FASES-COMERCIAL.md |
| T-063 | hecha | 8 | [Commercial] F-C1 cifrado de tokens en reposo (AEAD Fernet + Secret Manager + migración 002 + backfill, sin romper sesiones) | 190 backend OK; dump solo `v1:`; deploy rev-00016 dual-read; sonda prod Apply YT/Kick/Twitch OK 2026-09-17 | docs/FASES-COMERCIAL.md, docs/superpowers/plans/2026-09-17-f-c1-token-encryption.md |
| T-064 | hecha | 8 | [Commercial] F-C2 borrado real de datos + privacidad operativa (endpoint, UI, uninstaller, contacto) | 198 backend OK + 14 PG reales; erase PG: conteos a cero + `disconnected` + bearer rechazado; selfcheck OK; secret-scan limpio | docs/FASES-COMERCIAL.md, docs/superpowers/plans/2026-09-18-f-c2-borrado-total.md |
| T-065 | hecha | 8 | [Commercial] F-C4 operativa mínima (purga programada, backups+restore, alertas) | 209 backend OK + PG reales; job con conteos + restore verificado + señales de alerta probadas (recepción en canal: pendiente operador) | docs/FASES-COMERCIAL.md, docs/superpowers/plans/2026-09-19-f-c4-operativa-minima.md |
| T-066 | futuro | 8 | [Commercial] F-C3 firma del distribuible OV-IV (aparcada hasta tracción; ~$220–350/año) | signtool + publisher visible + payload firmado | docs/FASES-COMERCIAL.md |
| T-067 | hecha | 7 | [Packaging] Carpeta `dist/` para distribuibles + flujo `tools/package.ps1 -Flavor` (cierra nota 001) | `dist/*.exe + .sha256` reproducible desde `build_x64/pkg/`; `dist/` ignorada; cero `.exe` trackeados; install/uninstall desde `dist/` PASS | docs/notes/001-builds-por-comando-segun-destino.md, docs/INSTALLER.md |
| T-068 | hecha | 8 | [Managed] Apply Managed Kick (backend `/metadata/kick` + Apply server-side, contraparte de T-062) | E2E operador PASS (connect Managed → Apply título → read-back en directo); 215 backend OK + selfcheck 125/125; deploy rev-00027; `tools/deploy.ps1` + fix loopback explícito incluidos | docs/notes/007-portal-auth-managed-suscripciones.md |
| T-069 | hecha | 9 | [Facebook] FB-0 Investigación sin código (OAuth, endpoints, go-live/stop, tokens, errores, E2E viable + App Review indie CO) | brief aprobado `docs/T0FB-RESEARCH.md` (mapa Step 0 + 8 secciones + D1–D14; F-064) | docs/superpowers/plans/2026-09-20-facebook-metadata-livestatus.md |
| T-070 | hecha | 9 | [Facebook] FB-1 PoC offline + sondas manuales (estilo T-030) | 28/28 offline + B' update/read-back PASS + C go-live/stop/delete PASS (A/H1 negativas documentadas F-065/F-066); runner migrado a PKCE | docs/superpowers/plans/2026-09-20-facebook-metadata-livestatus.md |
| T-071 | hecha | 9 | [Facebook] FB-2 Independent (dock directo BYO-app) | build + selfchecks + E2E operador PASS (Connect PKCE, update+read-back, restart, Disconnect+revoke, limpieza; F-071–F-075; Page y creación-por-dock → FB-4) | docs/superpowers/plans/2026-09-20-facebook-metadata-livestatus.md |
| T-072 | hecha | 9 | [Facebook] FB-3 Managed (adapter + endpoint + dock) | E2E operador PASS (Connect, update+read-back, restart, Disconnect, limpieza); suite 244 OK + deploy rev-00032-7n9 | docs/superpowers/plans/2026-09-20-facebook-metadata-livestatus.md |
| T-073 | hecha | 9 | [Facebook] FB-4 UI y cierre (indicador+on/off si avalado, warnings, packaging, ADR) | E2E operador PASS Managed perfil (Connect, create+ID persistido, status LIVE, Apply+read-back idéntico, end, delete, restart, Disconnect, visibilidad F-086); suite 249 OK + selfcheck OK + commercial b00baabe…; PR #52 mergeado | docs/superpowers/plans/2026-09-20-facebook-metadata-livestatus.md |
| T-074 | hecha | 7 | Outage Managed 20–22/09: fuga pool (`self._released` compartido) + cliente que clasificaba 401/500 como network + `/ready` sin DB | test concurrencia rojo→verde, 218 backend OK, deploy rev-00028 + sesión real OK, commercial re-empaquetado, prueba viva operador OK | F-068, F-069, F-070 |
| T-075 | hecha | 10 | [Managed] Auto-reconnect cómodo: sin pedir Connect por expiración normal (mensajes desglosados + revalidación silenciosa + 1 reintento acotado) | backend 250 OK + pool fix rev-00035 + E2E operador PASS (Connect/Apply/restart); PR #54 mergeado | docs/superpowers/plans/2026-10-24-auto-reconnect-comodo.md |
| T-076 | hecha | 10 | [UX] Facebook gris sin conexión + YouTube default listo (activo/próximo, fechas, tooltip) | backend 250 OK + PR #55 mergeado + E2E operador PASS (combo elige 8getO, sin error FB) + deploy rev-00036 | docs/superpowers/plans/2026-10-24-fb-gris-yt-default.md |
| T-077 | investigación | 10 | [Facebook] Validar flujo propuesto LiveVideo propio → ingestión OBS/Aitum → publicación en perfil | Confirmar metadatos/audiencia al crear, señal RTMPS sobre el ID devuelto, transición a LIVE y visibilidad; resolver el conflicto de alcance `AGENTS.md` §23/§51 antes de implementar | `docs/FACEBOOK-LIVE-API-FLOW.md`; F-065/F-066/F-067/F-080; E2E lab 2026-10-26 (VALIDATION T-077; F-092–F-095): creates/read-back/update/GoLive-LIVE/End/DELETE PASS; persistencia NO, privacy pendiente prod, alcance §§23/51 pendiente — sigue en investigación |
| T-078 | investigación | 10 | [Facebook] MediaMTX relay local (URL fija + forward en caliente hacia LiveVideo propio) | Forward RTMPS a LiveVideo de prueba PASS + cambio de destino en caliente sin reconfigurar Aitum PASS + métricas lab (latencia/CPU/recovery) registradas + aviso de licencia MIT incluido si se empaqueta. E2E 2026-10-26 [D]: forward PASS + caliente PASS (2 emisiones LIVE+DIRECTO sin tocar Aitum, PATCH sin reinicio) + MIT identificado (empaquetado pendiente); métricas parciales (~57-59 MB WS + H264/inbound/0-errores; latencia/CPU/recovery pendientes, F-099); audiencia "Solo yo" pendiente prod — Fase 1 hecha 2026-10-26 (ADR-016: decisión + alcance + criterios; §§23/51 con excepción acotada; flujo como contrato vigente); sigue en investigación hasta E2E de aceptación, sin cambio de estado — Fases 2–6 port 2026-10-26 [D delegada]: relay.h/cpp + relay_manager + vista + package.ps1 con relay/MIT; build RelWithDebInfo OK, selfcheck 315 PASS/0 FAIL, diff-check 0, secret-scan limpio, WhatIf OK; E2E operador + métricas F-099 + audiencia F-093 + build E2E instalador + capturas pendientes [P]; simplificación FB Fase A+B 2026-10-26
  [D delegada]: auto-prepare (auto-relay si FB conectado, valida ID
  crea-nuevo + borra anterior UNPUBLISHED, fallback reutilizar honesto,
  Apply verificación silenciosa, señal emisor API local, auto-prepare
  tras End con cooldown) + vista (línea Facebook listo, Avanzado
  colapsado, botones condicionales, indicador señal); build OK,
  selfcheck 338/0, diff/scan limpios; pendientes E2E operador +
  capturas visuales + instalador con relay+fix [P]; plan consolidado
  v2 2026-10-26 [P, PLAN NO IMPLEMENTADO: arranque E1/E2/E3 sin
  timers + Go-live auto + semáforo + contador cooldown + auto-End +
  auto-prepare; detalle en VALIDATION.md]; validación dos-directos
  2026-10-26 [VALIDACIÓN/PLAN, NO implementado: pre-create NO +
  auto-End NO + cola diferida NO + verify/refire SÍ + Apply blinda
  SÍ + presupuestos 1 uso + ~30 textos + principio no-técnico;
  detalle en VALIDATION.md]; ajuste v3 2026-10-26 [P, PLAN NO
  IMPLEMENTADO: evento primario runOnOnline/runOnOffline + poll 3 s
  + confirmación 30 s + adaptativo 3→2→1 2-de-3 + check audiencia
  global OFF plaintext F-093 + contador ~mm:ss + semáforo sin texto
  + auto-End solo LIVE auto-iniciado + pre-creación + cola 1 disparo;
  detalle en VALIDATION.md]; sigue en
  investigación | `docs/MEDIAMTX-RELAY-FLOW.md`; F-092–F-095; VALIDATION T-078; F-096–F-099; T-077 intacta en investigación |
| T-079 | hecha | 10 | [Managed-users-v1] Fase 1 — modelo `users` + migración 003 | `backend/users.py` + `backend/migrations/003_users.sql` + `test_users_model` en `0f015a3`; suites 299→337 OK [D: mensaje + plan] | `docs/superpowers/specs/2026-10-26-managed-user-accounts-design.md` §3; `docs/superpowers/plans/2026-10-26-managed-user-accounts.md` Task 1 |
| T-080 | hecha | 10 | [Managed-users-v1] Fase 2 — endpoints users + auth doble | `POST /users/signup|login|logout` + `GET /users/me` + 401 `managed auth required` en connect/metadata [C: código `0f015a3`]; suites OK [D] | spec §4; plan Task 2 |
| T-081 | hecha | 10 | [Managed-users-v1] Fase 3 — gate login en el plugin | sesión de usuario solo-memoria, Managed sin login deshabilitado [C: `src/backend_auth.*`; D: spec §5]; f4 en verde [D] | spec §5; plan Task 3 |
| T-082 | hecha | 10 | [Managed-users-v1] Fase 4 — selfcheck f4/f5 | checks `f4-*`/`f5-*` en `src/metadata_selfcheck.cpp` [C]; OK [D: mensaje `0f015a3`] | plan Task 4 |
| T-083 | hecha | 10 | [Managed-users-v1] Fase 5 — `POST /users/erase` en cascada + ajustes suites | cascada usuarios→instalaciones→sesiones→conexiones→tokens no compartidos, solo conteos, separado de F-C2 [C]; suites 299→337 OK [D] | spec §§5–6; plan Task 5 |
| T-084 | pendiente | 10 | [Managed-users-v1] Fase 6 — E2E multi-PC + backup/rollback | acta E2E (adopción sin reset + aislamiento + Apply en ambas) + `pg_dump` previo y rollback verificado; sin ello no hay deploy | spec §8; plan Task 6 (pasos listos) |
| T-085 | hecha | 10 | [Managed] Logout limpia + login no hereda ajeno (purga de links de otro dueño al entrar; NULL se adopta; mismo usuario conserva; logout borra todo lo Managed de la instalación) | 8 tests nuevos + suite 345 OK + E2E operador pendiente | decisión usuario 2026-10-26 en chat |
| T-086 | hecha | 10 | [Managed-users-v1] Sesión de usuario persistente DPAPI + routing de arranque + estados gris/rojo de tarjeta (sustituye spec §5 memoria-solo, decisión usuario 2026-10-26) | build + selfcheck verdes (`startup-*`/`muser-*`/`card-*`/`f6-f8` PASS, DLL OK) [D: sesión implementadora]; E2E operador pendiente [P] | spec §5 (sustituido); T-085 |

Regla: cada PR referencia su T-### y actualiza esta tabla + STATE.
