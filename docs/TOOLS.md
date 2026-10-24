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
| Auditoría GCP (Cloud Run, IAM, secretos, Scheduler, monitoreo, cuotas) | subagente `gcp-stream-metadata` (`.opencode/agents/`) | solo lectura por defecto; nunca afirmar deploy validado sin ejecutarlo; lo temporal no se vuelve regla |
| Lint JS/TS auxiliar | `eslint` MCP | irrelevante para C++/Qt del plugin |

Manejo de errores: ante fallo de herramienta, registrar comando + salida en el mensaje/PR, probar una alternativa documentada, y si bloquea crear entrada en BACKLOG (bloqueada) + FINDINGS.

## Agentes locales y Skill compartida

Descubrimiento: `opencode debug agents` desde la raíz. Definiciones en
`.opencode/agents/` (`harness-steward`, `obs-plugin`,
`backend-integrations`, `gcp-stream-metadata` de solo lectura);
Skill en `.agents/skills/project-harness-governance/`
(descubierta por la herramienta `skill` en sesión 2026-10-24 y cargada
con éxito por `harness-steward` en prueba de subagente de solo lectura;
carga de `vitest` no autorizada rechazada con `permission.rejected`;
`git-workflow` autorizada cargó con éxito).
Estado real 2026-10-24: MCP conectados `chrome-devtools`,
`codebase-memory` (14 tools confirmadas por `tools/list` contra el
binario local), `context7` (2), `eslint` (1), `playwright` (25);
`github` requiere autenticación (no disponible).
Permisos endurecidos por agente con sintaxis v2 vigente (`permission:`,
forma actual según la documentación oficial; el `tools:` booleano está
heredado en desuso y no se usa; sin sección global `permission` en la
configuración efectiva; expansión comprobada en
`opencode debug agents`). Matiz del motor instalado: la clave `task`
documentada se ignora en silencio en el frontmatter; la denegación de
invocación de subagentes se expresa con `subagent: deny` (comprobado
efectivo en los 4 agentes, igual que en los built-ins `explore` y
`general`). `edit` negado salvo alcance
(`harness-steward` = 11 documentos/rutas listados;
`obs-plugin` = `src/**`; `backend-integrations` = `backend/**`;
GCP = denegado) — casos comprobados en reglas efectivas:
`docs/STATE.md`→allow y `src/metadata.cpp`→deny en steward;
`src/metadata.cpp`→allow y `backend/app.py`→deny en obs-plugin;
`backend/app.py`→allow y `src/metadata.cpp`→deny en backend;
cualquier `edit`→deny en GCP.
Lectura denegada (patrones enumerados por extensión; el motor solo
admite `*`/`?`, sin llaves): `dev.env`, `*.env`, `*.env.*`,
`*.envrc`, `*.key/.pem/.p12`, `*token*/*secret*` en
`.json/.txt/.yaml/.yml/.key`, `credentials.json/.yaml/.yml`,
`*credential*`, `client_secret*`; `*.env.example` permitido —
comprobados `dev.env`, `config.env`, `secrets.yaml`,
`api_tokens.txt`, `credentials.yml`, `client_secret_abc.json`→deny y
`backend/token_crypto.py`, `ADR-008-byod-secrets-dpapi.md`→allow
(verificado por glob que ningún archivo legítimo del repo colisiona).
Shell en `ask` salvo Git de solo lectura (`status/diff/log/rev-parse/
branch`→allow; `commit/push/merge/rebase/reset/clean/checkout`,
`gh`, `gcloud` salvo GCP, `curl`, `npm/npx`→deny comprobados;
`git push` en GCP queda en `ask` heredado); allowlist exacta de shell
de GCP intacta. Skills restringidas por ID (denegación general +
nombres exactos comprobados en disco). MCP por nombre real
`<servidor>_<tool>` (inventario confirmado por `tools/list` y el
historial local): solo lecturas necesarias por rol; denegados
`index_repository/delete_project/ingest_traces/manage_adr`,
`github_*`, `playwright_*`, `eslint_*`, `chrome-devtools_*`
(`search_graph`→allow y `delete_project/ingest_traces`→deny
comprobados; `github_list_issues`, `playwright_browser_navigate`,
`eslint_lint-files`→deny comprobados). `github` sigue sin
autenticación: no disponible. `question`/`browser`/`task` denegados;
`webfetch` permitido, `websearch` permitido en steward y `ask` en
dominio. GCP además: sin skills y sin MCP (solo shell + webfetch),
coherente con su rol auditor.
Limitaciones del motor: sin expansión por llaves (enumerar por
extensión); rutas evaluadas como relativas a la raíz según los
ejemplos oficiales (sin prueba de edición real en esta sesión);
`ask` en subagentes equivale a bloqueo.

`release-tooling` [C] (`opencode debug agents`, 2026-10-24): subagente
de herramientas de publicación; modifica `tools/deploy.ps1`, no lo
ejecuta. `edit`: `*`→deny + `tools/deploy.ps1`→allow exacto (sin
`tools/**`); `read`: `*`→allow + secretos idénticos a especialistas
(`dev.env`, `*.env`, `*.env.*`, `*.envrc`, `*.key/.pem/.p12`,
`*token*/*secret*` en `.json/.txt/.yaml/.yml/.key`,
`credentials.json/.yaml/.yml`, `*credential*`, `client_secret*`→deny;
`*.env.example`→allow); `bash`: `*`→ask + `git status/diff/log/
rev-parse/branch`→allow y `commit/push/merge/rebase/reset/clean/
checkout`, `gh`, `gcloud`, `curl`, `Invoke-WebRequest`, `npm/npx`→deny;
`glob/grep/list`→allow; `webfetch`→allow; `websearch`→deny (docs vía
`context7`); `question/browser`→deny; `subagent`: `*`→deny (sin
delegación anidada). `skill`: `*`→deny +
`project-harness-governance`→allow (única verificada en
`.agents/skills/`, 2026-10-24). MCP lectura: `context7_resolve-
library-id/query-docs` + `codebase-memory_search_graph/trace_path/
query_graph/get_graph_schema/search_code/get_code_snippet`→allow;
resto `codebase-memory_*`, `github_*`, `playwright_*`, `eslint_*`,
`chrome-devtools_*`→deny. Orquestador lo invoca vía
`subagent: release-tooling: allow` (sin regla `task`: el runtime la
ignora). Límites: sin ejecución, despliegues, red/producción ni Git
mutante.

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
- Rutas excluidas (`staging/`, `dev.env`): no inspeccionar contenido;
  evidencia solo según Skill (`project-harness-governance` §3) y
  `AGENTS.md` §55.10.
- Estado operativo (Scheduler): `/health`, `/ready`, `/version` no
  sustituyen la evidencia del propio job (`AGENTS.md` §55.11).
