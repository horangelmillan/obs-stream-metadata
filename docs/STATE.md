# STATE — estado actual del proyecto

> Único archivo que responde: dónde estamos, qué hacemos, qué falta.
> Actualizar en cada tarea terminada.

- **Fase actual:** Phase 1 (P1) — T-012 terminada en rama `feat/t-012-toolchain` (pendiente PR). T-010/T-011/T-040 mergeadas (`ecf6234`, PR #3).
- **Rama base:** `master` (confirmado por ADR-004; `default_branch: master`, sin branch protection, CI anclado a `master`).
- **Repo remoto:** `horangelmillan/obs-stream-metadata` (público, GitHub-flow con PRs).
- **Qué estamos haciendo:** nada — esperar merge de la PR T-012. Siguiente tras merge: P1 compilar el template oficial y cargarlo en OBS 32.2.2 (build + instalación + carga, sin código propio).
- **Qué falta para cerrar Phase 0:** solo entregar informe 13 puntos (T-002, sesión anterior).
- **Restricción activa:** PROHIBIDO código funcional (`src/`, OAuth, APIs, UI, storage, títulos) hasta Phase 1.
- **Contexto técnico:** `AGENTS.md` (válido) + ADR-005 (toolchain: OBS 32.2.2 target / ≥30.0 mín, VS17 2022, SDK 10.0.22621, CMake ≥3.28, Qt6 vía obs-deps del template) + ADR-006 / `PHASES.md` (P0-P7 risk-first).
- **Deuda conocida:** toolchain C++ INSTALADO y validado en T-012 (VS2022 17.14 + MSVC 19.44 + ATL + SDK 10.0.22621 + CMake 3.31.6 + Git 2.49.0; OBS 32.2.2 con Qt 6.11.1; sin Qt manual — Qt vía obs-deps en P1); template fija obs-studio 31.1.1, hay que subir a 32.2.2 en P1 (R1); Qt local 6.11.1 vs Qt del deps por verificar en carga (R2); wiki OBS-master (2026-06-20) pide VS2026/SDK26100/CMake4.2 para compilar OBS — NO aplica al template, pero P1 debe comprobar requisitos del tag 32.2.2 al subir `buildspec.json` (F-008); cuota/límites y broadcast YouTube pendientes de verificación experimental en P3.
- **Última actualización:** 2026-09-08 — T-012 completada (toolchain instalado + validado); PR pendiente.
