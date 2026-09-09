# STATE — estado actual del proyecto

> Único archivo que responde: dónde estamos, qué hacemos, qué falta.
> Actualizar en cada tarea terminada.

- **Fase actual:** Phase 5 (P5) — **T-032 en-progreso 2026-09-09** (rama `feat/t-032-hardening`, desde `master` tras merge de PR #9). Implementación completa + validación offline verde (build 0 errores, selfchecks 35/35 + 28/28, scans limpios); pendiente live del operador (reinicio con cuentas, revoke en web, matriz T1–T20 sin regresión). T-031 PASS mergeada a `master` (`898b4b6`).
- **Rama base:** `master` (confirmado por ADR-004; `default_branch: master`, sin branch protection, CI anclado a `master`).
- **Repo remoto:** `horangelmillan/obs-stream-metadata` (público, GitHub-flow con PRs).
- **Qué hicimos:** T-030 (P3 PASS): PoCs + triple LIVE PASS (runners `tools/t030_live_*.py`, F-015–F-024, `docs/T030-POC.md`). T-031 (P4 PASS, mergeada): dock unificado `src/metadata.*` + `src/metadata_dock.*`, matriz viva T1–T20 en verde, fixes F-025–F-028. T-032 (P5, en-progreso): storage DPAPI `src/secure_store.*` + revoke real + backoff acotado + fin de env vars + ADR-008 (F-029–F-032); validación offline verde, live pendiente del operador.
- **Qué falta para cerrar Phase 0:** solo entregar informe 13 puntos (T-002, preexistente, no bloquea fases).
- **P1 validada (T-013, 2026-09-08):** template vendorizado con `buildspec.json` propio (OBS 32.2.2 + obs-deps/Qt6 2026-07-15, ADR-007); configure ok (VS2022, SDK 22621); `obs-stream-metadata.dll` x64 compilado; carga + unload verificados en OBS 32.2.2 real ×2 arranques (log `[obs-stream-metadata] plugin loaded/unloaded`); Qt deps 6.11.1 == Qt OBS 6.11.1 (R2 cerrado); validación sin admin vía `OBS_PLUGINS_PATH/DATA_PATH` (F-010); CWD y sentinels documentados (F-009, F-011).
- **P2 validada y mergeada (T-020, 2026-09-08, PR #7 `c74eff3`):** dock nativo vía `obs_frontend_add_dock_by_id` (id `obs-stream-metadata-dock`, título `Stream Metadata`, 3 labels); registrado + visible (HWND `OBSDock`) + geometría restaurada por `DockState`; 5 ciclos abrir/cerrar limpios sin crash (F-013, F-014). Límite harness: píxeles Qt y toggle de menú no automatizables sin foreground → verificación manual de 30 s pendiente por el usuario.
- **Restricción vigente:** `poc/t030/` es evidencia histórica + regresión (no tocar salvo corrección justificada); el producto vive en `src/`.
- **Contexto técnico:** `AGENTS.md` (válido) + ADR-005 (toolchain: OBS 32.2.2 target / ≥30.0 mín, VS17 2022, SDK 10.0.22621, CMake ≥3.28, Qt6 vía obs-deps del template) + ADR-006 / `PHASES.md` (P0-P7 risk-first).
- **Deuda conocida:** R1/R2 cerrados (T-013, ADR-007); distribución de secrets Desktop (F-017/F-019) y storage OS, resueltos en T-032 (ADR-008, F-029–F-032) salvo validación viva del operador.
- **Última actualización:** 2026-09-09 — T-031 PASS (P4 cerrada): matriz viva completa con read-back por plataforma, fixes F-026/F-027/F-028 mergeados en la rama, CI verde. PR #9 lista para revisión/merge (fuera de esta sesión).
