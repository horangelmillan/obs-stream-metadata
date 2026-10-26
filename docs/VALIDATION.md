# VALIDATION — estrategia progresiva

> Nota de lectura (2026-10-26): este archivo es registro histórico de
> evidencia; se añaden entradas, no se reescribe el pasado (AGENTS.md
> §55.6). Estado vigente: fix trío señal/listo/contador implementado
> sin commit (detalle abajo; E2E pendiente). Los conteos antiguos
> (134/218/244/249) son fotografías de su sesión, no el estado actual:
> en este checkout hay 344 invocaciones `CHECK(` en
> `src/metadata_selfcheck.cpp` (conteo estático [C], sin ejecutar aquí).
> Últimos resultados documentados [D]: build OK + selfcheck 367/0
> (fix trío, delegado de la sesión implementadora).

## Fix trío señal/listo/contador (2026-10-26 — IMPLEMENTADO SIN COMMIT; T-078 sin cambio de estado)

```text
FUENTE [D delegada de la sesión implementadora, no re-ejecutada
  aquí; comunicada en la tarea que autoriza, 2026-10-26]:
  causa raíz — el dock leía GET /v3/config/paths/get/live
  (config estática PathConf, sin estado) en vez de
  GET /v3/paths/get/live (runtime Path con online/source/
  inboundBytes); por eso signalHasEmitter era falso siempre con
  Aitum emitiendo.
  Fix — endpoint runtime + parse reescrito + predicado único
  fbEmitReadiness (verde exige las tres; ámbar "lista pero sin
  señal") + contador con causa y gate lastOk<=0 + log
  diagnóstico con conteos.
  Validación — build OK, selfcheck 367/0, diff-check y
  secret-scan limpios.
BASE [C, esta subtarea documental]: solo edición de docs
  (`VALIDATION.md`, `STATE.md`, `PROJECT_CONTEXT.md`) +
  `git diff --check`; sin commit/push, sin secretos, sin
  `staging/`/`dev.env`. No reescribe historia: las entradas
  anteriores quedan intactas abajo.
ESTADO [C contra BACKLOG]: T-078 SIGUE en investigación, sin cambio
  de estado en esta subtarea (canónico BACKLOG; esta entrada no
  cambia estados). T-077 intacta.
PENDIENTES [P]: E2E operador + instalador con relay+fix.
FINAL: fix trío documentado; E2E de aceptación pendiente.
```

## Plan v3 — Fases completadas: lógica + vista (2026-10-26 — IMPLEMENTADO SIN COMMIT; T-078 sin cambio de estado)

> ESTE BLOQUE ES AVANCE DE IMPLEMENTACIÓN, NO E2E: la lógica y la
> vista del plan v3 están implementadas en el checkout sin commit; la
> validación E2E con dos directos reales queda pendiente [P]. Solo
> edición de docs en esta subtarea (`VALIDATION.md`, `STATE.md`,
> `PROJECT_CONTEXT.md`); sin commit/push, sin secretos, sin
> `staging/`/`dev.env`. No reescribe historia: el ajuste v3 [P]
> anterior queda intacto abajo.

```text
FUENTE [D delegada de las sesiones implementadoras, no re-ejecutada
  aquí; comunicada en la tarea que autoriza, 2026-10-26]:
  Fase 1 lógica — arranque exacto por evento E1/E2/E3; escucha
  QFileSystemWatcher + pregunta cada 3 s de respaldo; algoritmo
  adaptativo 3→2→1 con votación 2-de-3 e histéresis; pre-create
  ~6 s; auto-End con cerrojos (solo LIVE auto-iniciado, con
  confirmación Graph fresca, VOD siempre); cola diferida de un solo
  disparo con guarda de ID.
  Avisos runOnOnline/runOnOffline en tools/mediamtx.yml (señal
  online/offline vía ficheros signal.on/signal.off).
  Fase vista — semáforo sin texto (rojo/ruedita/triángulo) +
  contador ~mm:ss informativo + check global de audiencia en
  Avanzado (defecto OFF) + ~40 textos en lenguaje de persona.
  Validación — build OK, selfcheck 344/0, diff-check y secret-scan
  limpios.
BASE [C, esta subtarea documental]: rama
  `feat/facebook-status-port-t077-t078` (HEAD `e4ae8ec`);
  `tools/mediamtx.yml:44-45` contiene los avisos `runOnOnline` /
  `runOnOffline` (leído en esta sesión); 344 invocaciones `CHECK(`
  en `src/metadata_selfcheck.cpp` (conteo estático [C], sin
  ejecutar; coherente con el "344/0" delegado); `git diff --check`
  limpio antes y después de editar (ver informe); sin commit/push,
  sin secretos, sin `staging/`/`dev.env`.
PENDIENTES [P]: E2E operador con dos directos seguidos reales +
  métricas F-099 + audiencia F-093; capturas visuales del
  semáforo/contador/avisos; instalador con relay+fix (build E2E con
  descarga real). Sin afirmar Live/App Review/requisitos (sin
  evidencia).
ALCANCE [D delegada]: según lo delegado, dentro de la excepción
  acotada de ADR-016 (relay local + forward `:9997`, destino
  operador fijo `rtmp://127.0.0.1:1935/live`); sin multistream
  general, sin tocar outputs/servicios/escenas/fuentes, sin keys en
  repo.
ESTADO [C contra BACKLOG]: T-078 SIGUE en investigación, sin cambio
  de estado en esta subtarea (canónico BACKLOG; esta entrada no
  cambia estados). T-077 intacta.
```

## Plan Facebook-automático v3 — ajuste 2026-10-26 (decisiones nuevas del dueño; PLAN NO IMPLEMENTADO; T-078 sin cambio de estado)

> ESTE BLOQUE ES PLAN, NO EVIDENCIA: nada de lo descrito aquí está
> implementado ni verificado en esta sesión documental. Ajusta el plan
> v2 y la validación "dos directos" anteriores (quedan intactos abajo,
> no se reescriben). Solo edición de docs (`VALIDATION.md`,
> `STATE.md`, `PROJECT_CONTEXT.md`, `BACKLOG.md` nota sin cambio de
> estado); sin código, sin commit/push, sin secretos, sin
> `staging/`/`dev.env`.

```text
FUENTE [D]: decisiones NUEVAS del dueño comunicadas en la tarea que
  autoriza (2026-10-26; no re-ejecutadas aquí). Contrasta con:
  plan v2 (arranque E1/E2/E3 sin timers; Go-live auto; semáforo +
  contador; auto-End flanco+antirrebote 60s+gracia 5min; auto-prepare)
  + validación dos-directos (pre-create NO + auto-End NO + cola
  diferida NO + verify/refire SÍ + Apply blinda SÍ + presupuestos
  1 uso + ~30 textos + principio no-técnico) + ADR-016 (excepción
  acotada §§23/51: relay local + forward `:9997`, destino operador
  fijo `rtmp://127.0.0.1:1935/live`).
BASE [C, esta subtarea documental]: rama
  `feat/facebook-status-port-t077-t078` (HEAD `e4ae8ec`);
  `git status --porcelain` al inicio lista 4 ficheros documentales
  modificados sin commit (edición paralela v2 en curso de otra
  subtarea); `git diff --check` limpio; sin commit/push, sin
  secretos, sin `staging/`/`dev.env`.
AJUSTES NUEVOS [P, PLAN NO IMPLEMENTADO; nada implementado aquí]:
  1. Detección por evento como primaria: avisos `runOnOnline` /
     `runOnOffline` del repetidor (docs oficiales MediaMTX v1.21
     [D: citado en la tarea que autoriza; consulta directa de la
     doc pendiente [P]]) + pregunta cada 3 s de respaldo +
     confirmación 30 s seguidos. Matiza v2 ("sin timers" [D
     histórico]): el evento manda, el poll solo respalda.
  2. Algoritmo adaptativo 3→2→1 con votación 2-de-3 e histéresis
     [P]: rápido para pre-crear (~6 s), prudente 30 s para terminar.
     Base: detector de parpadeo Nagios + histéresis [D: citado en
     la tarea]; Phi-accrual evaluado y DESCARTADO por
     sobredimensionado para localhost [D: citado en la tarea].
     Sustituye el antirrebote fijo 60 s de v2 [D histórico].
  3. Check global "no volver a mostrar" del aviso de audiencia en
     Avanzado, defecto OFF, persistente en plaintext con texto de
     riesgo F-093 [P]. Precisa v2 (check global en Avanzado
     defecto OFF [D histórico]): SOLO salta ese aviso; F-093
     (`privacy` ilegible + badge "Solo yo" pese a EVERYONE 200)
     sigue vigente como riesgo a comunicar [D: FINDINGS.md].
  4. Contador ~mm:ss junto al semáforo [P]: estimación local, con
     `~`, informativo; no bloquea ni reintenta solo. Precisa v2
     (contador cooldown [D histórico]).
  5. Semáforo sin texto (rojo/ruedita/triángulo) + textos internos
     en lenguaje de persona [P]: continúa dos-directos (lista de
     ~30 textos con jerga a reescribir + principio "el usuario
     jamás espera ni decide nada técnico" [D]); la lista de ~30
     textos queda pendiente de reescritura [P].
  6. Auto-End SOLO LIVE auto-iniciado (flanco + gracia 5 min +
     confirmación Graph fresca, VOD siempre) + pre-creación del
     siguiente al detectarse la caída + cola diferida de un solo
     disparo con guarda de ID [P]. Cambia dos-directos [D
     histórico]: pre-create (antes NO existe) pasa a planificado
     al detectarse la caída; cola diferida (antes NO existe) pasa
     a planificada de un solo disparo con guarda de ID; auto-End
     (antes NO existe) pasa a planificado con las guardas citadas.
ALCANCE [P]: todo lo anterior respeta la excepción acotada de
  ADR-016 (relay local + forward `:9997`, destino operador fijo
  `rtmp://127.0.0.1:1935/live`); sin multistream general, sin tocar
  outputs/servicios/escenas/fuentes, sin keys en repo.
ESTADO [C contra BACKLOG]: T-078 SIGUE en investigación, sin cambio
  de estado en esta subtarea (canónico BACKLOG; esta entrada no cambia
  estados). T-077 intacta.
FINAL: ajuste v3 consolidado como [P]; redacción UX + implementación
  + E2E operador real (dos directos seguidos) + métricas F-099 +
  audiencia F-093 pendientes.
```

## Validación "dos directos seguidos" (2026-10-26 — VALIDACIÓN/PLAN, NO evidencia de implementación; T-078 sin cambio de estado)

> ESTE BLOQUE ES VALIDACIÓN/PLAN, NO EVIDENCIA: nada de lo descrito
> aquí está implementado ni verificado en esta sesión documental. Solo
> edición de docs (`VALIDATION.md`, `STATE.md`, `PROJECT_CONTEXT.md`,
> `BACKLOG.md` nota sin cambio de estado); sin código, sin commit/
> push, sin secretos, sin `staging/`/`dev.env`. No reescribe historia:
> el plan v2 anterior queda intacto abajo.

```text
FUENTE [D]: informes de trazabilidad obs-plugin + mensajes ux-ui de
  esta sesión, comunicados en la tarea que autoriza (2026-10-26; no
  re-ejecutados aquí). Veredictos comunicados [D]:
  NO existe pre-create al apagarse; NO existe auto-End; NO existe
  cola diferida; SÍ existen verify/refire independientes; Apply SÍ
  blinda la carrera; presupuestos de un solo uso; lista de ~30 textos
  con jerga a reescribir; textos propuestos en inglés; principio "el
  usuario jamás espera ni decide nada técnico".
BASE [C, esta subtarea documental]: rama
  `feat/facebook-status-port-t077-t078` (HEAD `e4ae8ec`);
  `git status --porcelain` al inicio lista 4 ficheros documentales
  modificados sin commit (plan v2 en curso de otra edición paralela);
  en esta subtarea solo edición de docs + `git diff --check`, sin
  commit/push, sin secretos, sin `staging/`/`dev.env`.
ESCENARIO [P, validado en papel, NO implementado]: dos directos
  seguidos sobre el mismo LiveVideo propio (segundo marcó LIVE sin
  crear nada nuevo en el caso trazado).
VEREDICTOS [D, trazabilidad comunicada; implementación [P]]:
  1. Pre-create al apagarse: NO existe (nada se crea al apagar) [D].
  2. Auto-End: NO existe (ningún End automático en el caso trazado) [D].
  3. Cola diferida: NO existe (nada queda encolado para después) [D].
  4. Verify/refire independientes: SÍ existen como operaciones
     separadas [D]; su E2E real queda pendiente [P].
  5. Apply blinda la carrera: SÍ (Apply cubre la ventana de carrera
     del escenario en el diseño validado) [D]; E2E pendiente [P].
  6. Presupuestos: de un solo uso [D]; sin reutilización entre
     directos [P por implementar/verificar].
UX [D, mensajes ux-ui comunicados; implementación [P]]: lista de ~30
  textos con jerga a reescribir + textos propuestos en inglés;
  principio rector: "el usuario jamás espera ni decide nada técnico".
  La redacción final en el dock queda pendiente [P].
ALCANCE [P]: todo lo anterior respeta la excepción acotada de
  ADR-016 (relay local + forward `:9997`, destino operador fijo
  `rtmp://127.0.0.1:1935/live`); sin multistream general, sin tocar
  outputs/servicios/escenas/fuentes, sin keys en repo.
ESTADO [C contra BACKLOG]: T-078 SIGUE en investigación, sin cambio
  de estado en esta subtarea (canónico BACKLOG; esta entrada no cambia
  estados). T-077 intacta.
FINAL: validación en papel consolidada como [P]; redacción UX +
  implementación + E2E operador (dos directos seguidos reales) +
  métricas F-099 + audiencia F-093 pendientes.
```

## Plan consolidado v2 Facebook-automático (2026-10-26 — PLAN NO IMPLEMENTADO; T-078 sin cambio de estado)

> ESTE BLOQUE ES PLAN, NO EVIDENCIA: nada de lo descrito aquí está
> implementado ni verificado en esta sesión documental. Solo edición
> de docs (`VALIDATION.md`, `STATE.md`, `PROJECT_CONTEXT.md`,
> `BACKLOG.md` nota sin cambio de estado); sin código, sin commit/
> push, sin secretos, sin `staging/`/`dev.env`.

```text
FUENTE [D]: ADR-016 (Fase 1 documental: relay local MediaMTX,
  excepción acotada AGENTS.md §§23/51) + E2E lab T-078 (forward +
  caliente PASS, 2 emisiones LIVE+DIRECTO sin tocar Aitum) +
  simplificación Fase A+B (auto-prepare + vista; build OK, selfcheck
  338/0 [D delegada de la sesión implementadora, no re-ejecutada
  aquí]) + fix findBinary (data path primero; build OK, selfcheck
  322/0 con relay-find-data 7/7 [D delegada]) + relay Fases 2–6 port
  (build OK, selfcheck 315/0 [D delegada]) + PRs #73/#75/#76/#77 e
  instaladores [D: citados en la tarea que autoriza, 2026-10-26; sin
  verificación remota en esta sesión — github MCP sin autenticación
  en este entorno, no se afirma acceso remoto].
BASE [C, esta sesión documental]: rama
  `feat/facebook-status-port-t077-t078` (HEAD `e4ae8ec`);
  `git status --porcelain` vacío al inicio y al final; solo
  `git diff --check` como verificación (limpio antes y después de
  editar); sin commit/push.
DECISIONES DEL DUEÑO PENDIENTES DE IMPLEMENTAR [P, origen: tarea que
  autoriza, 2026-10-26; nada implementado aquí]:
  1. Arranque exacto por evento (E1/E2/E3, sin timers) [P].
  2. Go live automático (señal proxy relay+forward; audiencia: check
     global en Avanzado, defecto OFF) [P].
  3. Semáforo sin texto (rojo/ruedita/triángulo) + contador ~mm:ss
     de cooldown [P].
  4. Auto-End por pérdida de señal (solo LIVE auto-iniciado;
     flanco+antirrebote 60s + gracia 5min + confirmación Graph
     fresca; VOD siempre) [P].
  5. Auto-prepare tras End (create nuevo + borra anterior
     UNPUBLISHED; fallback reutilizar) [P].
ALCANCE [P]: todo lo anterior respeta la excepción acotada de
  ADR-016 (relay local + forward `:9997`, destino operador fijo
  `rtmp://127.0.0.1:1935/live`); sin multistream general, sin tocar
  outputs/servicios/escenas/fuentes, sin keys en repo.
ESTADO [C contra BACKLOG]: T-078 SIGUE en investigación, sin cambio
  de estado en esta tarea (canónico BACKLOG; esta entrada no cambia
  estados). T-077 intacta.
FINAL: plan v2 consolidado como [P]; implementación + E2E operador +
  métricas F-099 + audiencia F-093 pendientes.
```

## Simplificación Facebook Fase A (auto-prepare) + Fase B (vista) (2026-10-26, sin commit; T-078 sin cambio de estado)

```text
FUENTE [D, delegada de la sesión implementadora; no re-ejecutada en
  esta sesión documental]: Fase A auto-prepare + Fase B vista.
  Build OK; selfcheck 338/0; `git diff --check` y secret-scan limpios.
  Aquí solo edición de docs + `git diff --check`; sin commit/push,
  sin secretos, sin staging/dev.env.
FASE A [D]: auto-prepare (auto-relay si FB conectado; valida ID:
  crea nuevo + borra anterior UNPUBLISHED; fallback reutilizar con
  aviso honesto; Apply con verificación silenciosa; señal del emisor
  vía API local; auto-prepare tras End con cooldown).
FASE B [D]: vista (línea "Facebook listo"; Avanzado colapsado;
  botones condicionales; indicador de señal).
ESTÁTICO [C, esta sesión documental, sin ejecutar]: `git status
  --porcelain` lista ficheros `src/` modificados sin commit en la rama
  `feat/facebook-status-port-t077-t078` (HEAD `01ea886`); sin commit/push.
ESTADO [C contra BACKLOG]: T-078 SIGUE en investigación, sin cambio
  de estado en esta tarea (canónico BACKLOG; esta entrada no cambia
  estados).
PENDIENTE [P]: E2E operador + capturas visuales + instalador con
  relay+fix.
FINAL: simplificación documentada; E2E de aceptación pendiente.
```

## Fix relay findBinary → data path primero (2026-10-26, sin commit; T-078 sin cambio de estado)

```text
FUENTE [D, delegada de la sesión implementadora; no re-ejecutada en
  esta sesión documental]: fix en `src/relay_manager.cpp::findBinary`
  (+ `src/relay.h`/`src/relay.cpp`/`src/relay_manager.h` + checks en
  `src/metadata_selfcheck.cpp`): buscaba el binario junto al programa
  pero el instalador coloca el relay en el data path del módulo; ahora
  busca primero en `obs_get_module_data_path()/relay/` (causa del
  "binary missing" del operador con instalador nuevo). Build OK;
  selfcheck 322/0 con relay-find-data 7/7; `git diff --check` y
  secret-scan limpios. Aquí solo edición de docs + `git diff --check`;
  sin commit/push, sin secretos, sin staging/dev.env.
ESTÁTICO [C, esta sesión documental, sin ejecutar]: `src/relay_manager.cpp:79`
  usa `obs_get_module_data_path`; `src/metadata_selfcheck.cpp`
  contiene 7 checks `relay-find-data-*`; `git status --porcelain`
  lista 5 ficheros `src/` modificados sin commit (relay.cpp/.h,
  relay_manager.cpp/.h, metadata_selfcheck.cpp).
ALCANCE [D]: solo orden de búsqueda del binario (data path primero,
  resto como fallback); sin cambios de alcance AGENTS.md §§23/51
  (ADR-016 intacto).
ESTADO [C contra BACKLOG]: T-078 SIGUE en investigación, sin cambio
  de estado en esta tarea (canónico BACKLOG; esta entrada no cambia
  estados).
PENDIENTE [P]: operador reinstala/reabre OBS → Start relay → E2E
  instalar→relay→forward→GoLive→LIVE + métricas F-099 + audiencia
  F-093.
FINAL: fix documentado; E2E de aceptación pendiente.
```

## Relay Fases 2–6 port lab→plugin (2026-10-26, sin commit; T-078 sin cambio de estado)

```text
FUENTE [D, delegada de la sesión implementadora; no re-ejecutada en
  esta sesión documental]: `src/relay.h`/`src/relay.cpp` +
  `src/relay_manager.*` + vista en el dock + `tools/package.ps1` con
  relay/MIT; build RelWithDebInfo OK; selfcheck 315 PASS/0 FAIL;
  `git diff --check` 0; secret-scan limpio; `package.ps1 -WhatIf` OK.
  Aquí solo edición de docs + `git diff --check`; sin commit/push,
  sin secretos, sin staging/dev.env.
ALCANCE FASES [D]: relay local MediaMTX integrado (instalación +
  proceso hijo + forward vía Control API `:9997` hacia la ingesta del
  LiveVideo propio; destino operador fijo `rtmp://127.0.0.1:1935/live`)
  + aviso MIT en el empaquetado. Excepción acotada §§23/51 (ADR-016):
  no automatiza Aitum/OBS, no es multistream general.
ESTADO [C contra BACKLOG]: T-078 SIGUE en investigación, sin cambio
  de estado en esta tarea (canónico BACKLOG; esta entrada no cambia
  estados). T-077 intacta en investigación.
PENDIENTE [P]: E2E operador instalar→relay→forward→GoLive→LIVE +
  métricas latencia/CPU/recovery (F-099) + audiencia en producción
  (F-093) + build E2E del instalador con descarga real de MediaMTX +
  capturas visuales.
FINAL: Fases 2–6 documentadas; E2E de aceptación pendiente.
```

## Port lab→plugin Fase D (2026-10-26, sin commit; T-077/T-078 sin cambio de estado)

```text
FUENTE [D, delegada de la sesión implementadora; no re-ejecutada en
  esta sesión documental]: backend `334 passed / 11 skipped`
  (skips PG-sin-servidor por diseño) + selfcheck `287/0 DLL OK` +
  `git diff --check` limpio + secret-scan limpio + 5 ficheros `src/`
  modificados sin commit (`src/metadata.cpp`, `src/metadata.h`,
  `src/metadata_dock.cpp`, `src/metadata_dock.h`,
  `src/metadata_selfcheck.cpp`) [C: `git status --porcelain` en esta
  sesión lista exactamente esos 5 modificados; sin commit/push].
ALCANCE PORTADO [D]: Status sin privacy en ambos modos + fallback;
  persistent opt-OFF en ambos modos + mapeo
  `persistent-key-not-allowed`; mensajes F-097 (cooldown ~3 min) y
  F-098 (auto-VOD) en el dock.
NO PORTADO [D]: formato lab base+clave (`#`), relay/scripts lab
  (MediaMTX, forward, re-fijado en caliente) — quedan en
  `docs/MEDIAMTX-RELAY-FLOW.md` + `lab/**`, fuera del plugin.
ESTADOS [C contra BACKLOG]: T-077 y T-078 siguen en investigación,
  sin cambio de estado en esta tarea (canónico BACKLOG; esta entrada
  no cambia estados).
PENDIENTE [P]: E2E del operador del port + resolución del conflicto
  de alcance AGENTS.md §§23/51 antes de implementar producto.
FINAL: Fase D documentada; E2E + alcance pendientes.
```

## E2E operador single-PC Managed (2026-10-26, autorización [D usuario: confirmó en prod los 3 puntos con "OK OK OK"])

```text
FUENTE:    chat del operador 2026-10-26 [D] (no re-ejecutado en esta
           sesión documental; aquí solo edición de docs +
           `git diff --check`; sin commit/push, sin secretos, sin
           staging/dev.env).
1. SESIÓN: reapertura con sesión válida → directo al panel
           (sesión persistente T-086 OK en vivo).
2. LOGOUT: logout → pantalla entrar + plataformas en gris neutro
           (sin rojos) (routing/estados T-086 OK en vivo).
3. REGLA:  logout desconectó las 4 plataformas (regla T-085
           funcionando en vivo).
PENDIENTE: T-084 multi-PC sigue pendiente [P] (requiere 2 máquinas);
           OAuth real con Apply de título real sigue pendiente del
           operador [P].
FINAL: E2E single-PC PASS operador; multi-PC + OAuth real pendientes.
```

## T-086 Sesión Managed persistente + routing arranque + estados tarjeta (2026-10-26, decisión [D usuario])

```text
DECISIÓN:  usuario 2026-10-26 [D]: SUSTITUYE spec §5 memoria-solo —
           la sesión de usuario Managed persiste cifrada en DPAPI
           (`managedUser`: token+expiry+username, fail-closed ante
           mismatch de `backendBaseUrl`) hasta logout explícito /
           401 / expiración; abrir OBS con sesión válida → directo
           al panel; login solo si venció o hubo logout.
CÓDIGO:    obs-plugin (persistencia `managedUser` DPAPI + revalidación
           al arrancar) + routing ux-ui (estados gris/rojo de tarjeta)
           [C: cambios sin commitear en este checkout —
           `src/secure_store.*`, `src/backend_auth.*`,
           `src/metadata_dock.cpp`; checks `startup-*` / `muser-*` /
           `card-*` / `f6-*` / `f7-*` / `f8-*` presentes en
           `src/metadata_selfcheck.cpp`].
EVIDENCIA: build + selfcheck verdes [D: sesión implementadora, no
           re-ejecutados en esta sesión documental]: selfcheck
           `startup-*` / `muser-*` / `card-*` / `f6-f8` PASS, DLL OK.
PENDIENTE: integrar (commit/PR), recompilar instalador, E2E operador
           con capturas [P].
FINAL: T-086 HECHA documental; E2E operador pendiente.
```

## Deploy rev-managed-users-v1 + smoke prod Managed v1 (2026-10-26, autorización [D usuario: cerrar el loop deploy+verificación])

```text
FUENTE:    reporte release-tooling + dictamen gcp en vivo 2026-10-26 [D]
             (no re-ejecutados en esta sesión documental; aquí solo
             edición de docs + `git diff --check`).
DEPLOY:    tag `rev-managed-users-v1` → revisión Cloud Run
             `obs-stream-metadata-service-00039-7jl` al 100% [D];
             `/health` ok, `/version` env=production, `/ready` true [D].
MIGRACIÓN: 003 confirmada en prod: `schema_migrations` = 001/002/003
             [D: consulta MCP Neon read-only del orquestador a rama
             `production` del proyecto `obs-stream-metadata`].
RED SEG.:  snapshot manual `snap-fancy-lab-a5r16mex` de rama
             production, 2026-10-26T02:11:27Z [D]; pg_dump del operador
             queda opcional.
SMOKE:     prod con usuario sintético desechable [D:
             backend-integrations, curl.exe]: signup 201 → login 200 →
             me 200 → status sin sesión 401 → logout 200 → me 401 →
             erase 200 (users:1, installations:1, sessions:3) → me 401;
             limpieza confirmada, repo intacto.
HUÉRFANAS: quedan 3 instalaciones huérfanas vacías (solo bootstrap)
             de la depuración, sin datos [D]; las purga el job diario o
             purga operativa [P].
PENDIENTE: operador humano (navegador/cuentas reales): OAuth real por
             plataforma + Apply de título real + E2E dos PCs (T-084
             sigue pendiente, BACKLOG canónico) [P].
FINAL: loop deploy+verificación cerrado salvo E2E operador (T-084).
```

## T-078 E2E relay MediaMTX local → LiveVideo propio (2026-10-26, autorizado 2026-10-26)

```text
FUENTE:    informes backend + capturas del operador, 2026-10-26 [D]
             (evidencia de sesión aportada a esta tarea documental;
             no re-ejecutada aquí; sin commit/push/PR en esta tarea).
RELAY:     MediaMTX v1.21.3 local (:1935/:9997, checksum oficial OK,
             licencia MIT — aviso registrado, empaquetado pendiente) [D].
FORMATO:   ganador base+'#'+clave; URL íntegra da EOF/0 bytes (ver F-096).
INGESTA:   OBS/Aitum→relay verificado (H264 1080p+AAC, inbound creciente,
             0 errores) [D].
FORWARD:   PASS en caliente por PATCH /v3/config/paths/patch/live SIN
             reinicio; 2 emisiones (IDs 28704775482518154 y
             28704809952514707) con forward→GoLive→LIVE+DIRECTO visible
             con movimiento SIN tocar Aitum entre ellas [D].
RATELIMIT: re-handshakes RTMPS seguidos limitados por Meta (cooldown
             ~3 min) (ver F-097) [D].
VOD:       auto-VOD al retirar señal (ver F-098) [D].
AUDIENCIA: PENDIENTE producción — sigue "Solo yo" pese a EVERYONE 200,
             igual que F-093 [P].
MÉTRICAS:  ~57-59 MB WS + códec/inbound/0-errores (ver F-099);
             latencia/CPU/recovery numéricos pendientes [P].
CIERRE:    End→LIVE_STOPPED, forward [], VOD del 2º conservado a petición,
             1er objeto borrado antes [D].
SECRETOS:  IDs de LiveVideo sí (no son secretos); tokens/claves/URLs de
             ingesta jamás registrados.
FINAL: E2E relay T-078 documentado; aceptación parcialmente cubierta
       (forward PASS + caliente PASS + licencia identificada; métricas
       latencia/CPU/recovery pendientes) — T-078 sigue en investigación.
       T-077 intacta. Detalle en F-096–F-099.
```

## T-077 E2E lab Facebook LiveVideo propio → ingesta → LIVE (2026-10-26, lab throwaway `lab/facebook`, autorizado)

```text
FUENTE:    informes backend + capturas del operador, 2026-10-26 [D]
             (evidencia de sesión aportada a esta tarea documental;
             no re-ejecutada aquí; sin commit/push/PR en esta tarea).
CONNECT:   PASS PKCE Test App (vía Test App; app Live bloquea localhost
             con Enforce HTTPS — ver F-094).
CREATE:    PASS ×5 UNPUBLISHED EVERYONE con IDs registrados:
             28703591109303258, 28703717372623965,
             28703720135957022, 28703757945953241, 28703813379281031.
READ-BACK: PASS título/descripción/status idénticos tras crear.
UPDATE:    PASS update + read-back idéntico.
PRIVACY:   PENDIENTE producción — privacy NO legible en GET (Meta 400)
             + badge efectivo "Solo yo" pese a EVERYONE 200 al crear
             (ver F-093); audiencia real en producción sin confirmar [P].
PERSIST:   VEREDICTO NO url única — ingestas distintas por create incluso
             con `persistent_stream_key_status=ENABLE` (ver F-092) [D].
GOLIVE:    PASS ingesta manual Aitum → GoLive 200 → LIVE + DIRECTO con
             movimiento visible en perfil (operador).
TEARDOWN:  PASS End → LIVE_STOPPED + DELETE ×2 con 100/33.
LAB-BUG:   Status con comas corregido en el lab (ver F-095).
ALCANCE:   T-077 sigue en investigación: su aceptación incluye "resolver
             el conflicto de alcance AGENTS.md §23/§51 antes de implementar"
             — pendiente; sin port al plugin en esta sesión. BACKLOG
             canónico: estado sin cambios.
SECRETOS:  IDs de LiveVideo sí (no son secretos); tokens/claves/URLs de
             ingesta jamás registrados.
FINAL: E2E lab T-077 documentado; pendientes: audiencia en producción,
       port al plugin, resolución de alcance §§23/51.
```

## T-076 Facebook gris + YouTube default listo (2026-10-24, PR #55 + #56)

```text
CÓDIGO:    PASS (plan docs/superpowers/plans/2026-10-24-fb-gris-yt-default.md
             ejecutado: Apply filtra por `platformUsable`, orden backend
             activo→próximos por fecha→terminados, selects index 0 + tooltip)
BACKEND:   PASS 250 OK documentado (10 skips PG por diseño; +18
             test_metadata_youtube por el orden listo)
E2E:       PASS OPERADOR (combo elige el activo/próximo, sin error FB
             sin conectar) [D]
DEPLOY:    rev-00036-cjj al 100% [D: BACKLOG + agente GCP 2026-10-24]
FINAL: T-076 HECHA (BACKLOG #56). Sin re-ejecución en esta sesión
       documental (no se corrieron tests/build aquí).
```

## T-075 Auto-reconnect cómodo Managed (2026-10-24, PR #54)

```text
CÓDIGO:    PASS (plan docs/superpowers/plans/2026-10-24-auto-reconnect-comodo.md:
             mensajes desglosados + revalidación silenciosa al arrancar +
             1 reintento acotado antes de pedir Connect; `backend/db.py`
             + `test_db_pool_concurrency.py` + `src/metadata_dock.*`)
BACKEND:   PASS 250 OK documentado + pool fix rev-00035 [D: BACKLOG]
E2E:       PASS OPERADOR (Connect/Apply/restart) [D: BACKLOG]
FINAL: T-075 HECHA (BACKLOG #54). Sin re-ejecución en esta sesión
       documental.
```

## T-073 FB-4 UI y cierre (2026-09-23, rama `feat/T-073-fb-4-cierre`)

```text
BACKEND:   PASS 249 OK (10 skips PG por diseño; 31 test_facebook.py:
             26 heredados + 5 Lifecycle nuevos status/create/end/delete/
             unknown-op; F-080 subcode 33 → PROVIDER_REJECTED)
SELFCHECK: PASS SELFCHECK OK (fb-33-notfound/fb-100-badrequest/
             fb-msg-404-dock/fb-warn-60d-100-10/fb-state-*/fblive-*
             nuevos; regresión TW/YT/KK/FB intacta)
BUILD:     PASS preset windows-x64 RelWithDebInfo, 0 errores
             (obs-stream-metadata.dll + metadata-selfcheck.exe)
SCAN:      PASS patrón CI (0 hits valores; constructores TokenPair
             preexistentes no son valores)
PACKAGE:   PASS local 19e8bcd8… + commercial b00baabe… (F-085 +
             F-086 incluidos) contra prod
             https://obs-stream-metadata-service-364043334054.us-east5.run.app
             (payload solo DLL+ini+tls; dist/ ignorada)
E2E:       PENDING OPERADOR (código listo; sin consentimiento humano en
             esta sesión; go-live exige aprobación explícita + doble llave):
             1. Connect perfil publish_video (Independent BYO + Managed
                backend) → `Connected as`.
             2. Create live video (dock) → ID persistido + restart lo
                restaura en el combo.
             3. Status → indicador Preview/LIVE/Ended.
             4. Apply título+desc → read-back idéntico.
             5. Go-live SOLO con aprobación (objeto dock) → LIVE visible.
             6. End → VOD + Delete → limpieza + ID retirado del store.
             7. Restart → sesión + ID restaurados; Disconnect → reconexión
                exigida (ID conservado).
             8. Warnings: 1363120 (60d) / 1363144 (100) / 10 / 100-33
                (no-gestionable → crear desde dock).
             E2E Managed 2026-09-23 (operador, commercial FB-4):
                Create PASS (ID 28278448785150828 persistido) + Status
                `● LIVE (LIVE)` PASS + Apply `Facebook ✓ updated` +
                read-back Explorer idéntico (title/desc `PRUEBA FB4`,
                status LIVE) PASS. H1 confirmada otra vez: Live Producer
                web muestra otro objeto (1652370182887354) — el read-back
                vale solo vía API/Explorer, no vía web.
                End `Facebook end OK` PASS + Delete `Facebook delete OK`
                PASS.                 Hallazgo F-085 (indicador no auto-refrescaba tras
                on/off) → fix con status encadenado. Hallazgo F-086
                (botones sin sentido visibles sin conexión) → fix
                visibilidad por `platformUsable` en las 4 tarjetas.
                Commercial vigente `b00baabe…`, reinstalar.
                Restart + Disconnect pendientes.
             Criterio de parada: sonda en rojo → parar, FINDINGS, preguntar.
             Page/App Review (D12/D14) fuera de E2E perfil.
FINAL: T-073 HECHA — E2E operador completo (Managed perfil):
       Connect, create+ID, status LIVE, Apply+read-back Explorer idéntico,
       end, delete, restart con sesión restaurada, Disconnect con solo
       Connect visible (F-086 verificado: desconectado solo Connect,
       conectado Disconnect+controles). Negativas vivas no ejercitadas
       (1363120/1363144/190/429): cubiertas offline en selfcheck
       (`fb-warn-*`, `fb-190-expired`, `fb-429-limited`) + backend
       (`expired_user_token`, `429_rate_limited`), como en T-071/T-072.
       Merge vía PR con template + CI verde + squash (pendiente
       autorización commit/push/PR). Cero FB-5 en esta sesión.
```

## T-072 FB-3 Managed (2026-09-23, rama `feat/T-072-fb-3-managed`)

```text
BACKEND:   PASS 244 OK (10 skips PG por diseño; 26 nuevos test_facebook.py,
             TDD rojo ImportError → verde con 3 fixes wire-format F-077)
SCAN:      PASS (patrón CI GNU grep sobre ficheros FB-3: limpio; sin valores)
BUILD:     PASS (preset windows-x64 incremental, RelWithDebInfo;
             obs-stream-metadata.dll + metadata-selfcheck.exe, 0 errores)
SELFCHECK: PASS exit 0, SELFCHECK OK (msnap-fb-write/msnap-fb-restore nuevos;
             regresión TW/YT/KK/FB-2 intacta; Qt bin en PATH por 0xC0000135)
PACKAGE:   PASS commercial contra prod (package.ps1: configure+build+install+
             makensis; dist/obs-stream-metadata-0.1.0-windows-x64-commercial.exe
             257124 bytes, sha256 92ecf154…f147f4; payload solo DLL+ini+tls)
FIX-E2E:   E2E halló bloqueo rancio en el botón Connect (F-078):
             `onConnectFacebook` retenía el mensaje FB-2 en modo Managed.
             Fix: rutea a `onConnectManaged` (espejo YouTube); cuerdas
             rancias a cero en fuente; rebuild 0 errores + selfcheck OK;
             commercial reempaquetado (sha256 `cb86f88b…215afee8`;
             instala ESTE, no el anterior). Backend intacto.
FIX-SCOPE: E2E mostró `Invalid Scopes: pages_manage_posts` (F-079 = F-072
             en Managed: `manage-streams` es Consumer). Fix: Managed pide
             solo `publish_video` (perfil D9); suite 244 OK; redeploy rev
             `00032-7n9` en verde; sonda prod confirma `scope=publish_video`
             (instalación revocada). Sin reinstalar plugin (solo backend).
ROUTING:   PASS (ruta genérica /metadata/facebook + /connect/facebook/*,
             sin cambios de routing; Independent intacto)
DEPLOY:    PASS rev-00032-7n9 al 100% (imagen con adapter FB perfil-only;
             /health ok + /version production + /ready true; PROVIDERS
             youtube,kick,twitch,facebook; rev-00029-9h8 previa superada)
ENABLE:    PASS rev-00031-crz al 100% (secretos FB_APP_ID/SECRET v1 desde
             ficheros del operador + IAM Secret Accessor al SA dedicado +
             mounts fb-app-id/fb-app-secret + SECRET_DIRS + PROVIDERS
             youtube,kick,twitch,facebook; arranque sin fail-fast)
START:     PASS sonda prod 10/10 (bootstrap→sesión→POST /connect/facebook
             200: dialog v26.0, publish_video, sin groups/email, PKCE S256,
             redirect prod /connect/facebook/callback, sin secret en URL;
             instalación de sonda revocada, cero filas)
E2E:       Connect PASS operador 2026-09-23 (consent perfil publish_video →
             callback connected id 28266677726327934 `Connected as` en dock).
             Update+read-back PASS (objeto API 28277653911896982 creado en
             Explorer → dock Apply título+desc → `Facebook ✓ updated` →
             read-back idéntico + status LIVE). Restart PASS (sesión
             restaurada, Apply de nuevo OK). Disconnect PASS
             (`disconnected`, reconexión exigida). Limpieza PASS (DELETE
             objeto prueba). Negativas vivas no ejercitadas (190/429):
             cubiertas en tests (`expired_user_token`, `429_rate_limited`)
             + camino SESSION_EXPIRED→reconnect (D8).
FINAL: T-072 HECHA — E2E operador completo. Merge vía PR con template +
       CI verde + squash (pendiente autorización commit/push/PR).
       Cero FB-4 en esta sesión.
```

## T-071 FB-2 Independent (2026-09-22, rama `feat/T-071-fb-2-independent`)

```text
BUILD:     PASS (preset windows-x64, RelWithDebInfo; obs-stream-metadata.dll + metadata-selfcheck.exe, 0 errores)
SELFCHECK: PASS (exit 0, 0 FAIL: fb-title-max/name/desc/254/255/payload/classify 190-1363120-1363144-10-613-429-5xx/scopes/auth-url + fb-store-* DPAPI; regresion TW/YT/KK intacta)
BACKEND:   PASS 218 OK (10 skips PG por diseno; sin cambios de backend, sin regresion)
SECRET:    PASS (motor CI GNU grep: producto limpio; tracked limpio; hits solo en .deps/ ignorado por .gitignore, no accionable)
E2E:       PASS OPERADOR 2026-09-22:
               Connect PKCE OK (`Connected as Horangel Millan`, callback
               localhost:3001 + Authorized; diálogo muestra permiso
               "Publicar un vídeo en tu biografía" = publish_video) +
               update+read-back PASS (objeto 28266780719650968 creado en
               Explorer → dock Apply título+desc+privacy → Explorer
               read-back idéntico `FB-2 E2E/FB-2 E2E/LIVE`) +
               restart PASS (sesión DPAPI restaura sola) +
               Disconnect PASS (`disconnected`, revoke DELETE /permissions,
               reconexión exigida) + limpieza PASS (end_live_video →
               LIVE_STOPPED, DELETE ×2 → true/true).
               Negativas vivas no ejercitadas (401/429): cubiertas offline
               en selfcheck (`fb-401-auth/fb-190-expired/fb-429-limited`)
               + camino de código (sin refresh clásico, §D8); long-lived
               con secret no probado en vivo (secret vacío en E2E).
               Page diferida (F-072); listado descartado (F-074/F-075).
FINAL: T-071 PASS — mergeable con PR (sin FB-3: cero backend/Managed FB).
FINAL: T-071 PARCIAL — no avanzar a FB-3 ni mergear sin E2E del operador
```

E2E operador (perfil; Page real pendiente de Page 100+, D9):

```text
Pre:   app `manage-streams` (Consumer) en Development + rol propio;
       registrar http://localhost:3001/fb-cb en Valid OAuth Redirect URIs;
       cuenta real solo via perfil (60+ dias).
1. Connect:    Facebook card → App ID (+ secret opcional) → Connect →
               consent (publish_video; pages_* diferidas por F-072 en apps
               Consumer) → callback localhost:3001 OK →
               `Connected as <nombre>` (long-lived best-effort en log).
2. Lista:      Refresh Facebook videos → targets (Profile; Pages diferidas
               F-072) + videos. NOTA F-074: el edge puede devolver 0 aunque
               existan objetos (legibles directo por ID): el combo acepta
               pegar el ID a mano; la lista es best-effort.
3. Apply:      seleccionar target + video, titulo 1-254 + desc → Apply →
               `Facebook ✓ updated`; verificar titulo/desc en Live Producer o
               GET /{id}?fields=id,title,description,status.
4. Restart:    reiniciar OBS → sesion restaurada (DPAPI) → Apply de nuevo OK.
5. Disconnect: Disconnect → DELETE /me/permissions → `disconnected`;
               reconexion exigida despues.
Negativas: titulo vacio/255 bloquea local; 190/401 → Needs reconnection;
           1363120/1363144 → mensajes elegibilidad; revoke en web → 401.
Criterio de parada: sonda en rojo → parar, registrar en FINDINGS, preguntar.
```

1. **Estática:** Markdown/CI Phase 0, revisión diff, secret-scan. Sin código aún.
2. **Compilación:** Phase 1+ con toolchain fijado (VS2022/CMake/Qt6/OBS SDK). Registrar comando + salida.
3. **Tests:** unitarios donde aplique; negativos obligatorios `AGENTS.md` §40.
4. **Integración:** dock carga en OBS 32.2.2, sin congelar UI (threading §26), `204`/`401`/`429` manejados (§28).
5. **Manual:** checklist `AGENTS.md` §39 por proveedor + §50 (definición de terminado).
6. **APIs reales:** solo cuando sea necesario; credenciales reales nunca en repo/logs; sin polling (cuota YouTube §11.9).

Toda afirmación "funciona" requiere evidencia (comando + salida). "Probablemente funciona" no es válido.

## T-068 Apply Managed Kick (2026-09-19)

```text
BUILD:     PASS (preset windows-x64, RelWithDebInfo, 0 errores; DLL con startManagedKickApply)
SELFCHECK: PASS 125/125, 0 FAIL · BACKEND: PASS 215/215 (10 skips PG por diseño; incluye MetadataKickTest + HttpMetadataKickTest nuevos)
DEPLOY:    PASS rev-00027-jb9 (imagen master-bff522f-dirty, /health ok + /version production + /ready true) vía nuevo tools/deploy.ps1
PACKAGE:   PASS commercial 49a3ac73… (prod verificado vivo) + local 71fda3e5… (loopback; distinto hash = sin contaminación)
E2E:       PASS operador (install manual commercial → Managed Connect Kick → Apply título → ✓ + read-back en directo, F-023)
SECRET:    PASS (patrón CI limpio en código+tests+scripts; sin tokens en respuestas/logs)
TRACKING:  PASS (ruta /metadata/kick genérica, sin cambios de routing; Independent intacto)
FINAL: T-068 PASS (cierra F-063)
```

## T-062 cierre de facto por T-063 (2026-09-19)

```text
IMPLEMENTACIÓN: DONE en PR #32 (commit 18e236a: Apply Twitch server-side PATCH Helix,
                diagnóstico 2.1-C.1, mapper robusto, FASES-COMERCIAL.md + filas T-062…T-066)
E2E HUMANO:     SATISFECHO DE FACTO por T-063 (sonda prod Apply YT/Kick/Twitch OK 2026-09-17,
                con efecto real; precedente T-034/T-061)
FINAL: T-062 HECHA (de facto; sin E2E dedicado adicional)
```

## T-067 carpeta dist/ + package.ps1 (2026-09-19)

```text
BUILD:     PASS (preset windows-x64, RelWithDebInfo, 0 errores; dll + selfcheck vigentes)
PACKAGE:   PASS (tools/package.ps1 -Flavor local → dist/*-local.exe + .sha256, hash coincide)
INSTALL:   PASS (desde dist/, OBS normal: carga en log; uninstall retira solo payload)
SECRET:    PASS (patrón CI limpio; dist/ ignorada; sin dev.env/productivos en el sabor)
TRACKING:  PASS (git ls-files sin .exe; dist/ con check-ignore)
FINAL: T-067 PASS
```

Evidencia: `dist/obs-stream-metadata-0.1.0-windows-x64-local.exe` (239309 bytes,
sha256 `e3d4919a…62e2` recalculado == `.sha256`); log OBS
`[obs-stream-metadata] plugin loaded successfully (version 0.1.0)` + `dock shown` +
cierre limpio (`dock removed`, `plugin unloaded`); tras uninstall, OBS abre sin el
plugin en el log. Nota: un `-Include` de PS 5.1 en el gate de payload del script
listaba todo el staging (falso positivo); corregido con `Where-Object` por extensión.

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

## T-058 múltiples directorios de secretos (2026-09-10)

```text
CAMBIO:   CompositeSecretStore (orden determinista, vacío=fail-fast) +
            STREAM_META_BACKEND_SECRET_DIRS (os.pathsep; ambos
            DIR+DIRS = fail-fast; solo DIR = compat T-057 intacta).
BACKEND:   PASS 134/134 local (126 heredados + 8 nuevos: composite,
            split, wiring multi-dir, ambigüedad, nombres faltantes;
            PG apagado → 7 skips por diseño)
SECRET:    PASS motor CI (GNU grep, patrón exacto del workflow: limpio;
            .NET/PowerShell da falsos positivos sobre `*_token=` en SQL
            y ejemplos — documentado, no accionable)
FINAL: T-058 PASS (sin despliegue; rev Cloud Run pendiente de reintento)
```

## YouTube OAuth E2E en producción (2026-09-10, rev-00006-xqr)

```text
MODO:      Managed (el backend productivo solo habla Managed)
REV:       obs-stream-metadata-service-00006-xqr (imagen T-058 ad754012),
            100% tráfico, env=production
START:     PASS (bootstrap 201 → session 200 TTL 1800 → start 200)
AUTH URL:  PASS (accounts.google.com, redirect exacto, state, PKCE S256,
            scope youtube.force-ssl, sin secretos)
CONSENT:   PASS (operador, cuenta SonokiGame UCXw8bp4xOpig8Y85hNDlrcA)
CALLBACK:  PASS 200 {provider: youtube, status: connected}
STATE:     PASS (single-use + TTL por diseño; sin mismatch/replay)
EXCHANGE:  PASS (implícito en connected; client_secret solo backend)
PERSIST:   PASS (conexión en PostgreSQL/Neon; status posterior la confirma)
STATUS:    PASS 200 {provider: youtube, status: connected} sin tokens
HEALTH:    200 durante toda la prueba; /version production
SECRETOS:  NO expuestos (respuestas, logs, repo, temporales eliminados)
FINAL: YouTube OAuth E2E en producción — PASS
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

## Observación histórica GCP reportada (2026-10-24) [D histórico; no verificado en esta sesión de edición]

```text
ORIGEN: informe gcp-stream-metadata de sesión anterior (2026-10-24),
  no verificación ejecutada en la sesión de edición actual
  (aquí no se ejecutó `gcloud`/`curl`).
COMANDOS REPORTADOS (exactos, no re-ejecutados aquí):
  gcloud run services describe obs-stream-metadata-service --project=obs-stream-metadata --region=us-east5
  gcloud scheduler jobs describe obs-stream-metadata-purge --project=obs-stream-metadata --location=us-east1
RESUMEN REDACTADO DE CAMPOS OBSERVADOS [D histórico]:
  - Cloud Run: URL antigua
    https://obs-stream-metadata-service-364043334054.us-east5.run.app;
    rev 00036-cjj; imagen master-ee5a402; PUBLIC_URL con la misma URL
    antigua.
  - Scheduler: job obs-stream-metadata-purge; uri con la misma URL
    antigua; schedule `0 3 * * *`, zona America/New_York, estado
    ENABLED; campo status.code 13 reportado (se cita sin interpretar).
  - Sin datos personales ni secretos en este resumen.
ESTADO ACTUAL [P]: discrepancia entre esta observación histórica (URL
  antigua) y la URL T-076 DOCUMENTADA en DEPLOYMENT.md pendiente de
  aclarar; procedencia T-076 pendiente; estado operativo actual
  pendiente de re-verificación en vivo. Ver bloque canónico en
  `docs/DEPLOYMENT.md` (T-076/DRIFT).
```

## Cuentas Managed v1 Fases 1–5 (T-079–T-083, 2026-10-26, commit `0f015a3`)

```text
ORIGEN [D, no re-ejecutado en esta sesión documental]: mensaje del
  commit `0f015a3` ("feat(managed-users-v1): users PBKDF2 + 003 +
  endpoints + auth doble + gate plugin + erase (Fases 1-5)") + plan
  `docs/superpowers/plans/2026-10-26-managed-user-accounts.md`
  (Tasks 1–5 cerradas con evidencia).
SUITES BACKEND: 299→337 tests OK (`python -m unittest discover
  -s backend/tests`, incluye `test_users_model`, `test_users_endpoints`,
  `test_users_dualauth`, `test_users_erase` + ajustes facebook/http/kick/
  metadata_youtube/pg/twitch).
SELFCHECK PLUGIN: f4 (gate login Managed, cabecera `X-User-Session`,
  formas prohibidas, sin recuperación en v1) + f5 (erase separado de
  F-C2, dual Managed, mensajes solo-conteos) OK
  [C: checks `f4-*`/`f5-*` presentes en `src/metadata_selfcheck.cpp`].
SECRET-SCAN: limpio, sin valores en el informe [D: plan Tasks 2/5/7].
ALCANCE: Fases 1–5 cerradas con evidencia; Fase 6 (E2E multi-PC +
  backup/rollback, T-084) pendiente del operador. PG en vivo y E2E real
  quedan para el paso siguiente, no se afirman aquí [P].
FINAL: Fases 1–5 PASS documental; Fase 6 PENDING.
```
