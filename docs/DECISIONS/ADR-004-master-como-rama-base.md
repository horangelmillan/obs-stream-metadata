# ADR-004 — Mantener `master` como rama base (no migrar a `main`)

- **Fecha:** 2026-09-08 · **Tarea:** T-040 · **Estado:** decidido.
- **Problema:** decidir si la rama principal debe seguir siendo `master` o migrarse a `main`.
- **Investigación (evidencia, no intuición):**
  - `gh api repos/horangelmillan/obs-stream-metadata` → `default_branch: master`; sin branch protection (HTTP 404 en `/branches/master/protection`).
  - `git remote show` / clon: `origin/HEAD -> origin/master`; ramas existentes (`docs/t-001-*`, `docs/t-002-*`) nacen de `master`.
  - `.github/workflows/ci-phase0.yml` dispara solo en `branches: [master]` (push y pull_request).
  - `docs/GIT.md`, `docs/STATE.md`, ADR-002 referencian `master` como base.
  - Config local: `init.defaultbranch=master`; `git ls-files --eol` → todo el árbol `i/lf w/lf` (sin mezcla CRLF).
  - El template oficial `obsproject/obs-plugintemplate` también usa `master` como rama base y sus workflows aceptan `master` o `main` — no hay presión del ecosistema hacia `main`.
- **Opciones:** (a) mantener `master`; (b) migrar a `main` (renombrar rama, cambiar default en GitHub, reescribir workflows/docs, re-apuntar PRs/ramas).
- **Elección:** (a) mantener `master`.
- **Por qué:** no existe ninguna razón técnica, de CI, de GitHub ni de tooling que obligue a usar `main`. Migrar aportaría valor nulo y riesgo real (reescritura de historial/referencias, CI desalineado, ramas huérfanas).
- **Consecuencias:**
  - `master` sigue siendo base de GitHub-flow (ADR-002 sin cambios).
  - Normalización de finales de línea: se añade `.gitattributes` con `* text=auto` para que el repo controle LF sin depender del `core.autocrlf` de cada máquina (el sistema trae `true`, el usuario local tiene `false`; el árbol actual ya es LF limpio y debe seguir siéndolo).
  - Revisitar solo si una herramienta futura exige `main` (registrar entonces un FINDING y un ADR nuevo, no migrar a ciegas).
