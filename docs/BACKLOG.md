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
| T-032 | en-progreso | 5 | Hardening: storage seguro OS, revoke en Disconnect, backoff 429/5xx, fin de env-vars, distribución secrets F-017/F-019 | sobrevive reinicio sin plaintext; revocación verificada por proveedor; 429 sin reintento agresivo; cero secretos en repo/logs; matriz T1–T20 sin regresión | AGENTS §16-18, §26-29, ADR-008, F-029–F-032 |
| T-033 | futuro | 6 | Testing integración + negativos AGENTS §39-40 | gate §50 en verde | AGENTS §39-40, §50 |
| T-034 | futuro | 7 | Packaging/release instalador plugin | flujo §36 funciona | template wiki Distribute |
| T-040 | hecha | 0 | Normalizar `core.autocrlf` y convención `master` vs `main` | ADR + config | — |

Regla: cada PR referencia su T-### y actualiza esta tabla + STATE.
