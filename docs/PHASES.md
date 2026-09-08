# PHASES — plan real del proyecto (T-010, 2026-09-08)

Plan definitivo hasta nuevo ADR. Orden **risk-first** según `AGENTS.md` §52:
el riesgo está en OAuth/metadata (sobre todo YouTube), no en el formulario Qt.
Por eso los PoCs de proveedor van **antes** que la UI unificada, y nada se
declara done sin operar contra APIs reales.

```text
P0 Environment ............ CERRADA (harness, sin código)
P1 Toolchain & Skeleton ... instalar toolchain (ADR-005) + template compila
P2 Dock PoC ............... dock Qt6 mínimo carga en OBS 32.2.2 (T-020)
P3 Provider PoCs .......... OAuth + cambio real título/desc por proveedor
P4 Unified Dock ........... UI mínima real título/descripción + apply
P5 Hardening .............. storage seguro, refresh, threading, errores, logs
P6 Integration Testing .... casos AGENTS.md §39-40 + gate §50
P7 Packaging & Release .... instalador/estructura plugin + release
```

Regla: no detallar la fase N+1 hasta cerrar la fase N (solo se adelanta lo
estrictamente necesario para estimar). Cada fase abre rama `feat/<T-id>-slug`
y cierra con PR (CI verde + aprobación).

---

## P0 — Environment ✅ CERRADA

- **Objetivo:** harness de agente (docs, CI, Git) sin código funcional.
- **Salidas:** `docs/` completo, CI Phase 0 verde, ADRs 001-004, F-001..F-005.
- **Criterio de salida (cumplido):** `master` limpio, cero `src/`/`CMakeLists.txt`.

## P1 — Toolchain & Skeleton (T-011 resto + T-012 + T-013) ✅ VALIDADA 2026-09-08

- **Objetivo:** entorno reproducible que compila el template oficial sin funcionalidad propia.
- **Alcance:** instalar VS17 2022 + SDK + CMake (ADR-005); clonar `obs-plugintemplate` como referencia; compilarlo con `ENABLE_FRONTEND_API=ON ENABLE_QT=ON`; añadir CI C++ (build Windows x64).
- **No-objetivos:** ningún dock propio, ningún OAuth, ningún cambio funcional.
- **Dependencias:** ADR-005. **Riesgo R1:** el template fija obs-studio 31.1.1 — subir a 32.2.2 y verificar.
- **Entrada:** ADR-005 aprobado. **Salida:** build verde local + CI del template compilado.
- **Validación:** `cmake --build` ok; artefacto `.dll/.pdb` generado; `git status` limpio de secretos.
- **Resultado T-013:** `buildspec.json` propio (OBS 32.2.2 + obs-deps/Qt6 2026-07-15, ADR-007); configure ok (106.8s, VS2022/SDK22621); `obs-stream-metadata.dll` x64 (12.800 bytes + PDB); carga + unload en OBS 32.2.2 real ×2 (log + módulos en memoria); Qt 6.11.1 == 6.11.1; sin crash, sentinels limpios. CI C++ pendiente (difere a P2; build local es la evidencia P1).

## P2 — Dock PoC (T-020) ✅ VALIDADA 2026-09-08

- **Objetivo:** demostrar que un dock propio carga en OBS 32.2.2 sin congelar la UI.
- **Alcance:** `OBS_DECLARE_MODULE` + `obs_module_load` + `QWidget` registrado vía `obs_frontend_add_dock_by_id` (id `obs-stream-metadata-dock`, título `Stream Metadata`); 3 labels PoC; log `[obs-stream-metadata]` sin secretos.
- **No-objetivos:** OAuth, HTTP contra plataformas, persistencia, estética.
- **Dependencias:** P1. **Riesgos:** incompatibilidad Qt (ver R2 de ADR-005); threading desde el inicio (nada de red en hilo UI, `AGENTS.md` §26).
- **Entrada:** template compila. **Salida:** plugin carga/descarga, dock abre/cierra.
- **Validación:** checklist manual: OBS lo lista, abre/cierra sin crash, log limpio.
- **Resultado T-020:** `add_dock_by_id` elegido tras leer `OBSStudioAPI.cpp`/`OBSBasic` (F-013); ownership OBS (plugin nunca borra); `remove_dock` en unload (no-op seguro en shutdown); geometría restaurada por `DockState` (40,40 400x300 verificado); contenido autoverificado (`3 labels` en log); 5 ciclos abrir/cerrar limpios, sin crash ni sentinels. Límite del harness: sin píxeles de contenido Qt ni toggle de menú automatizable en sesión sin foreground (F-014); verificación manual de 30 s pendiente por el usuario (Paneles → Stream Metadata → arrastrar).

## P3 — Provider PoCs (T-030: Twitch, YouTube, Kick)

- **Objetivo:** demostrar OAuth + cambio real de metadata por proveedor **antes** de construir la UI definitiva.
- **Alcance por proveedor:**
  - **Twitch:** OAuth (`channel:manage:broadcast`) → `PATCH /helix/channels` título (≤140). Descripción: NO soportada (F-001).
  - **YouTube:** OAuth (`youtube.force-ssl` a evaluar) → `liveBroadcasts.list` (selector) → `liveBroadcasts.update` título (1-100) + descripción (≤5000). **No crear broadcasts** (`AGENTS.md` §41).
  - **Kick:** OAuth 2.1+PKCE (`channel:write` + `user:read`/`channel:read` si hace falta identidad) → `PATCH /public/v1/channels` `stream_title`; `204` = éxito. `channel_description` NO es descripción de stream (F-003).
- **No-objetivos:** UI unificada, storage seguro definitivo, refresh automático (puede ser manual en el PoC).
- **Dependencias:** P2. **Riesgos:** selección de broadcast YouTube (F-004, el mayor riesgo del proyecto); redirect URIs `localhost` en Kick (`AGENTS.md` §14.9); cuota YouTube (sin polling).
- **Entrada:** dock carga. **Salida:** título cambiado y verificado en cada plataforma + descripción en YouTube.
- **Validación:** verificación en web de cada plataforma; tokens de prueba nunca en repo/logs.
- **Resultado T-030 (2026-09-08):** P3 PASS — triple LIVE PASS con read-back (Twitch device flow; YouTube Desktop+PKCE, `mine` solo, PUT `id+snippet`; Kick PKCE+localhost, 204, read-back en directo por doble vía). Findings F-019–F-024. Descripción YouTube queda para P4 (solo se validó título, sin regresión).

## P4 — Unified Dock (UI mínima real) — EN-PROGRESO (T-031, 2026-09-08)

Implementado en `feat/t-031-mvp-integration` (PARTIAL, sin validación viva):
`src/metadata.*` (modelo + validación restrictiva + payloads + mensajes §16),
`src/metadata_dock.*` (checkboxes, título, descripción solo-YouTube,
selector de broadcast, Connect/Disconnect por proveedor, Apply secuencial
async con resultado independiente, refresh-una-vez ante 401), tokens solo
en memoria (limitación documentada, P5 decide storage), gate CI P1→P4.
Evidencia: build 0 errores, selfchecks 19/19 + 28/28, secret-scan limpio,
OBS 32.2.2 ×2 ciclos limpios. Pendiente del operador: OAuth real + updates
visibles por plataforma (matriz §19).

- **Objetivo:** el dock usable del MVP (`AGENTS.md` §37): cuentas vinculadas, título, descripción, selector de broadcast YouTube, aplicar por plataforma, resultado independiente por plataforma (sin todo-o-nada, §19).
- **Alcance:** formulario Qt; matriz de capacidades visible (Twitch/Kick: descripción no disponible, §3); estado conectado/como-quién (§38); conectar/desconectar por proveedor (§29).
- **No-objetivos:** categorías/tags/presets/thumbnails (§43, §51); robustez final (va en P5).
- **Dependencias:** P3 (los flujos ya demostrados, aquí solo se unifican).
- **Entrada:** PoCs verificados. **Salida:** aplicar desde el dock funciona en las 3 plataformas.
- **Validación:** matriz §19 (fallo Twitch no bloquea YouTube/Kick y viceversa).

## P5 — Hardening

- **Objetivo:** que el MVP sea fiable y seguro, no solo funcional.
- **Alcance:** persistencia de tokens que sobrevive reinicios con almacenamiento OS-apropiado (nunca plaintext en JSON, §9/§16); refresh + detección 401→reconexión; mapeo de errores §28 (400/401/403/404/409/429/5xx + `204` como éxito); red fuera del hilo UI; logs útiles sin secretos (§27); validaciones locales §20.
- **No-objetivos:** nuevas funcionalidades.
- **Dependencias:** P4. **Riesgos:** mecanismo de storage seguro en Windows por investigar en su momento.
- **Entrada:** dock unificado funciona. **Salida:** reinicio/revocación/rate-limit se comportan según §28-29.
- **Validación:** pruebas de expiración/revocación por proveedor (pasos §39.5-9).

## P6 — Integration Testing

- **Objetivo:** ejecutar completos los checklists `AGENTS.md` §39 (positivos) y §40 (negativos).
- **Alcance:** solo pruebas y fixes derivados; sin features nuevas.
- **Dependencias:** P5. **Riesgos:** cuota YouTube (minimizar llamadas); flakiness de red (backoff limitado, sin loops ante 429).
- **Entrada:** hardening completo. **Salida:** §39-40 en verde documentado.
- **Validación:** gate `AGENTS.md` §50 (definición de terminado del MVP).

## P7 — Packaging & Release

- **Objetivo:** instalación estándar del plugin (estructura OBS, wiki "Distribute Your Plugin" / InnoSetup del template).
- **Alcance:** empaquetado, instrucciones de instalación, release en GitHub.
- **No-objetivos:** marketplace/autos ni features.
- **Dependencias:** P6 (gate §50 en verde).
- **Entrada:** MVP terminado según §50. **Salida:** `Instalar → OBS → Docks → Stream Metadata → conectar → aplicar` (§36).
