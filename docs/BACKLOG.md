# BACKLOG — tareas (mantenido por agentes)

Formato: `T-### | estado | fase | descripción | aceptación | links`.
Estados: pendiente / en-progreso / bloqueada / hecha / deuda / investigación / futuro.

| ID | Estado | Fase | Tarea | Aceptación | Links |
|---|---|---|---|---|---|
| T-001 | hecha | 0 | Harness Phase 0: docs + CI + git | PR mergeado, CI verde | ADR-001/002/003 |
| T-002 | en-progreso | 0 | Informe final 13 puntos Phase 0 | informe entregado al usuario | — |
| T-010 | pendiente | 1 | Definir fases 1-8 reales (alcance OBS/Qt/OAuth) | `PHASES.md` actualizado + ADR | F-001 |
| T-011 | investigación | 1 | Confirmar toolchain Windows (VS2022/CMake3.28+/Qt6/OBS SDK) | evidencia versiones | F-002 |
| T-020 | futuro | 2 | PoC dock Qt6 mínimo (botón, sin OAuth) | plugin carga en OBS 32.2.2 | AGENTS §5-7 |
| T-030 | futuro | 3+ | OAuth Twitch / YouTube / Kick + PoC título | cambios verificados en plataformas | AGENTS §10-14 |
| T-040 | deuda | 0 | Normalizar `core.autocrlf` y convención `master` vs `main` | ADR + config | — |

Regla: cada PR referencia su T-### y actualiza esta tabla + STATE.
