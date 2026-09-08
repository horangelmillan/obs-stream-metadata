# STATE — estado actual del proyecto

> Único archivo que responde: dónde estamos, qué hacemos, qué falta.
> Actualizar en cada tarea terminada.

- **Fase actual:** pre-Phase 1 — T-010/T-011/T-040 terminadas en rama `docs/t010-t011-t040-fases-toolchain-master` (pendiente PR).
- **Rama base:** `master` (confirmado por ADR-004; `default_branch: master`, sin branch protection, CI anclado a `master`).
- **Repo remoto:** `horangelmillan/obs-stream-metadata` (público, GitHub-flow con PRs).
- **Qué estamos haciendo:** nada — esperar merge de la PR T-010/T-011/T-040. Siguiente tras merge: T-012 instalar toolchain (P1).
- **Qué falta para cerrar Phase 0:** solo entregar informe 13 puntos (T-002, sesión anterior).
- **Restricción activa:** PROHIBIDO código funcional (`src/`, OAuth, APIs, UI, storage, títulos) hasta Phase 1.
- **Contexto técnico:** `AGENTS.md` (válido) + ADR-005 (toolchain: OBS 32.2.2 target / ≥30.0 mín, VS17 2022, SDK 10.0.22621, CMake ≥3.28, Qt6 vía obs-deps del template) + ADR-006 / `PHASES.md` (P0-P7 risk-first).
- **Deuda conocida:** sin toolchain C++ en PATH (cmake/VS/Qt SDK sin instalar — instalar en T-012, P1); template fija obs-studio 31.1.1, hay que subir a 32.2.2 en P1 (R1); Qt local 6.11.1 vs Qt del deps por verificar en carga (R2); cuota/límites y broadcast YouTube pendientes de verificación experimental en P3.
- **Última actualización:** 2026-09-08 — T-010/T-011/T-040 completadas (fases P0-P7, toolchain, master); PR pendiente.
