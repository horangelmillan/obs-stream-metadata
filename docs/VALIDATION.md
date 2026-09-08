# VALIDATION — estrategia progresiva

1. **Estática:** Markdown/CI Phase 0, revisión diff, secret-scan. Sin código aún.
2. **Compilación:** Phase 1+ con toolchain fijado (VS2022/CMake/Qt6/OBS SDK). Registrar comando + salida.
3. **Tests:** unitarios donde aplique; negativos obligatorios `AGENTS.md` §40.
4. **Integración:** dock carga en OBS 32.2.2, sin congelar UI (threading §26), `204`/`401`/`429` manejados (§28).
5. **Manual:** checklist `AGENTS.md` §39 por proveedor + §50 (definición de terminado).
6. **APIs reales:** solo cuando sea necesario; credenciales reales nunca en repo/logs; sin polling (cuota YouTube §11.9).

Toda afirmación "funciona" requiere evidencia (comando + salida). "Probablemente funciona" no es válido.

## P1 validada (T-013, 2026-09-08, OBS 32.2.2 x64)

```text
CONFIGURE: PASS (106.8s; OBS 32.2.2 sources + obs-deps/Qt6 2026-07-15; VS17 2022; SDK 22621)
BUILD:     PASS (obs-stream-metadata.dll 12.800 bytes + .pdb, x64, RelWithDebInfo)
ARTEFACTO: PASS (build_x64/RelWithDebInfo + rundir con locale/en-US.ini)
INSTALACIÓN: PASS (staging verificado; despliegue dev vía OBS_PLUGINS_PATH/DATA_PATH, F-010)
OBS LOAD:  PASS ×2 (módulo en memoria + `[obs-stream-metadata] plugin loaded successfully (version 0.1.0)` en log)
UNLOAD:    PASS ×2 (`[obs-stream-metadata] plugin unloaded` al cerrar)
RELOAD:    PASS (segundo arranque carga de nuevo; unload en caliente no existe en OBS — limitación documentada, F-012)
ESTABILIDAD: PASS (A load, B reapertura, C ciclo unload/reload vía shutdown/startup, D cierre limpio sin crash ni sentinels)
FINAL: P1 PASS
```

Procedimiento reproducible: ver `docs/TROUBLESHOOTING.md` (CWD, sentinels, env-vars).

## P2 validada (T-020, 2026-09-08, OBS 32.2.2 x64)

```text
BUILD:         PASS (dock-poc.cpp, sin errores; deps: obs/obs-frontend-api/Qt6Core/Qt6Widgets/VC-runtime)
INSTALACIÓN:   PASS (mismo procedimiento P1: staging + OBS_PLUGINS_PATH/DATA_PATH)
DOCK VISIBLE:  PASS (HWND OBSDock "Stream Metadata" VISIBLE=True; toggle+show verificados a nivel ventana)
CONTENIDO:     PASS (autochequeo: dock created (3 labels); captura de píxeles Qt imposible en esta sesión, F-014)
INTERACCIÓN:   PASS (mover/redimensionar vía SetWindowPos verificado; cerrar vía WM_CLOSE verificado; reabrir verificado)
PERSISTENCIA:  PASS (geometría 40,40 400x300 restaurada por DockState en rearranque sin tocar nada)
CIERRE LIMPIO: PASS ×5 (dock removed + plugin unloaded; sin crash; sentinels vacíos)
SEGUNDA CARGA: PASS ×5 (5 ciclos abrir/cerrar)
FINAL: P2 PASS (pendiente confirmación manual de 30 s por el usuario: Paneles → Stream Metadata → arrastrar/acoplar)
```
