# RESEARCH — reglas de investigación

`AGENTS.md` es la base; se actualiza solo con evidencia oficial nueva.

1. Fuente primaria = documentación oficial (OBS, Twitch, YouTube, Kick, Qt, CMake). Registrar siempre: URL + versión + fecha consulta.
2. `context7` para sintaxis/config de dependencias; `websearch` para detectar cambios; el snippet antiguo nunca es autoridad.
3. Distinguir **hecho** (cita oficial) de **hipótesis** (marcar como tal + cómo verificarla).
4. Si hay contradicción con `AGENTS.md`: NO resolver en silencio → registrar en `FINDINGS.md` (F-###), verificar oficial, y actualizar el doc correspondiente en el mismo PR.
5. Foco Phase 1+: confirmar versión OBS objetivo, template `obs-plugintemplate`, Frontend API docks, Qt6, flujos OAuth vigentes por proveedor, endpoints/límites/scopes, modelo `liveBroadcast` YouTube.
6. Plantilla de cita: `> Fuente: <url> — vX.Y — consultado YYYY-MM-DD — afirma: <cita breve>`.
