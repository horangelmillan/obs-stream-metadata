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

- Tarea pequeña (≤3 pasos, 1 archivo): plan reducido en el propio mensaje.
- Tarea compleja: plan explícito antes de código (skill `writing-plans`).
- Prohibido: `reset --hard`, `clean -fd`, rebase destructivo, force-push sin autorización explícita.
- Si el agente detecta trabajo fuera de alcance (`AGENTS.md` §51): parar, registrar en FINDINGS, preguntar.
