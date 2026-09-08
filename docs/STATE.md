# STATE — estado actual del proyecto

> Único archivo que responde: dónde estamos, qué hacemos, qué falta.
> Actualizar en cada tarea terminada.

- **Fase actual:** Phase 1 (P1) — T-013 terminada en rama `feat/p1-bootstrap-obs-32` (pendiente PR). T-012 mergeada (PR #4, `e54248b`); avance README mergeado (PR #5, `f303874`). T-010/T-011/T-040 mergeadas (`ecf6234`, PR #3).
- **Rama base:** `master` (confirmado por ADR-004; `default_branch: master`, sin branch protection, CI anclado a `master`).
- **Repo remoto:** `horangelmillan/obs-stream-metadata` (público, GitHub-flow con PRs).
- **Qué estamos haciendo:** nada — esperar merge de la PR T-013. Siguiente tras merge: P2 dock Qt6 mínimo (T-020) sobre la base P1.
- **Qué falta para cerrar Phase 0:** solo entregar informe 13 puntos (T-002, sesión anterior).
- **P1 validada (T-013, 2026-09-08):** template vendorizado con `buildspec.json` propio (OBS 32.2.2 + obs-deps/Qt6 2026-07-15, ADR-007); configure ok (VS2022, SDK 22621); `obs-stream-metadata.dll` x64 compilado; carga + unload verificados en OBS 32.2.2 real ×2 arranques (log `[obs-stream-metadata] plugin loaded/unloaded`); Qt deps 6.11.1 == Qt OBS 6.11.1 (R2 cerrado); validación sin admin vía `OBS_PLUGINS_PATH/DATA_PATH` (F-010); CWD y sentinels documentados (F-009, F-011).
- **Restricción activa:** solo esqueleto del template en `src/` (sin dock/OAuth/APIs/UI/storage). Código funcional, a partir de P2 (T-020).
- **Contexto técnico:** `AGENTS.md` (válido) + ADR-005 (toolchain: OBS 32.2.2 target / ≥30.0 mín, VS17 2022, SDK 10.0.22621, CMake ≥3.28, Qt6 vía obs-deps del template) + ADR-006 / `PHASES.md` (P0-P7 risk-first).
- **Deuda conocida:** R1/R2 cerrados en T-013 (buildspec 32.2.2 + Qt 6.11.1 exacto, ADR-007); cuota/límites y broadcast YouTube pendientes de verificación experimental en P3.
- **Última actualización:** 2026-09-08 — T-013 completada (P1 validada: build + carga + unload en OBS 32.2.2); PR pendiente.
