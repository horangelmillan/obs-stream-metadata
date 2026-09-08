# ADR-006 — Organización en fases risk-first (P0-P7)

- **Fecha:** 2026-09-08 · **Tarea:** T-010 · **Estado:** decidido.
- **Problema:** dividir el proyecto en fases que guíen a futuros agentes sin burocracia y sin esconder el riesgo real.
- **Opciones:** (a) fases por capa técnica (skeleton → UI → auth → providers → release); (b) fases risk-first: PoCs de OAuth/metadata por proveedor **antes** de la UI unificada, hardening y packaging al final.
- **Elección:** (b) P0-P7 según `docs/PHASES.md`.
- **Por qué:**
  - `AGENTS.md` §52 lo prescribe: el riesgo no está en el formulario Qt sino en las diferencias OAuth/metadata (Twitch título-solo, YouTube selección de broadcast, Kick 204/PKCE).
  - Construir la UI unificada antes de demostrar los tres flujos llevaría a rediseños (el selector de broadcast de YouTube condiciona la UI, F-004).
  - Separar P5-hardening de P4-UI evita dar por "terminado" algo funcional pero inseguro (tokens en plaintext, red en hilo UI).
  - P6 existe porque `AGENTS.md` §39-40 y §50 exigen evidencia por proveedor, no afirmaciones.
- **Consecuencias:**
  - BACKLOG: T-030 cubre P3 (PoCs por proveedor); se añaden T-012 (instalar toolchain, P1), T-031 (dock unificado, P4), T-032 (hardening, P5), T-033 (testing §39-40, P6), T-034 (packaging, P7).
  - No detallar la fase N+1 hasta cerrar la N; este ADR solo se revisa si la evidencia de P1-P3 contradice el orden.
