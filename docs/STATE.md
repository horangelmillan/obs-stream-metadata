# STATE — estado actual del proyecto

> Único archivo que responde: dónde estamos, qué hacemos, qué falta.
> Actualizar en cada tarea terminada.

- **Fase actual:** Phase 3 (P3) — T-030 en-progreso en rama `feat/t-030-poc-providers` (actualizada con `master`: P2 mergeada vía PR #7, `c74eff3`). T-030 no depende del dock (PoC en `poc/t030/`, fuera de `src/`).
- **Rama base:** `master` (confirmado por ADR-004; `default_branch: master`, sin branch protection, CI anclado a `master`).
- **Repo remoto:** `horangelmillan/obs-stream-metadata` (público, GitHub-flow con PRs).
- **Qué estamos haciendo:** T-030 (P3 PASS, 2026-09-08): investigación oficial cerrada (F-015/016/017 + `docs/T030-POC.md`); lógica offline en `poc/t030/` con selfcheck 28/28 PASS; **Twitch + YouTube + Kick LIVE PASS** (runners redactados `tools/t030_live_*.py`; YouTube: OAuth Desktop+PKCE, broadcast `Y9yFOeQw83s`, PUT id+snippet, read-back; Kick: OAuth 2.1+PKCE localhost, PATCH 204, read-back en directo por doble vía). Hallazgos nuevos F-019–F-024. **P4 (T-031) desbloqueada.** Pendiente menor del operador: restaurar título YouTube original + revocar grants Kick de las sesiones con FAIL.
- **Qué falta para cerrar Phase 0:** solo entregar informe 13 puntos (T-002, sesión anterior).
- **P1 validada (T-013, 2026-09-08):** template vendorizado con `buildspec.json` propio (OBS 32.2.2 + obs-deps/Qt6 2026-07-15, ADR-007); configure ok (VS2022, SDK 22621); `obs-stream-metadata.dll` x64 compilado; carga + unload verificados en OBS 32.2.2 real ×2 arranques (log `[obs-stream-metadata] plugin loaded/unloaded`); Qt deps 6.11.1 == Qt OBS 6.11.1 (R2 cerrado); validación sin admin vía `OBS_PLUGINS_PATH/DATA_PATH` (F-010); CWD y sentinels documentados (F-009, F-011).
- **P2 validada y mergeada (T-020, 2026-09-08, PR #7 `c74eff3`):** dock nativo vía `obs_frontend_add_dock_by_id` (id `obs-stream-metadata-dock`, título `Stream Metadata`, 3 labels); registrado + visible (HWND `OBSDock`) + geometría restaurada por `DockState`; 5 ciclos abrir/cerrar limpios sin crash (F-013, F-014). Límite harness: píxeles Qt y toggle de menú no automatizables sin foreground → verificación manual de 30 s pendiente por el usuario.
- **Restricción activa:** dock P2 en `src/` (sin OAuth/APIs/red/cuentas) + PoC de proveedores en `poc/t030/` (fuera de `src/` para no tripear el gate P1 de CI). Integración dock+proveedores, a partir de P4 (T-031).
- **Contexto técnico:** `AGENTS.md` (válido) + ADR-005 (toolchain: OBS 32.2.2 target / ≥30.0 mín, VS17 2022, SDK 10.0.22621, CMake ≥3.28, Qt6 vía obs-deps del template) + ADR-006 / `PHASES.md` (P0-P7 risk-first).
- **Deuda conocida:** R1/R2 cerrados en T-013 (buildspec 32.2.2 + Qt 6.11.1 exacto, ADR-007); cuota/límites y broadcast YouTube pendientes de verificación experimental en P3.
- **Última actualización:** 2026-09-08 — T-030 cerrada con triple LIVE PASS (P3 PASS); rama `feat/t-030-poc-providers` con runners + fixes + docs (PR #8 OPEN, sin merge); siguiente: P4/T-031.
