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
`backend-integrations`, `release-tooling`, `orquestador` (`mode:
primary`, sin edición), `gcp-stream-metadata` de solo lectura);
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
(`harness-steward` = 13 rutas listadas (11 documentales + `docs/BACKLOG.md`
+ `.gitignore`, reforma 2026-10-25; excepción puntual 2026-10-26 [D],
autorización del usuario a la guía: + `docs/MEDIAMTX-RELAY-FLOW.md` → allow,
`harness-steward` dueño del doc guía MediaMTX; precedente Fase 1:
`docs/BACKLOG.md` y `.gitignore` + specs Managed 2026-10-26 [D]: + `docs/superpowers/specs/*.md` → allow, transcribe diseño "Cuentas de usuario Managed" aprobado en chat; solo specs autorizados por tarea);
`obs-plugin` = `src/**`; `backend-integrations` = `backend/**` + `lab/**`
(dueño ejecutor lab, reforma 2026-10-25);
`release-tooling` = `tools/**` (ampliado desde `tools/deploy.ps1` exacto
para cubrir `tools/package.ps1`, T-067, reforma 2026-10-25);
GCP = denegado; orquestador = denegado total) — casos comprobados en reglas efectivas:
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
Excepción G3 2026-10-26 [D] (autorización del usuario, "Quitar todo"):
`.env`/`dev.env` legibles en los 7 roles; solo nombres de variable +
presencia/ausencia, jamás valores; resto de denies intacto:

- `harness-steward`: `.env` legible, secretos no.
- `obs-plugin`: `.env` legible, secretos no.
- `backend-integrations`: `.env` legible, secretos no.
- `gcp-stream-metadata`: `.env` legible, secretos no.
- `release-tooling`: `.env` legible, secretos no.
- `orquestador`: `.env` legible, secretos no.
- `git-integration`: `.env` legible, secretos no (sigue prohibido
  `git add`/commit de `.env` y secretos).
Shell en `ask` salvo Git de solo lectura (`status/diff/log/rev-parse/
branch`→allow; `commit/push/merge/rebase/reset/clean/checkout`,
`gh`, `gcloud` salvo GCP, `curl`→deny comprobados;
validación por stack, reforma 2026-10-25: `backend-integrations` =
`python*`/`pytest*`/`node*`/`npm*`/`npx*`→allow (lab Node + backend
Python; `lab/` aún no existe en el checkout [C, 2026-10-25], permiso
anticipado para la Fase 2); `obs-plugin` = `cmake*`/`ctest*`/
`msbuild*`→allow (C++/Qt de `src/`); `release-tooling` = sin ejecución
(`Invoke-WebRequest`→deny intacto);
`git push` en GCP queda en `ask` heredado); allowlist exacta de shell
de GCP intacta. Skills restringidas por ID (denegación general +
nombres exactos comprobados en disco): añadidos reforma 2026-10-25
(`executing-plans` + `requesting-code-review` en `backend-integrations`;
`requesting-code-review` en `obs-plugin`; resto intacto). MCP por nombre real
`<servidor>_<tool>` (inventario confirmado por `tools/list` y el
historial local): solo lecturas necesarias por rol; denegados
`index_repository/delete_project/ingest_traces/manage_adr`,
`github_*`, `playwright_*`, `eslint_*`, `chrome-devtools_*`
(excepción: `backend-integrations` `playwright_*`/`chrome-devtools_*`→`allow`
por orden explícita del dueño 2026-10-26, E2E lab T-077, ver F-091)
(`search_graph`→allow y `delete_project/ingest_traces`→deny
comprobados; `github_list_issues`, `playwright_browser_navigate`,
`eslint_lint-files`→deny comprobados). `github` sigue sin
autenticación: no disponible. `question`/`task` denegados;
`browser`: `deny` en steward/orquestador/dominio salvo
`backend-integrations` = `ask` solo-localhost (reforma 2026-10-25;
efectividad [P] pendiente de comprobar con `opencode debug agents` en
la Fase 2; si el motor no lo admite rige `deny` y el lab se valida sin
navegador);
`webfetch` permitido, `websearch` permitido en steward y `ask` en
dominio. GCP además: sin skills y sin MCP (solo shell + webfetch),
coherente con su rol auditor.
Apertura solo-lab T-077 (2026-10-26, autorización del usuario) [D]:
`backend-integrations.browser`: `ask`→`allow` (solo E2E lab T-077
`localhost:3002` + observación por el operador); orden explícita del
dueño del repositorio 2026-10-26 [D]: se elimina la regla que impedía
usar `chrome-devtools` MCP para el E2E del lab → `playwright_*` y
`chrome-devtools_*` en `backend-integrations` pasan a `allow`
(autorización standing lab T-077 ya registrada; riesgo aceptado por el
dueño, ver F-091); standing T-077 = batería lab completa sin OK por paso; GoLive/LIVE_NOW y todo lo productivo siguen
con gates y OK explícito separado.
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
mutante. Reforma 2026-10-25 [C frontmatters de esta sesión]: `edit`
pasa a `tools/**` (cubre `tools/package.ps1`, T-067); el resto del
bloque queda intacto.

`git-integration` [C] (definición `.opencode/agents/git-integration.md`,
fase G1 2026-10-25): operador Git atómico; integra trabajo ya revisado
y no edita ningún archivo (`edit`: `*`→deny total). `read`: `*`→allow
+ secretos idénticos a los demás agentes (`dev.env`, `*.env`,
`*.env.*`, `*.envrc`, `*.key/.pem/.p12`, `*token*/*secret*` en
`.json/.txt/.yaml/.yml/.key`, `credentials.json/.yaml/.yml`,
`*credential*`, `client_secret*`→deny; `*.env.example`→allow).
`bash`: `*`→ask + allows exactos de solo lectura (`git status/diff/
log/rev-parse/branch/show`) y de integración (`git add backend/**`,
`git add src/**`, `git add docs/**`, `git add AGENTS.md`,
`git add .opencode/agents/*`, `git add .gitignore`, `git add tools/**`,
`git commit*`, `git push*`, `gh pr create/view/status*`,
`gh run list/view*`); denies explícitos (`git reset/clean/rebase/
checkout*`, `gcloud *`, `curl*`, `npm*`, `npx*`). Prohibido en cuerpo:
`git add -A` / `git add .`, `staging/`, `dev.env`/secretos,
force-push / `--hard` sin autorización separada escrita; commits
atómicos por grupo (Conventional + T-### + evidencia); gate
`verification-before-completion` antes de cada push/PR/merge;
secreto-scan antes de cada commit. `subagent`/`browser`/`question`→deny.
Skills (`*`→deny + tres verificadas en disco en esta sesión [C]:
`project-harness-governance` local, `verification-before-completion`
y `git-workflow` globales). MCP: solo lectura denegada en bloque
(`codebase-memory_*`, `context7_*`, `github_*`, `playwright_*`,
`eslint_*`, `chrome-devtools_*`→deny); `github` MCP sin autenticación:
no disponible. El orquestador lo invoca vía
`subagent: git-integration: allow` y sigue sin ejecutar Git
directamente (sus denies de `git commit/push/merge` y `gh *` intactos).
Autorización vigente [D] (fase G1, 2026-10-25, orden del usuario):
commit/push/PR/merge de ESTE run quedan autorizados hasta el merge a
`master`, con parada solo si el usuario lo pide; futuros runs requieren
autorización separada. Parche G1.5 [D] (2026-10-25): merge incluido en este run (`gh pr merge*`, `git merge --no-ff*` en allowlist exacta). Gate G2 [P]: lanzar `git-integration` para
integrar el Todo-junto en commits atómicos ordenados.

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
  `AGENTS.md` §55.10 (excepción G3 2026-10-26: `.env`/`dev.env`
  legibles por nombres de variable, jamás valores; `staging/` sigue
  excluida salvo `status`).
- Estado operativo (Scheduler): `/health`, `/ready`, `/version` no
  sustituyen la evidencia del propio job (`AGENTS.md` §55.11).
