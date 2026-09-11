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

Regla: cada PR referencia su T-### y actualiza esta tabla + STATE.
