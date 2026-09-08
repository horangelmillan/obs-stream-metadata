# OBS Stream Metadata (Twitch / YouTube / Kick)

Plugin nativo de OBS Studio (C++ / Qt6) para gestionar **título y descripción de streams**.
Estado: **Phase 0 — entorno agente listo, sin código funcional**.

Fuente técnica principal: `AGENTS.md` (investigación previa, 2026-09-07).
Reglas operativas para agentes: `AGENTS.md` + `docs/`.

## Alcance MVP (resumen)

- Conectar cuentas Twitch / YouTube / Kick (OAuth).
- Editar título (+ descripción solo YouTube) y aplicar por plataforma.
- Resultado independiente por plataforma; sin todo-o-nada.
- Restricción clave: Twitch y Kick **no** tienen descripción de stream equivalente a YouTube.

Fuera de alcance: multichat, multistream, categorías, tags, presets, thumbnails, analytics, scheduling, EventSub, cloud sync (ver `AGENTS.md` §51).

## Estructura

```text
AGENTS.md            investigación y contexto técnico (fuente primaria)
README.md            este archivo
docs/                harness: STATE, WORKFLOW, TOOLS, RESEARCH, GIT, BACKLOG,
                     FINDINGS, DECISIONS/, PHASES, VALIDATION, SECURITY, TROUBLESHOOTING
.github/             PR template + CI Phase 0
```

## Flujo de trabajo

1. Leer `docs/STATE.md` (dónde estamos).
2. Seguir `docs/WORKFLOW.md`.
3. Respetar `docs/TOOLS.md`, `docs/GIT.md`, `docs/SECURITY.md`.
4. Actualizar `docs/BACKLOG.md` / `docs/FINDINGS.md` / `docs/DECISIONS/` si el conocimiento afecta trabajo futuro.

## Estado actual

Ver `docs/STATE.md`. Próximo paso: Phase 1 — esqueleto plugin OBS (pendiente de definir tras Phase 0).
