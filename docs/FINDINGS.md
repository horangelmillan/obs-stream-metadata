# FINDINGS — hallazgos

Formato: `F-### | fecha | contexto | descubrimiento | evidencia/fuente | impacto | decisión | tareas`.

| ID | Fecha | Contexto | Descubrimiento | Evidencia / Fuente | Impacto | Decisión | Tareas |
|---|---|---|---|---|---|---|---|
| F-001 | 2026-09-07 | Capacidades MVP | Título ≠ descripción: Twitch/Kick sin descripción de stream equivalente; solo YouTube la soporta | `AGENTS.md` §2-3, §47 | UI no debe prometer "aplicar descripción a todo" | Modelo por plataforma (matriz §3) | T-010, T-030 |
| F-002 | 2026-09-08 | Entorno Windows | Sin cmake/VS/Qt SDK en PATH; OBS 32.2.2 con Qt6 DLLs instalado | `obs64.exe 32.2.2`, `bin\64bit\Qt6*.dll` | No compilar en Phase 0; toolchain pendiente | Registrar deuda T-011, no instalar sin ADR | T-011 |
| F-003 | 2026-09-07 | Kick API | `PATCH /public/v1/channels` con `stream_title` → `204`; `channel_description` ≠ descripción de stream | `AGENTS.md` §14.5-14.6 | No escribir `channel_description` como descripción | Título Kick sí; descripción no soportada | T-030 |
| F-004 | 2026-09-07 | YouTube | Hay que seleccionar `liveBroadcast` antes de actualizar; no crear broadcasts en MVP | `AGENTS.md` §21, §41, §48 | Flujo account→broadcast→snippet | Selector de broadcast en UI futura | T-030 |
| F-005 | 2026-09-08 | CI secret-scan | Patrón ingenuo (palabras sueltas) marcó docs legítimos; apóstrofe en patrón rompió bash (exit 2) | runs PR #1 `34187529299`, `34187597990` | CI rojo en 2 intentos | Escanear valores (asignaciones/prefijos/PEM), sin apóstrofes en `run:` | T-001 |
| F-006 | 2026-09-08 | Toolchain Windows | OBS 32.2.2 local lleva Qt **6.11.1** (`Qt6Core.dll`); template fija obs-studio **31.1.1** en `buildspec.json` (hay que subir a 32.2.2 en P1); toolchain (VS/CMake/Qt SDK) ausente en PATH | `C:\Program Files\obs-studio\bin\64bit\Qt6Core.dll` 6.11.1.0; `obs-plugintemplate` `CMakeLists.txt` (`3.28...3.30`) + wiki Build System Requirements (VS17 2022, SDK 10.0.22621, CMake 3.30.5) | Qt no se instala manual (lo da obs-deps); R1-R4 en ADR-005 | ADR-005; instalar en T-012 | T-012, T-020 |
| F-007 | 2026-09-08 | master vs main | Ninguna razón técnica/CI/GitHub/tooling obliga a `main`: `default_branch: master`, sin branch protection, CI anclado a `master`, template oficial también usa `master` | `gh api repos/...` (`default_branch: master`, 404 en protection); `ci-phase0.yml` `branches: [master]`; árbol `i/lf w/lf` limpio | Se mantiene `master`; `.gitattributes` `* text=auto` independiza LF del `autocrlf` local | ADR-004 | — |

Los hallazgos importantes se consolidan en `AGENTS.md`/docs permanentes cuando corresponda.
