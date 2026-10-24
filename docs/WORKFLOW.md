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
| VALIDATION | probar antes de afirmar | `VALIDATION.md` | build/tests/CI | evidencia (comando + salida) |
| REVIEW | detectar regresión/fuera-alcance | diff | `requesting-code-review` si complejo | checklist ok |
| DOCUMENTATION | persistir conocimiento útil | hallazgo/ADR/backlog | editar solo docs afectados | STATE/BACKLOG/FINDINGS/ADR al día |
| COMMIT/PR | integrar controlado | `GIT.md` | commit atómico + PR | PR con template + CI |
| NEXT | continuar sin perder contexto | STATE | actualizar STATE | siguiente T-### |

Reglas:

- **Jerarquía documental (AGENTS.md §55):** BACKLOG manda en estados; STATE resume; DEPLOYMENT distingue verificado/documentado/pendiente; VALIDATION no reescribe el pasado; PROJECT_CONTEXT enlaza, no duplica.
- **El agente ejecuta todo lo ejecutable (OBLIGATORIO, T-076):** si un comando puede correr en su propio entorno (build, tests, `package.ps1`, `gcloud`, `gh`, `curl`, deploy, verificación de instalador), LO EJECUTA ÉL y entrega evidencia + artefacto listo. Al usuario NUNCA se le pide abrir una terminal para algo que el agente ya puede hacer.
- Al usuario solo se le pide lo que exige su presencia física: contraseñas y clics de permiso en Google/Twitch/Kick/Meta (ningún agente puede consentir por él), cerrar OBS para instalar con admin, y verificación visual en OBS/YouTube Studio. Esos pasos se le dan masticados, uno por uno, sin terminal.
- Contexto que originó la regla (2026-10-24): se pidió al usuario compilar y empaquetar tres veces, incluyendo una por un error de compilación que el agente podía haber detectado compilando él primero. No se repite.
- Tarea pequeña (≤3 pasos, 1 archivo): plan reducido en el propio mensaje.
- Tarea compleja: plan explícito antes de código (skill `writing-plans`).
- Prohibido: `reset --hard`, `clean -fd`, rebase destructivo, force-push sin autorización explícita.
- Si el agente detecta trabajo fuera de alcance (`AGENTS.md` §51): parar, registrar en FINDINGS, preguntar.
