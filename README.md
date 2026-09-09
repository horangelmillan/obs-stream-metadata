# OBS Stream Metadata (Twitch / YouTube / Kick)

Plugin nativo de OBS Studio (C++ / Qt6) para gestionar **título y descripción de streams**.
Estado: **Phase 4 (P4) validada — dock MVP funcional en OBS 32.2.2 (Twitch/YouTube/Kick, matriz viva T1–T20 en verde, T-031 PASS)**.

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

Ver `docs/STATE.md` (fuente de verdad).

- **Phase 0 cerrada:** harness agente (docs, CI, Git) sin código funcional.
- **Fases P0–P7 definidas** (`docs/PHASES.md`, ADR-006), orden risk-first.
- **Toolchain instalado y validado (T-012):** VS2022 17.14 + MSVC 19.44 + ATL + SDK 10.0.22621 + CMake 3.31.6 + Git 2.49.0; target OBS 32.2.2 + Qt 6.11.1; sin Qt manual (ADR-005).
- **Próximo paso:** P5 — Hardening (T-032): storage seguro OS, revoke, backoff, distribución de secrets.
