# WORKFLOW — flujo estándar de tareas

Todo trabajo futuro sigue este pipeline. Adaptar profundidad, no saltarse fases.

```text
TASK → CONTEXT → INVESTIGATION → PLAN → IMPLEMENTATION → VALIDATION → REVIEW → DOCUMENTATION → COMMIT/PR → NEXT
```

| Fase | Objetivo | Entradas | Permitido | Salida |
|---|---|---|---|---|
| TASK | definir done | BACKLOG T-### | aclarar con usuario | criterio aceptación |
| CONTEXT | reconstruir estado | STATE, BACKLOG, FINDINGS, `git status`, `git log -5` | solo lectura + `git status/diff/log` | lista archivos afectados |
| INVESTIGATION | hechos verificables | docs oficiales | context7, websearch, github MCP | fuentes con versión/fecha/URL |
| PLAN | cómo + riesgos + validación | contexto | `writing-plans` si >3 pasos | plan escrito o reducido |
| IMPLEMENTATION | cambio mínimo | plan | edit quirúrgico | diff pequeño |
| VALIDATION | probar antes de afirmar | `VALIDATION.md` | build/tests/CI del propio stack (ver abajo) | evidencia (comando + salida) |
| REVIEW | detectar regresión/fuera-alcance | diff | `requesting-code-review` si complejo | checklist ok |
| DOCUMENTATION | persistir conocimiento útil | hallazgo/ADR/backlog | editar solo docs afectados | STATE/BACKLOG/FINDINGS/ADR al día |
| COMMIT/PR | integrar controlado | gate escrito del usuario | `git-integration` ejecuta lo autorizado | commits atómicos + PR con evidencia por gate |
| NEXT | continuar sin perder contexto | STATE | actualizar STATE | siguiente T-### |

Reglas:

- **Jerarquía documental (AGENTS.md §55):** BACKLOG manda en estados; STATE resume; DEPLOYMENT distingue verificado/documentado/pendiente; VALIDATION no reescribe el pasado; PROJECT_CONTEXT enlaza, no duplica.
- **El agente ejecuta todo lo ejecutable (OBLIGATORIO, T-076):** si un comando puede correr en su propio entorno (build, tests, `package.ps1`, `gcloud`, `gh`, `curl`, deploy, verificación de instalador), LO EJECUTA ÉL y entrega evidencia + artefacto listo. Al usuario NUNCA se le pide abrir una terminal para algo que el agente ya puede hacer.
- Al usuario solo se le pide lo que exige su presencia física: contraseñas y clics de permiso en Google/Twitch/Kick/Meta (ningún agente puede consentir por él), cerrar OBS para instalar con admin, y verificación visual en OBS/YouTube Studio. Esos pasos se le dan masticados, uno por uno, sin terminal.
- Contexto que originó la regla (2026-10-24): se pidió al usuario compilar y empaquetar tres veces, incluyendo una por un error de compilación que el agente podía haber detectado compilando él primero. No se repite.
- Tarea pequeña (≤3 pasos, 1 archivo): plan reducido en el propio mensaje.
- Tarea compleja: plan explícito antes de código (skill `writing-plans`).
- **VALIDATION por stack (reforma 2026-10-25; resuelve la contradicción
  con el `bash` anterior, sin debilitar gates):** cada agente ejecuta
  solo lo de su alcance, siempre con evidencia comando+salida, y nada
  fuera de él:
  - `obs-plugin` (`src/`): `cmake*`/`ctest*`/`msbuild*` (+ selfchecks
    del plugin). Sin `npm`/`npx`, sin `curl`, sin red productiva.
  - `ux-ui` (`src/`, capa de vista): `cmake*`/`ctest*`/`msbuild*`
    (build + selfchecks de estados de la vista) + capturas del
    operador para lo visual ([P] hasta entonces). Sin `npm`/`npx`,
    sin `curl`, sin Git mutante, sin red productiva; jamás lógica
    de auth/red/secretos (territorio `obs-plugin`).
  - `backend-integrations` (`backend/` + `lab/**`): `python*`/
    `pytest*` (backend) y `node*`/`npm*`/`npx*` (lab Node). Sin `curl`,
    sin `gh`/`gcloud`, sin Git mutante, sin producción. Navegador del
    lab solo contra `localhost` (`browser: ask`; si el motor no lo
    admite rige `deny` —ver `docs/TOOLS.md`— y el lab se valida sin
    navegador).
  - `release-tooling` (`tools/**`): sin ejecución (modifica
    `deploy.ps1`/`package.ps1` sin correrlos); valida solo con
    `git diff --check` y lectura (`opencode debug agents`).
  - `harness-steward` (documental): solo `git diff --check` tras
    editar; nada de tests, build, commit, push, PR ni operaciones
    externas.
  - Lo no ejecutable por el agente asignado (p. ej. E2E con presencia
    física, OAuth con consentimiento, despliegues) se registra como
    [P] con su checklist para el operador/orquestador; nunca se afirma
    éxito sin salida observada.
- Prohibido: `reset --hard`, `clean -fd`, rebase destructivo, force-push sin autorización explícita.
- **COMMIT/PR con integrador Git dedicado (fase G1, 2026-10-25 [D]):**
  solo `git-integration` ejecuta Git mutante, y únicamente lo cubierto
  por el gate escrito vigente (este run: autorizado hasta merge a
  `master`, parada solo si el usuario lo pide; futuros runs exigen
  autorización separada). Evidencia comando + salida por gate; gate
  `verification-before-completion` antes de cada push/PR/merge;
  secreto-scan antes de cada commit.
- **REVIEW antes de PR:** ningún grupo de archivos se integra sin
  revisión previa del diff contra los criterios de aceptación.
- **DOCUMENTATION tras integrar:** `harness-steward` actualiza
  `docs/STATE.md` y `docs/BACKLOG.md` (canónico) con la evidencia de
  integración; ningún otro agente edita esos documentos.
- Si el agente detecta trabajo fuera de alcance (`AGENTS.md` §51): parar, registrar en FINDINGS, preguntar.
