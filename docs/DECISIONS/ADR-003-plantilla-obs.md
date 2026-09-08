# ADR-003 — Partir del template oficial obs-plugintemplate

- **Problema:** base del plugin nativo OBS (CMake, Qt6, Frontend API docks).
- **Opciones:** (a) template oficial `obsproject/obs-plugintemplate`; (b) plugin antiguo de GitHub; (c) desde cero.
- **Elección:** (a) template oficial como única base (pendiente de clonar en Phase 1).
- **Por qué:** `AGENTS.md` §6-8 lo prescribe; trae CMake moderno, Qt6, `ENABLE_FRONTEND_API/QT`, presets y workflows.
- **Evidencia:** docs OBS 32.2.2; `obs_frontend_add_dock_by_id` desde OBS 30.0.
- **Consecuencias:** Phase 1 empieza clonando template como referencia; no copiar tutoriales viejos si contradicen docs actuales.
