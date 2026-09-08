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
| T-020 | futuro | 2 | PoC dock Qt6 mínimo (botón, sin OAuth) | plugin carga en OBS 32.2.2 | AGENTS §5-7 |
| T-030 | futuro | 3 | PoCs OAuth + título por proveedor (Twitch/YouTube/Kick) + desc YouTube | cambios verificados en plataformas | AGENTS §10-14 |
| T-031 | futuro | 4 | Dock unificado: título/desc + apply por plataforma + resultado independiente | matriz AGENTS §19 en verde | F-001, F-004 |
| T-032 | futuro | 5 | Hardening: storage seguro, refresh, threading, errores §28, logs sin secretos | reinicio/revocación/429 ok | AGENTS §16-18, §26-28 |
| T-033 | futuro | 6 | Testing integración + negativos AGENTS §39-40 | gate §50 en verde | AGENTS §39-40, §50 |
| T-034 | futuro | 7 | Packaging/release instalador plugin | flujo §36 funciona | template wiki Distribute |
| T-040 | hecha | 0 | Normalizar `core.autocrlf` y convención `master` vs `main` | ADR + config | — |

Regla: cada PR referencia su T-### y actualiza esta tabla + STATE.
