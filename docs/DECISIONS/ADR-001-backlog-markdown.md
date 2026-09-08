# ADR-001 — Backlog/hallazgos/ADR en Markdown en repo

- **Problema:** agentes pierden contexto entre sesiones; se necesita trazabilidad sin infraestructura extra.
- **Opciones:** (a) Markdown en repo; (b) GitHub Issues/Projects.
- **Elección:** (a) Markdown (`BACKLOG.md`, `FINDINGS.md`, `DECISIONS/ADR-###.md`).
- **Por qué:** decisión explícita del usuario (2026-09-08); cero dependencias; versionado con el código; editable con las mismas herramientas.
- **Evidencia:** repo sin remoto al inicio; flujo local-first más simple (YAGNI).
- **Consecuencias:** mantener tablas al día en cada PR; migrar a Issues solo con ADR nuevo si escala.
