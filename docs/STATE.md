# STATE — estado actual del proyecto

> Resumen breve reconciliado con el checkout y `docs/BACKLOG.md`
> (canónico: si discrepan, manda BACKLOG). Actualizar en cada tarea
> terminada. Convenciones: [C] comprobado en este checkout ahora mismo
> · [D] documentado (E2E/sesión previa citada, no re-ejecutado aquí)
> · [P] pendiente.

- **Checkout [C]:** rama `master` (HEAD `174b144`, merge PR #71
  `feat/sesion-persistente-t086`). Árbol limpio al inicio de esta
  tarea documental (`git status --porcelain` vacío); sin commit/push
  en esta tarea. `.env`/`dev.env` ignorados (`.gitignore:61/63`);
  sin lectura de secretos ni de `staging/` aquí.
- **Fase actual:** sin ciclo de desarrollo abierto. BACKLOG reconciliado
  [C contra `docs/BACKLOG.md`]: todo hecha salvo `T-002` (en-progreso,
  informe Phase 0, no bloquea), `T-059`/`T-066` (futuro: licencias/firma)
  y la fila `T-058` duplicada histórica (+ `T-042` superseded).
  Últimas: T-075 (PR #54) y T-076 (PR #55) hecha el 2026-10-24 [D].
- **Qué hicimos (post T-033):** T-062 Apply Managed Twitch (E2E de facto
  vía T-063) [D]; F-C1/F-C2/F-C4 (cifrado, borrado, purga+backups+
  alertas) [D]; T-067 `dist/` + `tools/package.ps1` [D]; T-068 Apply
  Managed Kick + `tools/deploy.ps1` [D]; T-069–T-073 Facebook perfil
  (Independent T-071, Managed T-072, cierre T-073 con PR #52) [D];
  T-074 outage 20–22/09 (fuga del pool + 401/500 honestos, rev
  `00028-zml`) [D]; T-075 auto-reconnect cómodo (PR #54) [D]; T-076
  Facebook gris + YouTube default listo (PR #55, E2E operador, rev
  `00036-cjj`) [D].
- **Baselines en este checkout [C, conteo estático sin ejecutar]:**
  backend 250 métodos `def test_*` en `backend/tests/` (coherente con el
  "250 OK" documentado en T-075/T-076); selfcheck 180 invocaciones
  `CHECK(` en `src/metadata_selfcheck.cpp` (+1 línea `#define`; último
  resultado documentado: SELFCHECK OK, T-073). No se ejecutaron pruebas
  en esta sesión documental.
- **Capacidades vigentes [C código + D E2E]:** un solo plugin, dos modos
  (Independiente BYO-app / Administrado vía backend). Twitch, YouTube,
  Kick y Facebook-perfil soportados en ambos modos
  (`managedSupported()==true` en `src/metadata_dock.cpp:928-932`;
  adapters + tests en `backend/`); descripción de stream solo en
  YouTube y Facebook-perfil (matriz `AGENTS.md` §3). Integración de
  Pages de Facebook: diferida, sin fechas. Estado de la app de Meta y
  requisitos de acceso para las pruebas del operador: pendientes de
  confirmar (sin afirmar Live, App Review ni acciones iniciadas en
  Meta).
- **Distinción obligatoria (AGENTS.md §55.8):** app de Meta en modo
  Development y sin App Review ⇒ solo sirve a usuarios con rol en la
  app (operador); NO es desbloqueo para terceros [D: F-072/F-079,
  DEPLOYMENT]. Hecho independiente del despliegue del backend en Cloud
  Run [D: subagente GCP 2026-10-24].
- **Cloud Run (estado operativo; detalle en DEPLOYMENT.md):**
  documentado rev `00036-cjj` con `PROVIDERS=youtube,kick,twitch,
  facebook`. URL canónica 2026-10-26 [D, confirmada por el usuario en
  chat; coincide con `FB_URL_APP` en `.env` y con `PUBLIC_URL` en
  Cloud Run, evidenciado por backend-integrations ese día sin citar
  valores]:
  `https://obs-stream-metadata-service-364043334054.us-east5.run.app`.
  La hash
  `https://obs-stream-metadata-service-mdurv2xfsa-ul.a.run.app` queda
  como alterna no canónica [D histórico 2026-10-24]. `tools/deploy.ps1:58`
  construye la canónica: NO necesita cambio [C, leído en esta sesión].
  Sin re-verificación en vivo en esta sesión [P].
- **Discrepancias residuales conocidas [P, requieren confirmación]:**
  1. `ARCHITECTURE-BACKEND.md` §18 fila Twitch Managed ("pendiente",
  "direct only") desactualizada frente al código (adapter + Apply +
  `managedSupported()==true`) y BACKLOG T-062 (precedente F-064; ARCH
  fuera del alcance de esta tarea documental). 2. `PRIVACY.md` declara
  "Twitch Managed is unsupported" (desactualizado; mismo origen).
  3. Datos vivos de producción (URL, revisión al 100 %, E2E) citados
  como documentados, no re-verificados aquí.
- **Plan del operador pendiente [P, intención comunicada por el usuario
  el 2026-10-24]:** avanzar durante octubre de 2026 hacia pruebas de
  Facebook con usuarios reales y streams reales. No se afirma que la app
  de Meta esté en Live, que App Review esté aprobado ni qué requisitos
  específicos aplican (sin evidencia). Tarea separada del despliegue del
  backend en Cloud Run (ver DEPLOYMENT.md); requiere confirmación y
  alcance explícito antes de ejecutar nada.
- **Facebook Live API [P]:** el operador quiere validar el flujo de
  LiveVideo creado por API con título, descripción y audiencia Público,
  ingestión de OBS/Aitum y publicación final. El proceso propuesto está en
  `docs/FACEBOOK-LIVE-API-FLOW.md`, T-077. F-067 ya documenta create/update/
  read-back de metadatos, pero la emisión real vía ingestión y publicación
  visible aún requieren validación. La integración Aitum/RTMP está fuera del
  alcance vigente (`AGENTS.md` §23/§51); resolver alcance antes de cambios. E2E lab 2026-10-26 [D]:
  Connect PKCE Test App PASS; create UNPUBLISHED EVERYONE ×5 PASS
  (IDs `28703591109303258`, `28703717372623965`, `28703720135957022`,
  `28703757945953241`, `28703813379281031`); read-back título/desc/
  status idénticos PASS; update+read-back PASS; ingesta manual Aitum →
  GoLive 200 → LIVE + DIRECTO visible en perfil PASS; End →
  LIVE_STOPPED + DELETE ×2 (100/33) PASS; bug lab Status-comas
  corregido (solo lab). Veredictos negativos: persistencia NO url única
  (F-092); privacy ilegible en GET (Meta 400) + badge "Solo yo" pese a
  EVERYONE 200 (F-093); localhost bloqueado en app Live con Enforce
  HTTPS, Test App como vía (F-094). Pendientes [P]: audiencia en
  producción, port al plugin, resolución del conflicto de alcance
  §§23/51 antes de implementar producto. BACKLOG canónico: T-077 sigue
  en investigación, sin cambio de estado. Detalle en VALIDATION.md.
- **Relay local MediaMTX [D E2E lab 2026-10-26, T-078 en investigación]:** propuesta validada en lab (`docs/MEDIAMTX-RELAY-FLOW.md` como guía; T-078): MediaMTX v1.21.3 local con forward RTMPS en caliente (PATCH sin reinicio, formato base+`#`+clave) → 2 emisiones LIVE+DIRECTO con movimiento sin tocar Aitum entre ellas; responde a F-092 (sin URL única de ingesta) con URL fija local + re-fijado del forward. Nuevo punto de partida arquitectónico pendiente de port al plugin y de resolución del conflicto de alcance §§23/51. Pendientes [P]: métricas latencia/CPU/recovery (F-099), audiencia en producción (sigue "Solo yo" pese a EVERYONE 200, como F-093), rate-limit a re-handshakes (~3 min, F-097). Detalle en VALIDATION.md T-078 y F-096–F-099. BACKLOG canónico: T-078 sigue en investigación (aceptación parcialmente cubierta), T-077 intacta.
- **Fix relay findBinary → data path primero (2026-10-26) [D delegada + C estático parcial]:** `findBinary` buscaba junto al programa pero el instalador pone el relay en el data path del módulo; ahora busca primero en `obs_get_module_data_path()/relay/` (causa del "binary missing" del operador con instalador nuevo). Evidencia delegada [D, no re-ejecutada aquí]: build OK, selfcheck 322/0 con relay-find-data 7/7, diff-check y secret-scan limpios. [C en esta sesión: `src/relay_manager.cpp:79` usa `obs_get_module_data_path`, `src/metadata_selfcheck.cpp` contiene 7 checks `relay-find-data-*`, y `git status --porcelain` lista 5 ficheros `src/` modificados sin commit]. Sin cambios de alcance §§23/51 (ADR-016 intacto). T-078 SIGUE en investigación sin cambio de estado (canónico BACKLOG). Pendiente [P]: operador reinstala/reabre OBS → Start relay → E2E instalar→forward→GoLive→LIVE + métricas F-099 + audiencia F-093. Detalle en VALIDATION.md.
- **Relay Fases 2–6 port lab→plugin (2026-10-26) [D delegada, no re-ejecutada aquí]:** `src/relay.h`/`src/relay.cpp` + `src/relay_manager.*` + vista en el dock + `tools/package.ps1` con relay/MIT (aviso MIT incluido en el empaquetado); build RelWithDebInfo OK; selfcheck 315 PASS/0 FAIL; `git diff --check` 0; secret-scan limpio; `package.ps1 -WhatIf` OK. Excepción acotada §§23/51 (ADR-016): proceso hijo + forward vía `:9997`, destino operador fijo `rtmp://127.0.0.1:1935/live`; sin automatizar Aitum/OBS, sin multistream general. T-078 SIGUE en investigación sin cambio de estado (canónico BACKLOG). Pendientes [P]: E2E operador instalar→relay→forward→GoLive→LIVE, métricas F-099, audiencia F-093, build E2E del instalador con descarga real, capturas visuales. Detalle en VALIDATION.md.
- **Port lab→plugin Fase D (2026-10-26) [D delegada + C parcial]:** Status sin privacy en ambos modos + fallback, persistent opt-OFF en ambos modos + mapeo `persistent-key-not-allowed`, mensajes F-097 (cooldown ~3 min) y F-098 (auto-VOD) en el dock; NO portado el formato lab base+clave (`#`) ni relay/scripts lab (quedan en `docs/MEDIAMTX-RELAY-FLOW.md` + `lab/**`). Evidencia delegada [D, no re-ejecutada aquí]: backend 334 passed/11 skipped + selfcheck 287/0 DLL OK + diff-check limpio + secret-scan limpio. [C en esta sesión: `git status --porcelain` lista 5 ficheros `src/` modificados sin commit (`metadata.cpp/.h`, `metadata_dock.cpp/.h`, `metadata_selfcheck.cpp`); sin commit/push]. T-077/T-078 siguen en investigación sin cambio de estado (canónico BACKLOG). Pendientes [P]: E2E operador del port + resolución del conflicto de alcance §§23/51. Detalle en VALIDATION.md.
- **Simplificación Facebook Fase A+B (2026-10-26) [D delegada, no re-ejecutada aquí]:** Fase A auto-prepare (auto-relay si FB conectado; valida ID: crea nuevo + borra anterior UNPUBLISHED, fallback reutilizar con aviso honesto; Apply con verificación silenciosa; señal del emisor vía API local; auto-prepare tras End con cooldown) + Fase B vista (línea "Facebook listo", Avanzado colapsado, botones condicionales, indicador de señal). Evidencia delegada [D]: build OK, selfcheck 338/0, diff-check y secret-scan limpios. [C en esta sesión: rama `feat/facebook-status-port-t077-t078` (HEAD `01ea886`) con ficheros `src/` modificados sin commit según `git status --porcelain`; solo docs + `git diff --check` aquí, sin commit/push]. T-078 SIGUE en investigación sin cambio de estado (canónico BACKLOG). Pendientes [P]: E2E operador, capturas visuales, instalador con relay+fix. Detalle en VALIDATION.md.
- **Plan consolidado v2 Facebook-automático (2026-10-26) [P, PLAN NO IMPLEMENTADO; T-078 sin cambio de estado]:** decisiones del dueño pendientes de implementar, consolidadas en `docs/VALIDATION.md` (origen: tarea que autoriza, 2026-10-26): arranque exacto por evento (E1/E2/E3, sin timers) + Go live automático (señal proxy relay+forward; audiencia: check global en Avanzado, defecto OFF) + semáforo sin texto (rojo/ruedita/triángulo) + contador ~mm:ss de cooldown + auto-End por pérdida de señal (solo LIVE auto-iniciado; flanco+antirrebote 60s+gracia 5min+confirmación Graph fresca; VOD siempre) + auto-prepare tras End (create nuevo + borra anterior UNPUBLISHED, fallback reutilizar). Alcance [P]: excepción acotada ADR-016 intacta. T-078 SIGUE en investigación (canónico BACKLOG). Pendientes [P]: implementación + E2E operador + métricas F-099 + audiencia F-093.
- **Validación dos-directos (2026-10-26) [VALIDACIÓN/PLAN, NO implementado; T-078 sin cambio de estado]:** veredictos obs-plugin + ux-ui comunicados en la tarea que autoriza [D, no re-ejecutados aquí]: pre-create al apagarse NO existe + auto-End NO existe + cola diferida NO existe + verify/refire independientes SÍ + Apply blinda carrera SÍ + presupuestos de un solo uso + lista de ~30 textos con jerga a reescribir + textos propuestos en inglés + principio "el usuario jamás espera ni decide nada técnico". Detalle en `docs/VALIDATION.md`. T-078 SIGUE en investigación (canónico BACKLOG). Pendientes [P]: redacción UX + implementación + E2E operador real + métricas F-099 + audiencia F-093.
- **Ajuste v3 Facebook-automático (2026-10-26) [P, PLAN NO IMPLEMENTADO; T-078 sin cambio de estado]:** decisiones nuevas del dueño, consolidadas en `docs/VALIDATION.md` (origen: tarea que autoriza, 2026-10-26): (1) detección por evento como primaria (avisos `runOnOnline`/`runOnOffline` del repetidor, docs oficiales MediaMTX v1.21 [D citado en la tarea; consulta directa pendiente [P]]) + pregunta cada 3 s de respaldo + confirmación 30 s seguidos (matiza v2 "sin timers"); (2) algoritmo adaptativo 3→2→1 con votación 2-de-3 e histéresis (rápido pre-crear ~6 s, prudente 30 s terminar; base Nagios + histéresis; Phi-accrual descartado por sobredimensionado para localhost); (3) check global "no volver a mostrar" del aviso de audiencia en Avanzado, defecto OFF, persistente plaintext con texto de riesgo F-093, solo salta ese aviso; (4) contador ~mm:ss junto al semáforo (estimación local con ~, informativo, sin bloquear ni reintentar solo); (5) semáforo sin texto (rojo/ruedita/triángulo) + textos internos en lenguaje de persona (~30 textos a reescribir [P]); (6) auto-End solo LIVE auto-iniciado (flanco+gracia 5 min+confirmación Graph fresca, VOD siempre) + pre-creación del siguiente al detectarse la caída + cola diferida de un solo disparo con guarda de ID (cambia los veredictos NO de dos-directos a planificado). Alcance [P]: excepción acotada ADR-016 intacta. T-078 SIGUE en investigación (canónico BACKLOG). Pendientes [P]: redacción UX + implementación + E2E operador + métricas F-099 + audiencia F-093.
- **Plan v3 — Fases completadas: lógica + vista (2026-10-26) [D delegada + C parcial; T-078 sin cambio de estado]:** Fase 1 lógica (arranque exacto por evento E1/E2/E3; escucha QFileSystemWatcher + respaldo cada 3 s; adaptativo 3→2→1 con votación 2-de-3 e histéresis; pre-create ~6 s; auto-End con cerrojos solo LIVE auto-iniciado; cola diferida de 1 disparo con guarda de ID) + avisos `runOnOnline`/`runOnOffline` en `tools/mediamtx.yml:44-45` [C, leído en esta sesión] + Fase vista (semáforo sin texto, contador ~mm:ss, check global de audiencia en Avanzado defecto OFF, ~40 textos en lenguaje de persona). Evidencia delegada [D, no re-ejecutada aquí]: build OK, selfcheck 344/0 (coherente con conteo estático [C]: 344 invocaciones `CHECK(` en `src/metadata_selfcheck.cpp`, sin ejecutar), diff-check y secret-scan limpios. Solo docs + `git diff --check` en esta subtarea, sin commit/push. T-078 SIGUE en investigación (canónico BACKLOG). Pendientes [P]: E2E operador con dos directos reales + métricas F-099 + audiencia F-093; capturas visuales; instalador con relay+fix. Detalle en VALIDATION.md.
- **Fix trío señal/listo/contador (2026-10-26) [D delegada; T-078 sin cambio de estado]:** causa raíz — el dock leía `GET /v3/config/paths/get/live` (config estática PathConf, sin estado) en vez de `GET /v3/paths/get/live` (runtime Path con online/source/inboundBytes); por eso signalHasEmitter era falso siempre con Aitum emitiendo [D]. Fix [D]: endpoint runtime + parse reescrito + predicado único fbEmitReadiness (verde exige las tres; ámbar "lista pero sin señal") + contador con causa y gate lastOk<=0 + log diagnóstico con conteos. Evidencia delegada [D, no re-ejecutada aquí]: build OK, selfcheck 367/0, diff-check y secret-scan limpios. Solo docs + `git diff --check` en esta subtarea, sin commit/push. T-078 SIGUE en investigación (canónico BACKLOG). Pendientes [P]: E2E operador + instalador. Detalle en VALIDATION.md.
- **Sesión Managed larga — 15 días deslizantes + tope 30 días (2026-10-26) [D delegada + C parcial; sin cambio de estados]:** backend 15 días deslizantes con actividad + tope absoluto 30 días desde el login (sustituye TTL fijo 30 min); 7 tests nuevos `test_users_session_sliding.py`, suite 341 passed/11 skipped verde [D]. Diagnóstico [D]: sesiones en PG, el deploy no mató nada; causa del cierre prematuro = TTL 30 min fijos. Plugin [D]: margen 5 min, `loginAt` DPAPI, motivo del re-login en persona + `obs_log` con códigos, selfchecks `usess-*` 34. [C en esta subtarea: 7 `def test_*` en el fichero nuevo + 28 ocurrencias `usess` en `src/metadata_selfcheck.cpp` (diferencia 28 vs 34 no conciliada [P]); `git status` con `backend/`+`src/` modificados sin commit; solo docs + `git diff --check` aquí, sin commit/push]. Pendientes [P]: deploy backend para efecto real + instalador + E2E operador. Detalle en VALIDATION.md.
- **Cambio "crear solo ante intención" (2026-10-26) [D delegada; T-078 sin cambio de estado]:** al abrir SOLO se valida (relay local + una lectura si hay ID); jamás create/refire/countdown/triángulo propio; creación solo ante primera intención en cualquier orden (Aplicar con título válido / señal con título válido / Salir al aire); sin título válido no hay creación ni espera; countdown solo tras acción del usuario [D]. Traza del build viejo documentada [D]: create+refire al abrir → cooldown ~2:59 sin hacer nada. Evidencia delegada [D, no re-ejecutada aquí]: build OK, selfcheck 409/0, diff-check y scan limpios. [C en esta subtarea: rama `feat/facebook-status-port-t077-t078` (HEAD `540dc95`), 5 ficheros `src/` modificados sin commit según `git status --porcelain`; solo docs + `git diff --check` aquí, sin commit/push]. T-078 SIGUE en investigación (canónico BACKLOG). Pendientes [P]: E2E operador + instalador. Detalle en VALIDATION.md.
- **Go live automático (2026-10-26) [D delegada; T-078 sin cambio de estado]:** disparo por eventos señal+verify; Verified→auto; AudienceUnverified→check global o Sí único, resto bloquea; un intento por ID, anti-doble, cooldown, relectura confirma [D]. Cierra la brecha documentada: el plan lo exigía desde v2 y hubo un apaño manual intermedio, ya sustituido [D]. Evidencia delegada [D, no re-ejecutada aquí]: build OK, selfcheck con autogo-* 13/13 en verde, diff-check y scan limpios. [C en esta subtarea: rama `feat/facebook-status-port-t077-t078` (HEAD `07b41b1`), `src/metadata*.cpp/.h` modificados sin commit, invocaciones `autogo-*` presentes en `src/metadata_selfcheck.cpp` sin ejecutar; solo docs + `git diff --check`, sin commit/push]. T-078 SIGUE en investigación (canónico BACKLOG). Pendientes [P]: E2E operador + instalador. Detalle en VALIDATION.md.
- **Cambio "sin Meta al abrir" (2026-10-26) [D delegada; T-078 sin cambio de estado]:** el auto-prepare al abrir creaba Y re-fijaba destino (handshake), chocando con cooldown y mostrando triángulo+cuenta sin que el usuario hiciera nada [D]; ahora crea/valida sin re-fijar y el destino se fija solo ante intención real (Aplicar, señal detectada, Go live) [D]; mensaje honesto sin cuenta ni triángulo [D]; botón de Avanzado renombrado a "Comprobar estado" (antes ambiguo con "Comprobar señal") [D]; el amarillo "Se cortó la conexión" es fallo de lectura del vídeo en Meta, no corte de emisión (aclarado en docs) [D]. Evidencia delegada [D, no re-ejecutada aquí]: build OK, selfcheck PASS 0 FAIL, diff-check y scan limpios. Solo docs + `git diff --check` en esta subtarea, sin commit/push. T-078 SIGUE en investigación (canónico BACKLOG). Pendientes [P]: E2E operador + instalador. Detalle en VALIDATION.md.
- **Reforma harness en curso [C, 2026-10-26, Fase 1 documental]:** apertura
  mínima-privilegio + validación por stack + reparto steward/BACKLOG y
  ejecutor lab (F-088). Cambios solo documentales/permisos, sin código:
  `harness-steward` único escritor de `docs/BACKLOG.md` + dueño de
  `.gitignore`; `release-tooling` en `tools/**` (sin ejecución);
  `backend-integrations` dueño ejecutor de `lab/**` (aún no existe en el
  checkout [C]); `obs-plugin` valida con `cmake*/ctest*/msbuild*`;
  `AGENTS.md` §56 con tabla de propiedad; TOOLS/WORKFLOW reconciliados.
  Sin cambios de estados BACKLOG en esta tarea. Próximo gate: Fase 2 lab
  por `backend-integrations` (incl. entrada `node_modules` en
  `.gitignore` y comprobación `browser: ask` con `opencode debug
  agents`).
- **Cuentas Managed v1 Fases 1–5 cerradas (2026-10-26) [D/C]:** commit
  `0f015a3` (`feat(managed-users-v1): users PBKDF2 + 003 + endpoints +
  auth doble + gate plugin + erase (Fases 1-5)`, en el historial de este
  checkout [C: listado en `git log --oneline`]) implementa: modelo
  `users` PBKDF2-SHA256 600k + migración `003_users.sql` (Fase 1),
  endpoints `POST /users/signup|login|logout` + `GET /users/me` + auth
  doble Managed con 401 normalizado (Fase 2), gate login en el plugin
  con sesión solo-memoria (Fase 3), selfcheck f4/f5 (Fase 4) y
  `POST /users/erase` en cascada separado de F-C2 (Fase 5). Evidencia
  [D, no re-ejecutada aquí]: suites 299→337 OK + selfcheck f4/f5 OK +
  secret-scan limpio (mensaje de `0f015a3` + plan
  `docs/superpowers/plans/2026-10-26-managed-user-accounts.md`).
  Canónico en BACKLOG T-079–T-084; acta en VALIDATION.md.
  `docs/PRIVACY.md` actualizado con el inventario Managed v1
  (descriptivo; el código manda). Deploy y smoke cerrados abajo; pendiente [P] solo Fase 6:
  backup/rollback (T-084, operador). Cierre 2026-10-26 [D]:
  tag `rev-managed-users-v1` hacia rev `obs-stream-metadata-service-
  00039-7jl` al 100% (`/health` ok, `/version` env=production,
  `/ready` true; reporte release-tooling + dictamen gcp en vivo);
  migracion 003 en prod (`schema_migrations` 001/002/003, consulta
  MCP Neon read-only a rama `production`); snapshot manual
  `snap-fancy-lab-a5r16mex` (2026-10-26T02:11:27Z; pg_dump opcional);
  smoke prod con usuario sintetico desechable PASS con limpieza
  (signup 201, login 200, me 200, status sin sesion 401, logout 200,
  me 401, erase 200, me 401; quedan 3 instalaciones huerfanas vacias
  de depuracion, sin datos: job diario o purga operativa [P]).
  Detalle en VALIDATION.md. Pendiente [P]: E2E operador con cuentas
  reales (OAuth por plataforma + Apply real + dos PCs; T-084 sigue
  pendiente, BACKLOG canonico).
- **Regla logout/login Managed (T-085, 2026-10-26) [D]:** logout explícito = desconexión total Managed de la PC; re-login mismo usuario conserva; otro usuario entra limpio; reinicio sin logout conserva. Pendiente [P]: desplegar backend + E2E operador.
- **Sesión Managed persistente (T-086, 2026-10-26) [D, decisión del usuario; sustituye spec §5 memoria-solo]:** la sesión de usuario Managed persiste cifrada en DPAPI (`managedUser`: token+expiry+username, fail-closed ante mismatch de `backendBaseUrl`) hasta logout explícito/401/expiración; al abrir OBS con sesión válida → directo al panel; login solo si venció o hubo logout. Implementado por obs-plugin + routing ux-ui (cambios sin commitear en este checkout [C]); build+selfcheck verdes según sesión implementadora (`startup-*`/`muser-*`/`card-*`/`f6-f8` PASS, DLL OK) [D]. Pendientes [P]: integrar (commit/PR), recompilar instalador, E2E operador con capturas.
- **E2E operador single-PC Managed (2026-10-26) [D, chat del operador; autorización: confirmó en prod los 3 puntos con "OK OK OK"]:** 1. reapertura con sesión válida → directo al panel (sesión persistente T-086 OK en vivo); 2. logout → pantalla entrar + plataformas en gris neutro, sin rojos (T-086 OK en vivo); 3. logout desconectó las 4 plataformas (regla T-085 funcionando en vivo). Pendiente [P]: T-084 multi-PC (requiere 2 máquinas) + OAuth real con Apply de título real del operador. Detalle en VALIDATION.md. Sin cambios de estado BACKLOG en esta tarea (fuera de alcance).
- **Rama base:** `master` (ADR-004; CI anclado a `master`).
- **Repo remoto:** `horangelmillan/obs-stream-metadata` (público,
  GitHub-flow con PRs).
- **Restricción vigente:** `poc/` es evidencia histórica + regresión
  (no tocar salvo corrección justificada); el producto vive en `src/`
  + `backend/`.
- **Última actualización:** 2026-10-26 — Cambio "crear solo ante
  intención" (IMPLEMENTADO SIN COMMIT; al abrir solo valida, jamás
  create/refire/countdown/triángulo propio; creación solo ante
  primera intención en cualquier orden; sin título válido no hay
  creación ni espera; countdown solo tras acción del usuario;
  traza del build viejo create+refire→cooldown ~2:59 documentada;
  build OK, selfcheck 409/0, diff-check y scan limpios [D delegada];
  T-078 sigue en investigación sin cambio de estado; pendientes [P]:
  E2E operador, instalador);
  sin commit/push en esta tarea documental.
  Anterior (2026-10-26): Go live automático
  (IMPLEMENTADO SIN COMMIT; disparo por eventos señal+verify,
  Verified→auto, AudienceUnverified→check global o Sí único,
  resto bloquea; un intento por ID, anti-doble, cooldown,
  relectura confirma; cierra la brecha del plan v2 sustituyendo el
  apaño manual intermedio; build OK, selfcheck autogo-* 13/13 en
  verde, diff-check y scan limpios [D delegada]; T-078 sigue
  en investigación sin cambio de estado; pendientes [P]: E2E
  operador, instalador);
  sin commit/push en esta tarea documental.
  Anterior (2026-10-26): Cambio "sin Meta al abrir"
  (IMPLEMENTADO SIN COMMIT; auto-prepare crea/valida sin re-fijar,
  destino solo ante intención real, mensaje honesto, "Comprobar
  estado", amarillo = fallo de lectura en Meta; build OK, selfcheck
  PASS 0 FAIL, diff-check y scan limpios [D delegada]; T-078 sigue
  en investigación sin cambio de estado; pendientes [P]: E2E
  operador, instalador);
  sin commit/push en esta tarea documental.
  Anterior (2026-10-26): Sesión Managed larga
  (IMPLEMENTADA SIN COMMIT; 15 días deslizantes + tope 30 días,
  7 tests nuevos, suite 341/11 verde [D delegada]; plugin margen
  5 min + loginAt DPAPI + motivo visible + obs_log con códigos +
  selfchecks usess-* 34 [D delegada]; sin cambio de estados;
  pendientes [P]: deploy backend para efecto real, instalador,
  E2E operador);
  sin commit/push en esta tarea documental.
  Anterior (2026-10-26): Fix trío
  señal/listo/contador (IMPLEMENTADO SIN COMMIT; causa raíz
  config-estática vs runtime + endpoint runtime + fbEmitReadiness +
  contador con causa/gate + log con conteos; build OK, selfcheck
  367/0, diff-check y scan limpios [D delegada]; T-078 sigue en
  investigación sin cambio de estado; pendientes [P]: E2E operador,
  instalador con relay+fix);
  sin commit/push en esta tarea documental.
  Anterior (2026-10-26): Ajuste v3
  Facebook-automático (PLAN NO IMPLEMENTADO; decisiones nuevas del
  dueño: evento primario runOnOnline/runOnOffline + poll 3 s +
  confirmación 30 s; adaptativo 3→2→1 2-de-3; check audiencia
  global OFF plaintext F-093; contador ~mm:ss; semáforo sin texto;
  auto-End solo LIVE auto-iniciado + pre-creación + cola 1 disparo;
  T-078 sigue en investigación sin cambio de estado);
  sin commit/push en esta tarea documental.
  Anterior (2026-10-26): Validación "dos directos
  seguidos" (VALIDACIÓN/PLAN, NO evidencia de implementación;
  veredictos obs-plugin + ux-ui: pre-create NO + auto-End NO + cola
  diferida NO + verify/refire SÍ + Apply blinda SÍ + presupuestos
  1 uso + ~30 textos + principio no-técnico; T-078 sigue en
  investigación sin cambio de estado);
  sin commit/push en esta tarea documental.
  Anterior (2026-10-26): Plan consolidado v2
  Facebook-automático (PLAN NO IMPLEMENTADO; decisiones del dueño
  pendientes: arranque E1/E2/E3 sin timers + Go-live auto + semáforo
  + contador cooldown + auto-End + auto-prepare; T-078 sigue en
  investigación sin cambio de estado);
  sin commit/push en esta tarea documental.
  Anterior (2026-10-26): Simplificación
  Facebook Fase A+B documentada (auto-prepare + vista; build OK,
  selfcheck 338/0, diff-check y scan limpios [D delegada]; T-078 sigue
  en investigación sin cambio de estado; pendientes [P]: E2E operador,
  capturas visuales, instalador con relay+fix);
  sin commit/push en esta tarea documental.
  Anterior (2026-10-26): Fix relay findBinary
  documentado (data path primero, causa del "binary missing" con
  instalador nuevo; build OK, selfcheck 322/0 con relay-find-data 7/7
  [D delegada + C estático]; T-078 sigue en investigación sin cambio
  de estado; pendientes [P]: reinstalar/reabrir OBS, Start relay, E2E);
  sin commit/push en esta tarea documental.
  Anterior (2026-10-26): Relay Fases 2–6
  documentadas (relay.h/cpp + relay_manager + vista + package.ps1 con
  relay/MIT; build RelWithDebInfo OK, selfcheck 315 PASS/0 FAIL,
  diff-check 0, secret-scan limpio, WhatIf OK [D delegada]; T-078 sigue
  en investigación sin cambio de estado; E2E operador + métricas F-099 +
  audiencia F-093 + build E2E instalador + capturas pendientes [P]);
  sin commit/push en esta tarea documental.
  Anterior (2026-10-26): Port lab→plugin Fase D
  documentado (status sin privacy + fallback, persistent opt-OFF +
  mapeo, mensajes F-097/F-098; sin formato base#clave ni relay; T-077/
  T-078 sin cambio de estado; E2E operador + alcance §§23/51
  pendientes [P]); 5 ficheros `src/` modificados sin commit [C];
  sin commit/push en esta tarea documental.
  Anterior (2026-10-26): E2E operador single-PC
  Managed PASS (sesión persistente → directo al panel + logout a
  entrar en gris neutro + desconexión total de las 4 plataformas;
  "OK OK OK" en prod [D]; T-084 multi-PC + OAuth real con Apply
  pendientes [P]); sin commit/push en esta tarea documental.
  Anterior (2026-10-26): T-086 hecha (sesión Managed persistente DPAPI + routing arranque + estados gris/rojo tarjeta; sustituye spec §5 por decisión del usuario [D]; build+selfcheck verdes [D]; pendientes [P]: integrar, recompilar instalador, E2E operador con capturas); sin commit/push en esta tarea documental.
  Anterior (2026-10-26): T-085 hecha (regla "logout limpia, entrar limpio" implementada y verificada según autorización [D]; 8 tests nuevos + suite 345 OK; pendiente [P]: desplegar backend + E2E operador); sin commit/push en esta tarea documental.
  Anterior (2026-10-26): loop deploy+verificación
  Managed v1 cerrado (rev `00039-7jl` al 100%, migración 003 en prod,
  snapshot `snap-fancy-lab-a5r16mex`, smoke sintético PASS con limpieza;
  T-084 E2E operador pendiente); sin cambios de código, sin operaciones
  externas en esta sesión documental.
  Anterior (2026-10-26): E2E relay T-078 documentado
  (forward + caliente PASS, métricas parciales; sigue en investigación,
  T-077 intacta); sin cambios de código, sin operaciones externas.
  Anterior (2026-10-25): reforma harness Fase 1 documental (F-088).
  Anterior (2026-10-24): flujo objetivo Facebook Live API documentado
  como propuesta pendiente (`docs/FACEBOOK-LIVE-API-FLOW.md`, T-077).
