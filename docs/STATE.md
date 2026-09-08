# STATE — estado actual del proyecto

> Único archivo que responde: dónde estamos, qué hacemos, qué falta.
> Actualizar en cada tarea terminada.

- **Fase actual:** Phase 2 (P2) — T-020 terminada en rama `feat/t-020-dock-poc` (pendiente PR). P1 mergeada (PR #6, `8205089`). T-012 mergeada (PR #4). T-010/T-011/T-040 mergeadas (PR #3).
- **Rama base:** `master` (confirmado por ADR-004; `default_branch: master`, sin branch protection, CI anclado a `master`).
- **Repo remoto:** `horangelmillan/obs-stream-metadata` (público, GitHub-flow con PRs).
- **Qué estamos haciendo:** nada — esperar merge de la PR T-020. Siguiente tras merge: P3 PoCs OAuth + título por proveedor (T-030).
- **Qué falta para cerrar Phase 0:** solo entregar informe 13 puntos (T-002, sesión anterior).
- **P2 validada (T-020, 2026-09-08):** dock nativo vía `obs_frontend_add_dock_by_id` (id `obs-stream-metadata-dock`, título `Stream Metadata`, 3 labels); registrado + visible (HWND `OBSDock`) + geometría restaurada por `DockState`; 5 ciclos abrir/cerrar limpios sin crash (F-013, F-014). Límite harness: píxeles Qt y toggle de menú no automatizables sin foreground → verificación manual de 30 s pendiente por el usuario.
- **Restricción activa:** PoC de dock en `src/` (sin OAuth/APIs/red/cuentas). Código de proveedores, a partir de P3 (T-030).
- **Contexto técnico:** `AGENTS.md` (válido) + ADR-005 (toolchain: OBS 32.2.2 target / ≥30.0 mín, VS17 2022, SDK 10.0.22621, CMake ≥3.28, Qt6 vía obs-deps del template) + ADR-006 / `PHASES.md` (P0-P7 risk-first).
- **Deuda conocida:** R1/R2 cerrados en T-013 (buildspec 32.2.2 + Qt 6.11.1 exacto, ADR-007); cuota/límites y broadcast YouTube pendientes de verificación experimental en P3.
- **Última actualización:** 2026-09-08 — T-020 completada (P2 validada: dock visible + persistencia + 5 ciclos limpios); PR pendiente.
