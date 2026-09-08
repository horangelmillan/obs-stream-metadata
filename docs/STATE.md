# STATE — estado actual del proyecto

> Único archivo que responde: dónde estamos, qué hacemos, qué falta.
> Actualizar en cada tarea terminada.

- **Fase actual:** Phase 3 (P3) — T-030 en-progreso en rama `feat/t-030-poc-providers` (nacida de `master`). P1 mergeada (`8205089`, PR #6). P2/T-020 validada técnicamente en su rama pero PR #7 sigue **OPEN sin merge** (verificado 2026-09-08); T-030 no depende de ella (PoC en `poc/t030/`, fuera de `src/`, sin dock).
- **Rama base:** `master` (confirmado por ADR-004; `default_branch: master`, sin branch protection, CI anclado a `master`).
- **Repo remoto:** `horangelmillan/obs-stream-metadata` (público, GitHub-flow con PRs).
- **Qué estamos haciendo:** T-030 (P3 parcial): investigación oficial cerrada (F-015/016/017 + `docs/T030-POC.md`); lógica offline en `poc/t030/` con selfcheck 28/28 PASS; **Twitch LIVE PASS** (device flow + PATCH 204 + read-back, 2026-09-08, tokens revocados; runner redactado `tools/t030_live_twitch.py`, F-018). Pendientes del operador YouTube y Kick. **No avanzar a P4.**
- **Qué falta para cerrar Phase 0:** solo entregar informe 13 puntos (T-002, sesión anterior).
- **P1 validada (T-013, 2026-09-08):** template vendorizado con `buildspec.json` propio (OBS 32.2.2 + obs-deps/Qt6 2026-07-15, ADR-007); configure ok (VS2022, SDK 22621); `obs-stream-metadata.dll` x64 compilado; carga + unload verificados en OBS 32.2.2 real ×2 arranques (log `[obs-stream-metadata] plugin loaded/unloaded`); Qt deps 6.11.1 == Qt OBS 6.11.1 (R2 cerrado); validación sin admin vía `OBS_PLUGINS_PATH/DATA_PATH` (F-010); CWD y sentinels documentados (F-009, F-011).
- **Restricción activa:** PoC de proveedores en `poc/t030/` (fuera de `src/` para no tripear el gate P1 de CI); `src/` sigue siendo solo esqueleto. Integración con el dock, a partir de P4 (T-031).
- **Contexto técnico:** `AGENTS.md` (válido) + ADR-005 (toolchain: OBS 32.2.2 target / ≥30.0 mín, VS17 2022, SDK 10.0.22621, CMake ≥3.28, Qt6 vía obs-deps del template) + ADR-006 / `PHASES.md` (P0-P7 risk-first).
- **Deuda conocida:** R1/R2 cerrados en T-013 (buildspec 32.2.2 + Qt 6.11.1 exacto, ADR-007); cuota/límites y broadcast YouTube pendientes de verificación experimental en P3.
- **Última actualización:** 2026-09-08 — T-030 parcial (lógica offline PASS 28/28, build ok, gates CI locales ok; verificación viva pendiente del operador); PR #7 (P2) verificada OPEN sin merge.
