# VALIDATION — estrategia progresiva

1. **Estática:** Markdown/CI Phase 0, revisión diff, secret-scan. Sin código aún.
2. **Compilación:** Phase 1+ con toolchain fijado (VS2022/CMake/Qt6/OBS SDK). Registrar comando + salida.
3. **Tests:** unitarios donde aplique; negativos obligatorios `AGENTS.md` §40.
4. **Integración:** dock carga en OBS 32.2.2, sin congelar UI (threading §26), `204`/`401`/`429` manejados (§28).
5. **Manual:** checklist `AGENTS.md` §39 por proveedor + §50 (definición de terminado).
6. **APIs reales:** solo cuando sea necesario; credenciales reales nunca en repo/logs; sin polling (cuota YouTube §11.9).

Toda afirmación "funciona" requiere evidencia (comando + salida). "Probablemente funciona" no es válido.

## T-048 Managed wiring (2026-09-09, `managed-link-test.exe`, cliente C++ real)

```text
BUILD:     PASS (obs-stream-metadata.dll + metadata-selfcheck.exe + managed-link-test.exe, 0 errores)
SELFCHECK: PASS 89/89 · BACKEND: PASS 70/70 (incl. regresión alias /connect/youtube)
YT MANAGED LIVE: PASS — bootstrap → session (HMAC C++↔Python) → /connect/youtube →
                 browser + consent → callback → status connected →
                 disconnect → disconnected (secret/tokens jamás en plugin/logs)
KICK MANAGED LIVE: PASS — idem vía localhost:3000 (secret backend-only)
SECRET:    PASS (scan patrón CI limpio; temp store borrado; dev.env intacto y sin traquear)
DOCK:      cableado compilado; pixel-click pendiente operador (F-014);
           smoke OBS previo con `dock ready (mode=Independent)` vigente
FINAL: T-048 PASS (sin licencias/pagos/producción; matrices T-050–T-052 cerradas abajo)
```

## T-050/T-051/T-052 cierre (2026-09-09/10, commit `8ebe9cb`, PR #19)

```text
BUILD:     PASS (obs-stream-metadata.dll + metadata-selfcheck.exe, exit 0, 0 errores;
            revalidado 2026-09-10 sobre master mergeado)
SELFCHECK: PASS 106/106, 0 FAIL (89 heredados + 17 nuevos msnap-* T-051; el CHECK 18
            `msnap-rt-skipped-non-windows` solo ejecuta fuera de Windows)
BACKEND:   PASS 73/73 (70 heredados + 3 nuevos test_cross_provider.py T-052:
            mismatch youtube/kick, mismatch kick/youtube, twitch-managed-unsupported)
YT MANAGED LIVE: PASS 2026-09-09 — bootstrap → session → connect → browser consent →
            callback → status → disconnect (revalidado; no repetido el 2026-09-10)
KICK MANAGED LIVE: PASS 2026-09-09 — idem vía localhost (revalidado; no repetido el 2026-09-10)
SECRET:    PASS (patrón CI ci-phase0.yml sobre árbol trackeado: limpio; único hit local
            en dev.env ignorado .gitignore:60, sin traquear; snapshots Managed sin secretos
            verificado por msnap-no-secret-shapes)
AUDIT:     ADR-012 intacto; managedSupported()==false para Twitch; loadStore sin auto-fetch;
            Account ≠ ManagedConn; sin fallback silencioso
DOCK:      pixel-click pendiente operador (F-014); checklist cross-mode en TROUBLESHOOTING T-052
FINAL: T-050 PASS + T-051 PASS + T-052 PASS con salvedad GUI-clicks operador PENDING
```

## T-053 entorno explícito DEV/PROD (2026-09-10)

```text
BUILD:     PASS (obs-stream-metadata.dll + metadata-selfcheck.exe +
            managed-link-test, exit 0, 0 errores)
SELFCHECK: PASS 115/115, 0 FAIL (106 heredados + 9 nuevos envbind-*: match,
            mismatch-cross, legacy-empty, empty-current, roundtrip + shape)
BACKEND:   PASS 88/88 (73 heredados + 15 nuevos test_environment.py:
            normalize/gates/version, sin secretos reales)
SECRET:    PASS (patrón CI ci-phase0.yml sobre árbol trackeado: limpio;
            dev.env local ignorado .gitignore:60, sin traquear)
AUDIT:     ADR-012 intacto; ConnectionMode intacto (sin DevMode/ProdMode);
            T-050/T-051/T-052 intactas; env explícito sin fallback
LIVE:      no repetido (sin credenciales nuevas en esta sesión; YT/Kick
            Managed PASS 2026-09-09 siguen vigentes)
FINAL: T-053 PASS
```

## T-054 producción-capable (2026-09-10, ADR-013)

```text
BUILD:     PASS (sin cambios C++; dll + selfcheck + managed-link-test
            vigentes de T-053, exit 0)
SELFCHECK: PASS 115/115, 0 FAIL (sin cambios; regresión intacta)
BACKEND:   PASS 104/104 (88 heredados + 16 nuevos: 14 test_prodstores.py +
            2 gates AllowAll en test_environment.py; fakes sintéticos)
SECRET:    PASS (patrón CI ci-phase0.yml sobre árbol trackeado: limpio;
            dev.env local ignorado, sin traquear)
PROD-SMOKE: PASS local (settings production + FileSecretStore + SQLite +
            limiter explícito → serve → /version {"env":"production"} +
            /ready true + meta.db creado; script temporal, fuera del repo)
TLS:       config validada (a medias/ausente = fail-fast); handshake real
            con certificado pendiente del operador (DEPLOYMENT.md checklist)
AUDIT:     ADR-012 intacto; T-050/T-051/T-052/T-053 intactas (88→104 sin
            borrar tests; test_environment actualizado por gate más
            estricto, no por regresión);Managed sigue backend-only
CI:        nuevo job backend-tests (stdlib, 3.13, sin credenciales)
FINAL: T-054 PASS CON SALVEDAD (production-capable; no production-deployed)
```

## T-055 PostgreSQL portable + Cloud Run/Neon (2026-09-10, ADR-014)

```text
BUILD:     PASS (sin cambios C++; dll + selfcheck + managed-link-test
            vigentes; backend: imports + suite en verde)
SELFCHECK: PASS 115/115, 0 FAIL (sin cambios; regresión intacta)
BACKEND:   PASS 116/116 (104 heredados + 12 nuevos test_pg.py contra
            PostgreSQL 18 real local: URL/migrations/CRUD/constraints/
            pool/multi-instancia/wiring; 1 ajuste legítimo en
            test_prodstores por SQLite→DEV-only)
MIGRATIONS: PASS (001 desde cero + idempotencia + orden, en PG real)
PG-SMOKE:  PASS (prod wiring + serve + /version.env + /ready sobre PG)
DEV-SMOKE: PASS (dev in-memory intacto tras T-055)
DOCKER:    no validado por build (daemon inactivo); HEALTHCHECK one-liner
            validado contra servidor real; sintaxis revisada
SECRET:    PASS (patrón CI sobre árbol trackeado: limpio; dev.env local
            ignorado; sin credenciales reales en código/tests/docs/CI)
AUDIT:     ADR-012 intacto; T-050/T-051/T-052/T-053/T-054 intactas
            (test_environment intacto salvo 2 gates T-054 vigentes);
            Managed backend-only; Account≠ManagedConn; sin `NEON_*`
LIVE:      no repetido (Managed PASS 2026-09-09 vigentes; sin cambios)
FINAL: T-055 PASS CON SALVEDAD (despliegue Cloud Run+Neon pendiente
            del operador; ver DEPLOYMENT.md checklist)
```

## T-056 bind production 0.0.0.0 (2026-09-10)

```text
CAMBIO:   load_settings() — dev default 127.0.0.1, prod default 0.0.0.0,
            STREAM_META_BACKEND_HOST explícito siempre gana; PORT intacto.
            Causa: primer despliegue Cloud Run FALLÓ (bind loopback).
BACKEND:  PASS 121/121, 0 skips con PG local (104+12 heredados T-055 +
            5 nuevos BindHostTest); 115 selfcheck intactos (sin cambios C++)
SECRET:   PASS (patrón CI; dev.env local ignorado, sin traquear)
DEPLOY:   reintento PENDIENTE (startup, /health, /ready, /version, PG,
            secreto, PUBLIC_URL, providers prod por verificar)
FINAL: T-056 PASS (código+tests+docs; despliegue pendiente de reintento)
```

## T-057 Secret Manager ↔ FileSecretStore (2026-09-10)

```text
ANÁLISIS:  FileSecretStore ya esperaba mounts (docstring T-054); Cloud Run
            --set-secrets volúmenes satisfacen el contrato sin cambiarlo.
            Sin workaround /tmp; sin eliminar exigencia SECRET_DIR.
CAMBIO:    required_secret_names por adapter + verificación al arrancar en
            prod (nombres faltantes en el error, nunca valores).
BACKEND:   PASS 126/126 local (121 heredados + 5 nuevos
            ProviderSecretsBootCheckTest, con dobles; PG server apagado →
            7 skips por diseño, validados con servidor en T-055/56)
SECRET:    PASS (patrón CI; sin valores reales en código/tests/docs)
DEPLOY:    rev-00002 FALLIDA documentada (SECRET_DIR ausente); contrato de
            montaje + SECRET_DIR + enablement documentados; reintento y
            OAuth remoto PENDIENTES
FINAL: T-057 PASS (análisis+contrato+cambio mínimo+docs)
```

## P6 parcial (T-033, 2026-09-09, rama `feat/t-033-integration-testing`)

```text
CONFIGURE: PASS (preset windows-x64 incremental, Qt6 Network vía obs-deps, crypt32 SDK)
BUILD:     PASS (plugin .dll + metadata-selfcheck.exe + provider-poc, RelWithDebInfo, 0 errores)
SELFCHECK: PASS 68/68 (metadata: 35 heredados T-031/T-032 + 33 nuevos §40 T-033:
            youtube-empty/100, ytdesc-5000, mixed-141, mapa §28 200/400/403/
            404/409/500/503/0/999, message-safe-all + 6 mensajes específicos,
            ytpl id/title/desc/preserve/no-contentdetails, backoff-3/neg,
            revoke shape tw/yt/kick) + 28/28 (provider-poc, regresión intacta)
SECRET:    PASS (gate P4 + scan de valores en src/tools/docs, incl. ficheros nuevos; logs solo longitudes/códigos)
CI:        pendiente (PR por abrir)
LIVE:      PENDIENTE OPERADOR (§39-40 con cuentas reales: procedimiento en
            TROUBLESHOOTING T-033; gate §50 no se declara verde sin él)
FINAL: P6 PARTIAL — no avanzar a P7 hasta el live del operador.
```

## P5 parcial (T-032, 2026-09-09, rama `feat/t-032-hardening`)

```text
CONFIGURE: PASS (preset windows-x64 incremental, Qt6 Network vía obs-deps, crypt32 SDK)
BUILD:     PASS (plugin .dll + metadata-selfcheck.exe + provider-poc, RelWithDebInfo, 0 errores)
SELFCHECK: PASS 35/35 (metadata: 19 heredados + backoff-*/revoke-*/store-* T-032) + 28/28 (provider-poc, regresión intacta)
           DPAPI round-trip real en máquina dev: save→load iguales, fichero sin valores en claro, clear borra
SECRET:    PASS (gate P4 + scan de valores en src/tools/docs, incl. ficheros nuevos; logs solo longitudes/códigos)
CI:        pendiente (PR por abrir)
LIVE:      PENDIENTE OPERADOR (OBS en uso durante la sesión: sin smoke del DLL) —
           reinicio con cuentas (DPAPI restore), revoke en web → 401 → refresh/reconexión (§39.5-9),
           429/5xx con Retrying… acotado, matriz T1–T20 sin regresión, `tls backend ready: yes`,
           cero secretos en %APPDATA%\obs-studio\logs
FINAL: P5 PARTIAL — no avanzar a P6 hasta el live del operador (P6/T-033 lo ejecuta).
```

## P4 validada (T-031 PASS, 2026-09-08/09, rama `feat/t-031-mvp-integration`)

```text
CONFIGURE: PASS (preset windows-x64 incremental, Qt6 Network vía obs-deps)
BUILD:     PASS (plugin .dll + metadata-selfcheck.exe, RelWithDebInfo, 0 errores)
SELFCHECK: PASS 19/19 (metadata) + 28/28 (provider-poc, regresión intacta)
SECRET:    PASS (gate P4 + scan de valores en src/tools/docs; logs sin secretos)
CI:        PASS (phase0-gates success en PR #9, incl. fixes)
LIVE DOCK: PASS — T1 Twitch OAuth+PATCH+read-back web; T2 YouTube OAuth+lista
           (Y9yFOeQw83s)+PUT título+descripción+read-back Studio (edición
           posterior del operador explica el texto actual); T3 Kick OAuth+
           PATCH 204+read-back dashboard (offline, regla F-023 anotada)
MULTI:     PASS T4 (✓✓✓ + triple dashboard) — T6/T7/T8 cubiertos por T1/T2/T3
DESC:      PASS T5 (solo YouTube; Twitch/Kick ignoran sin error)
NEGATIVOS: PASS T9 (max 100 bloquea sin red) / T10 (5000 bloquea) /
           T11 (sin plataforma) / T12 (sin auth) / T13 (✓✓✗ parcial) /
           T14 (OAuth cancel → Error, sin cuelgue) / T15 (401 → refresh
           único → Needs reconnection, sin loops)
ASYNC:     PASS T16 (dock movible durante Apply)
LIFECYCLE: PASS T17/T18 (ciclos elegantes, sin crash ni sentinels) +
           persistencia de acoplamiento verificada; dock acoplado con
           scroll (F-028), `tls backend ready: yes` en log
FIXES LIVE: F-026 (callback first-wins + puerto Kick 3000), F-027 (TLS
           Schannel empaquetado + connectHttpError + DATA_PATH dev),
           F-028 (scroll + resultado=connected)
FINAL: P4 PASS — T-031 cerrada. No avanzar a P5 en esta sesión.
```

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

## P3 parcial (T-030, 2026-09-08, rama `feat/t-030-poc-providers`)

```text
CONFIGURE: PASS (preset windows-x64, 5.2s incremental; frontend+Qt ON)
BUILD:     PASS (plugin .dll + provider-poc.lib, RelWithDebInfo, 0 errores)
SELFCHECK: PASS 28/28 (PKCE RFC 7636, state, URLs 3 proveedores,
           validadores §20, payloads exactos, merge YouTube, HTTP §28
           incl. 204=éxito) — provider-poc-selfcheck.exe, exit 0
CI-LOCAL:  PASS (grep gate P1 sin matches en src/cmake; secret-scan sin matches)
TWITCH:    PASS vivo 2026-09-08 (OAuth device flow; /validate login sonokigame /
           user 182281392 / scope channel:manage:broadcast; PATCH 204 + read-back
           "T030 LIVE Twitch 20260908"; tokens revocados; runner redactado F-018)
YOUTUBE:   PASS vivo 2026-09-08 (OAuth Desktop+PKCE+loopback; token con
           secret local F-019; list mine=true F-020; broadcast Y9yFOeQw83s
           ready; PUT 200 solo id+snippet F-021 + read-back "T030 LIVE
           YouTube test1"; revoke access 200; runner tools/t030_live_youtube.py)
KICK:      PASS vivo 2026-09-08 (OAuth 2.1+PKCE localhost sonokigame/128456005;
           token tras fix Cloudflare-UA F-022; PATCH 204 + read-back en directo
           por doble vía "T030 LIVE Kick test1" F-023; revoke 200/200;
           runner tools/t030_live_kick.py)
FINAL: P3 PASS — P4 desbloqueada
```
