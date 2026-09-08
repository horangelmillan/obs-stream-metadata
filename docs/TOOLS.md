# TOOLS — política de herramientas y MCP

Principio: la herramienta correcta para cada tarea; verificar antes de afirmar; no instalar sin ADR.

## MCP

| Tarea | Herramienta | Cuándo NO |
|---|---|---|
| Definiciones, callers, impacto (cuando haya código) | `codebase-memory` search_graph / trace_path | no usar grep ciego primero |
| Docs de librerías/SDK/API/CLI (OBS, Qt, CMake, OAuth) | `context7` resolve-library-id → query-docs | no usar websearch como autoridad |
| Actualidad / cambios de API | `websearch` + contraste oficial | no usar snippets viejos como autoridad |
| Repos, PRs, issues, código remoto | `github` MCP / `gh` CLI | no asumir sin leer template del repo |
| Validación UI futura en navegador | `playwright` (requiere `localhost:9222`) | no declarar done solo porque compila |
| Lint JS/TS auxiliar | `eslint` MCP | irrelevante para C++/Qt del plugin |

Manejo de errores: ante fallo de herramienta, registrar comando + salida en el mensaje/PR, probar una alternativa documentada, y si bloquea crear entrada en BACKLOG (bloqueada) + FINDINGS.

## Skills (solo si aportan; compatibles con el proyecto)

- Siempre: `ponytail` (full) + `clean-code` — YAGNI, stdlib antes que dependencia, diff mínimo.
- Git/CI: `git-workflow`, `github-actions` al tocar ramas, commits, PRs, workflows.
- Planificación: `writing-plans` (multi-paso), `executing-plans` (ejecutar plan), `brainstorming` (diseño).
- Calidad: `verification-before-completion` (gate antes de done), `systematic-debugging` (fallos), `test-driven-development` (features/fixes), `requesting-code-review` (cambios grandes).
- Paralelismo: `dispatching-parallel-agents`, `subagent-driven-development` (subtareas independientes).
- Cierre: `finishing-a-development-branch`, `using-git-worktrees` (aislamiento).
- Excluidas aquí: `antfu`, `drizzle`, `vitest`, `sapui5`, `vercel-*`, `deploy-to-vercel` (stack JS/web no aplica al plugin C++/Qt).

## Comandos locales

- Lectura: `read`/`glob`/`grep`; `bash` solo para comandos de sistema (git, gh) o cómputo puntual.
- Edición: `read` antes de `edit`; `oldString` mínimo; verificar región editada.
- Destructivos: requieren autorización explícita del usuario.
