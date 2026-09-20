# T0FB-RESEARCH — FB-0: inventario + brief Facebook (T-069, sin código)

> Fecha: 2026-09-20 · Estado: `T-069` en-progreso · Sin cambios de producto.
> Regla de cita (RESEARCH.md): cada decisión cita archivo/línea del proyecto
> (Step 0) + fuente Meta con URL + versión/fecha. Nada de tutoriales viejos
> como autoridad. Consultas Meta realizadas el 2026-09-20 (Graph API v25/v26).

## A. Step 0 — Mapa del proyecto (verificado contra código)

Archivos leídos (todos, sin omitir): `src/` (14 ficheros: `metadata.h/.cpp`,
`metadata_dock.h/.cpp` 2774 líneas, `secure_store.h/.cpp`, `backend_auth.h/.cpp`,
`dock-poc.*`, `plugin-main.c`, `plugin-support.*`, `metadata_selfcheck.cpp`,
`managed_link_test.cpp`), `backend/` (`app.py`, `http_server.py`, `kernel.py`,
`ports.py`, `oauth.py`, `errors.py`, `auth.py`, `config.py`, `environment.py`,
`stores.py`, `pgstores.py`, `prodstores.py`, `db.py`, `token_crypto.py`,
`logging_setup.py`, `maintenance.py`, `privacy.py`, `adapters/{youtube,kick,twitch}.py`,
`migrations/001_init.sql + 002_token_ciphertext.sql`, 18 tests),
`tools/` (8 scripts), `cmake/` (plantilla OBS), raíz (`CMakeLists.txt`,
`CMakePresets.json`, `buildspec.json`, `Dockerfile.backend`), `data/locale/en-US.ini`,
`ops/monitoring/`, `poc/t030/`, `.github/workflows/ci-phase0.yml` y docs
normativas (`AGENTS.md`, `ARCHITECTURE-BACKEND.md`, `PROJECT_CONTEXT.md`,
`BACKLOG.md`, `STATE.md`, `FINDINGS.md` F-001…F-064, `VALIDATION.md`,
`DEPLOYMENT.md`, `SECURITY.md`, `PRIVACY.md`, `TOOLS.md`, `TROUBLESHOOTING.md`,
`RESEARCH.md`, `PHASES.md`, `INSTALLER.md`, `T030-POC.md`, `FASES-COMERCIAL.md`,
ADRs 001…014, notas 001…007). Grafo `codebase-memory`
(`C-Users-Horan-Desktop-stream-manager`: 2011 nodos, 5731 aristas) usado para
`ConnectService.apply_metadata` (`backend/oauth.py:171`), `TokenStore`
(`backend/ports.py:58`), matriz Provider×Mode y boundaries.

### A.1 Patrón por proveedor (el que Facebook debe reutilizar)

- Kernel sin detalles de proveedor (`backend/kernel.py:1-13`): `Provider`
  (`:12`), `CAPABILITIES` título/descripción (`:34-38`), `Account`
  (`:42-47`), `MetadataUpdate{platforms,title,description,broadcast_id}`
  (`:81-88`, contenido solo tránsito, nunca persistido).
- Puerto `OAuthProvider` (`backend/ports.py:25`): `authorization_url`,
  `exchange_code`, `refresh`, `revoke`, `fetch_identity` + `list_resources` /
  `apply_metadata` opt-in con default que rechaza (`:49-55`). `TokenStore`
  (`:58-68`), `OAuthTransactionStore` single-use+TTL (`:140-172`),
  `ConnectionStore` (`:175-194`, sin títulos persistidos).
- `ConnectService` genérico (`backend/oauth.py:29`): `start` (PKCE+state,
  TTL 600 s, `:43-61`), `callback` (wrong-provider sin consumir, `:64-93`),
  `status`/`connection` (solo id+display, `:99-118`), `ensure_fresh_token`
  single-flight (`:125-161`), `list_resources`/`apply_metadata` server-side
  (`:164-176`), `disconnect` borrado-local + revoke best-effort (`:205-214`).
- Rutas genéricas, sin cambios por proveedor nuevo (`backend/http_server.py`):
  `POST /metadata/<name>` (`:285-293`), `POST /connect/<provider>`
  (`:295-301`), callback sin bearer (`:334-337`), `/connect/<p>/disconnect`,
  `/privacy/erase` (F-C2, `:267-281`), `/ops/purge` (F-C4).
- Errores taxonomía §28 (`backend/errors.py:13-22`): respuestas solo
  `{code,message,requestId}`, detalle solo logs.
- Wiring por flags (`backend/app.py:60-79`): `enable_youtube/kick/twitch`;
  prod exige secretos por nombre (`:150-164`), `PROVIDERS=youtube,kick` en
  despliegue (`docs/DEPLOYMENT.md:74`).

### A.2 Plugin C++ / modos

- `meta::Platform` + `ConnectionMode` (`src/metadata.h:20-27`); límites
  Twitch 140 / YT 1–100 + desc 5000 (`:42-45`); `supportsDescription` solo YT
  (`:62`); `Outcome` §28 (`:78-88`); `Account != ManagedConn`
  (`src/metadata_dock.h:53-57` vs `src/secure_store.h:34-42`).
- `managedSupported()` true para YT/Kick/**Twitch**
  (`src/metadata_dock.cpp:740-744`); Apply Managed por proveedor
  (`src/metadata_dock.h:108-118`); `ManagedConn` sin tokens por construcción.
- Custodia: DPAPI CurrentUser (`src/secure_store.h:4-14`), `backendInstall`
  ligado a `backendBaseUrl` (`:44-65`, `:89-93`); `backend_auth::Client`
  bootstrap→HMAC→bearer en memoria (`src/backend_auth.h:28-60`); sin fallback
  provider/modo; `loadStore` sin auto-fetch.
- Estado: disco = snapshots en claro + DPAPI; PG (`001_init.sql`,
  `002_token_ciphertext.sql` Fernet F-C1); prod rev-00027 sana
  (`docs/DEPLOYMENT.md:47-51`); backend 215 OK + selfcheck 125/125.

### A.3 Discrepancia registrada

- F-064: docs que dicen "Twitch Managed unsupported/pendiente" están
  desactualizadas frente al código FASE 2 (implementado, no desplegado en
  prod). No bloquea FB-0; reconciliar en FB-4 o tarea docs dedicada.

## B. Brief Facebook (7 puntos, fuentes oficiales fechadas)

### 1) OAuth distribuido

- Flujo manual desktop documentado (v26.0, actualizado 2026-06-30):
  `GET https://www.facebook.com/v26.0/dialog/oauth?client_id=&redirect_uri=&state=`;
  en desktop embebido el redirect es
  `https://www.facebook.com/connect/login_success.html`; `state` anti-CSRF.
  Fuente: `developers.facebook.com/documentation/facebook-login/guides/advanced/manual-flow`.
- Existe flujo OIDC Authorization Code + PKCE para login manual: `client_secret`
  **opcional** en `/oauth/access_token` si se envía `code_verifier` (S256).
  Fuente: `.../facebook-login/guides/advanced/oidc-token`.
- Redirects: Strict Mode obligatorio; URIs en Valid OAuth Redirect URIs;
  `http://localhost` auto-permitido **solo en Development mode**; en el resto
  se exigen https. App tipo Native/Desktop: el secret en binario se asume
  expuesto (no firmar llamadas app-token). `appsecret_proof` (sha256 del token
  con el secret) opcional endurableble. Fuente: `.../facebook-login/security`.
- Scopes mínimos: perfil → `publish_video`; Page → token de admin +
  `pages_read_engagement` + `pages_manage_posts` (+ `pages_show_list` para
  listar). Fuente: `.../live-video-api` (overview) y `.../live-video-api/guides/streaming`
  (actualizado 2026-07-02).
- Niveles: Standard (solo usuarios con rol en la app / desarrollo) vs Advanced
  (cualquiera; requiere App Review y a veces business verification). App nueva =
  Development mode: todo activo pero solo para roles + test users/test pages.
  Fuentes: `.../development/build-and-test/app-modes` (2025-05-05),
  `.../graph-api/overview/access-levels`, `.../development/build-and-test` (test users).
- Revocación: `DELETE /{user-id}/permissions/{permission}` (permiso) y
  `DELETE /{user-id}/permissions` (de-autorización total, invalida tokens);
  + deauthorize/data-deletion callback configurable en dashboard.
  Fuente: `.../graph-api/reference/user/permissions` (v26.0),
  `.../facebook-login/guides/permissions/request-revoke`.

### 2) Endpoints update / list / read

- `POST /{live-video-id}` = actualizar campos del LiveVideo (título ≤254,
  descripción). `GET /{page-id|user-id}/live_videos` = listar.
  `GET /{live-video-id}?fields=status,title,description` = leer.
  Fuente: `.../live-video-api/reference` (tabla de endpoints).
- El overview afirma manipulación del objeto para "update its description or
  title". `LIVE_VIDEO__EDIT_API_NOT_ALLOWED` = editar un live por la **Video**
  API está prohibido **mientras está en vivo**: hay que usar el live-video ID
  (o sea, `POST /{live-video-id}` es la vía permitida en vivo).
  Fuente: `.../live-video-api/overview` + tabla de error codes del reference.
- Creación con `status` (`UNPUBLISHED, LIVE_NOW, SCHEDULED_*`) y
  `title`/`description` como query params; ejemplo v26.0 con título+descripción.
  Fuente: `.../live-video-api/guides/streaming` (2026-07-02) y referencia
  `.../graph-api/reference/{user,page,event}/live_videos` (vía context7).
- Hipótesis a validar en FB-1: enum exacto de `status` en lectura para el
  indicador (filtros documentados: `LIVE, VOD, SCHEDULED_LIVE`), y update sobre
  vídeo `UNPUBLISHED`/programado sin emitir (vía E2E sin live).

### 3) Go-live / stop / audiencia + interplay Aitum (sin RTMP/keys, §51)

> Principio general (fuente de ingesta agnóstica): cualquier herramienta
> externa (multistream como Aitum, OBS directo, otro software, o Live Producer
> con clave) deja un LiveVideo en vista previa (UNPUBLISHED, sin publicar)
> alimentado por RTMPS. Todo lo que el operador hace hoy en el navegador
> (título+descripción, audiencia "Público", pulsar "Transmitir") es estado del
> LiveVideo **existente**, no creación de ingesta: encaja en el patrón del
> proyecto (como el selector de broadcast de YouTube), sin tocar RTMP/keys ni
> depender de ninguna herramienta externa. El caso Aitum del operador es solo
> una instancia de este flujo general.

- Publicar el vídeo existente = transición de estado vía API: `POST`
  con `status=LIVE_NOW` sobre el LiveVideo en vista previa (es lo que hace el
  botón "Transmitir" de Live Producer). Evidencia: respuesta aceptada en
  Stack Overflow citando la guía oficial (`developers.facebook.com/docs/live-video-api/guides/…`,
  2021-01-12; hilo `65673339`). La referencia `user|page/live_videos`
  (v25/v26) documenta el enum `UNPUBLISHED, LIVE_NOW, SCHEDULED_*` y la
  referencia LiveVideo expone sección `Updating`.
- Audiencia = campo `privacy` del LiveVideo (`Privacy Parameter`, documentado
  en create de user/page/event). Poner "Público" equivale a
  `privacy={"value":"EVERYONE"}`. El error oficial `LIVE_VIDEO__PRIVACY_REQUIRED`
  ("You need to set a privacy before going live") confirma que privacy se fija
  por API antes de publicar. El overview (2026-07-02) lista "define audiences"
  como manipulación soportada del objeto. Pendiente FB-1: confirmar que
  `privacy` acepta update post-creación (no solo en create).
- Título confirmado "Maximum 254 characters" (referencia `user/live_videos`
  v25.0). Terminar = `POST /{id}?end_live_video=true` → VOD (VODs: retención
  30 días desde 2025-02-19, `about.fb.com/news/2025/02/…`).
- Hipótesis H1 (a validar en FB-1, sondas solo-lectura): el LiveVideo que deja
  la herramienta externa es un objeto del usuario/page y aparece en
  `GET /{user-id|page-id}/live_videos` con nuestro token (`publish_video` /
  page token), sea cual sea la herramienta que lo alimenta. Diseño de sonda:
  con la ingesta externa en vista previa, comparar el ID de la URL de Live
  Producer (p. ej. `/live/producer/1822728272489194`) contra el listado +
  `GET /{id}?fields=status,title,description` (esperado: UNPUBLISHED). Si H1
  falla, el flujo "gestionar vídeo de terceros" no es viable y se rediseña.
- Grupos fuera: la Groups API está deprecada desde v19 (enero 2024); el alcance
  es perfil + Page únicamente. Fuente: guía de cambios v19 + PRISM (2024-04-24).

### 8) App Review para Managed-terceros (detalle; sin coste, sin bloqueo de seguridad)

- **Coste: cero.** Ni la revisión ni la verificación de empresa se pagan;
  son procesos documentales. Tiempos orientativos oficiales: revisión de
  permisos hasta varias semanas; verificación de empresa unos días según
  calidad de la documentación. Fuente: `.../resp-plat-initiatives/app-review/AR-FAQs`
  ("How long will it take…"), tutorial App Review.
- **Corrección importante: una app en Development NO puede atender a terceros.**
  Regla oficial: si la usa alguien sin rol en la app, debe pasar App Review;
  en Development solo funcionan usuarios con rol (admin/dev/tester). La app
  centralizada en modo desarrollo sirve para ti y para pruebas, **no para
  clientes**. Para clientes hace falta app en Live + acceso Advanced aprobado.
  Es el mismo patrón que YouTube (verificación de scopes). Fuente:
  `.../docs/apps/review`, `.../development/build-and-test/app-modes` (2025-05-05).
- **Checklist Managed-Facebook**: business verification (obligatoria para
  Advanced; documentos legales/facturas, una sola vez por Business Manager) +
  icono sin marcas + Privacy Policy URL + callback de borrado de datos +
  preguntas de manejo/protección de datos + screencast 1080p (login + cada
  permiso/feature en uso; Meta sugiere grabar con OBS) + app testeable por el
  revisor (test app hija hereda permisos) + Data Use Checkup anual. Fuente:
  tutorial y guía de envío App Review.
- **Riesgo de revisión (no bloqueante)**: el overview exige que la app
  "produzca un stream RTMPS"; la nuestra gestiona, no emite. Mitigación: el
  screencast demuestra el flujo completo con encoder externo alimentando
  (la propia guía contempla "streaming software of your choice" junto a la
  API), y el lanzamiento puede ser Independent-primero (sin revisión) +
  Managed tras la aprobación. Fuente: overview + getting-started v26.0.
- **Seguridad: sin bloqueo.** Mismo modelo que YT/Kick (secret solo Secret
  Manager, tokens cifrados F-C1, borrado total F-C2, nada en repo/logs). Los
  Platform Data (tokens, IDs) ya están minimizados en `docs/PRIVACY.md`; las
  respuestas del cuestionario salen de ahí. Fuente: Platform Terms §3/§6.
- **Indie sin empresa (2026-09-20): hasta dónde se llega.**
  - La verificación **individual/personal ya no habilita Advanced**
    (Meta, blog 2023-02-01: "individual verification will no longer be
    allowed for access"). Hace falta verificación de **negocio**, pero NO una
    sociedad: el propio flujo de Meta ofrece opción **Sole Proprietor**, y hay
    documentos de autónomo aceptados por país (Japón: `個人事業の開業届`;
    Brasil: MEI; España/LATAM: alta de autónomo/monotributo o el documento de
    la lista "Other countries": licencia, registro fiscal, bank statement del
    negocio). Meta no cobra nada; el único coste es el trámite local (a menudo
    gratuito o mínimo). Fuente: blog Meta 2023-02-01, Help Centre `159334372093366`,
    guías 2026 (keepersdigital, singhamandeep).
  - Escalera realista: 1) uso propio + test users (dev, sin revisión);
    2) **Independent BYO-app para clientes** (cada cliente crea su app gratis
    y la usa para sí mismo en dev: oficial, sin revisión; coste = UX de setup);
    3) Managed-terceros tras alta de autónomo + verificación + App Review.
  - Decisión D14: el producto no exige empresa; el techo sin registrarse como
    autónomo es el modo Independent (D4), que ya cubre título+desc+audiencia+
    publicar para cada usuario con su propia app.
- **Decisión §51 revisada**: ON (publicar) viable SÍ sobre el vídeo existente
  en vista previa con datos del encoder (D6R); OFF viable SÍ (D7); audiencia
  "Público" viable SÍ como `privacy` (D11). Nada de esto toca RTMP/keys/Aitum:
  es gestión de estado del LiveVideo, análoga al `liveBroadcasts.transition`
  de YouTube. Confirmación empírica obligatoria en FB-1 antes de FB-2.

### 4) Tokens (estrategia sin refresh clásico para `TokenStore`)

- Tokens cortos (horas) → user long-lived ~60 días vía
  `GET /oauth/access_token?grant_type=fb_exchange_token&client_id=&client_secret=&fb_exchange_token=`,
  **solo server-side** (lleva app secret), nunca con token expirado.
  Page token derivado del user long-lived **sin fecha de expiración**
  (solo cae por password/rol/de-autorización). Sin `refresh_token` clásico.
  Fuente: `.../facebook-login/guides/access-tokens/get-long-lived`
  (actualizado 2026-06-30, v26.0).
- Estrategia compatible con `TokenStore(provider,user_id)` (`backend/ports.py:58`)
  y `ensure_fresh_token` (`backend/oauth.py:125-161`): guardar `TokenPair`
  con `refresh_token=""` + `expires_in` real (user) / `0` (page sin expiración);
  al expirar → `SESSION_EXPIRED` → reconectar (flujo ya existente, sin romper
  a otros providers). El adapter FB implementará renovación por re-exchange
  con secret backend-only (detalle FB-3). Independent BYO-app: el usuario posee
  su app secret (mismo modelo que YT/Kick Independent, DPAPI).

### 5) Catálogo de errores → mensajes UI

| Origen | Mapeo backend (`backend/errors.py`) | Mensaje UI propuesto |
|---|---|---|
| `200` subcode `1363120` (cuenta <60 días) | `AUTHORIZATION` | "Tu cuenta debe tener al menos 60 días para transmitir en Facebook." |
| `200` subcode `1363144` (Page/perfil-pro <100 seguidores) | `AUTHORIZATION` | "Tu Page necesita al menos 100 seguidores para transmitir." |
| `190` token inválido/expirado | `SESSION_EXPIRED` → re-exchange/reconnect | "Sesión de Facebook caducada. Vuelve a conectar." |
| `200` permisos / `10` revisión requerida | `AUTHORIZATION` | "Faltan permisos o la app aún no tiene aprobación." |
| `100` parámetro inválido / título >254 | `INVALID_REQUEST` | Validación local 1–254 antes de enviar. |
| `613` / `4`/`17` rate limit, `2`/`1` backend | `PROVIDER_RATE_LIMITED` / `PROVIDER_UNAVAILABLE` | Reintento acotado existente (sin loops). |
| `240` restricción desktop, `459` checkpoint | `PROVIDER_REJECTED` | "Facebook bloqueó la operación. Revisa tu cuenta." |
| `LIVE_VIDEO__EDIT_API_NOT_ALLOWED` | `INVALID_REQUEST` interno | Nunca llamar a Video API en vivo; usar `POST /{live-video-id}`. |

Fuente códigos elegibilidad: getting-started v25/v26 (tabla "Permission denied
error codes"); resto: referencia `user/permissions` + taxonomía del proyecto.

### 6) E2E viable con activos actuales

- Activos: cuenta 60+ días OK; sin Page 100+; Meta app por crear.
- Vía E2E sin Page: **perfil** (solo exige 60 días) → crear LiveVideo
  `SCHEDULED`/`UNPUBLISHED` → `POST /{id}` título+desc → read-back → `DELETE`.
  Sin emitir, sin Page, sin revisión (app en Development + rol propio).
- Temprano (FB-1): app en Development + test users/test pages para loops
  offline/vivos sin tocar la cuenta real.
- Pendiente del operador: crear la Meta app (Development) y decidir si se
  persigue Page 100+ para E2E Page real; App Review + Advanced solo cuando el
  producto vaya a terceros (no para FB-1/FB-2).

### 7) Decisiones (gate: brief aprobado por el operador)

| # | Decisión | Sustento proyecto (archivo/línea) | Fuente Meta (fecha) |
|---|---|---|---|
| D1 | Metadata FB viable SÍ (título+descripción, perfil y Page) | Patrón `apply_metadata` opt-in `backend/ports.py:49-55`; genérica `/metadata/<name>` `backend/http_server.py:285-293` | broadcasting guide 2026-07-02 (v26.0): update title/description; `POST /{live_video_id}` reference |
| D2 | Título ≤254 + descripción SÍ (a diferencia de Twitch/Kick) | `CAPABILITIES` `backend/kernel.py:34-38` (nueva entrada FB `True,True`); validación local estilo `src/metadata.h:42-45` | create params `title (max 254 chars)` (context7, graph-api reference) |
| D3 | Page+perfil confirmados | `Provider` enum + `ConnectService` por proveedor `backend/app.py:60-79` | streaming guide 2026-07-02: `publish_video` vs `pages_*` |
| D4 | Independent SÍ (BYO-app; redirect a confirmar empíricamente; OIDC+PKCE secretless como alternativa) | `Record` DPAPI + `Account` Independent `src/secure_store.h:34-42`; callback loopback existente | manual-flow 2026-06-30 (v26.0); OIDC PKCE doc; security (localhost dev) |
| D5 | Managed SÍ (adapter + `ConnectService`, sin cambios de routing) | `ConnectService` `backend/oauth.py:29`; `required_secret_names` patrón `backend/adapters/youtube.py:168`; gate prod `backend/app.py:150-164` | long-lived tokens 2026-06-30 (secret server-side); app-modes 2025-05-05 |
| D6R | Go-live ON por API: SÍ sobre el LiveVideo existente en vista previa (revisa D6) | `list_resources`+`apply_metadata` con id `backend/oauth.py:164-176`; `MetadataUpdate.broadcast_id` `backend/kernel.py:88` | Transición `status=LIVE_NOW` (SO `65673339` citando guía oficial, 2021); enum `UNPUBLISHED…` (referencia v25/v26); `Updating` en `user\|page/live_videos` |
| D7 | Indicador en-vivo SÍ; botón OFF viable SÍ (con confirmación UI por riesgo) | `GET`+read-back patrón YT `backend/adapters/youtube.py:248-272` | `end_live_video` (getting-started v26.0); filtros `LIVE, VOD, SCHEDULED_LIVE` |
| D11 | Audiencia "Público" SÍ como `privacy={"value":"EVERYONE"}` (validar update post-create en FB-1) | `apply_metadata(access,data)` genérico `backend/ports.py:53-55` (nuevo campo `privacy` en `data`) | `privacy` Privacy Parameter (referencia create); `LIVE_VIDEO__PRIVACY_REQUIRED` (reference); "define audiences" (overview 2026-07-02) |
| D12 | Managed-terceros exige app Live + Advanced (dev centralizada NO sirve a clientes); coste cero; sin bloqueo de seguridad | Patrón backend existente (`backend/app.py:107-177` prod wiring; `docs/PRIVACY.md`) | `.../docs/apps/review` (sin rol → review); AR-FAQs (tiempos); tutorial (checklist); Platform Terms §3/§6 |
| D13 | Diseño agnóstico a la fuente de ingesta (Aitum es una instancia, no dependencia) | Sin APIs/archivos/procesos externos (precedente `AGENTS.md` §24) | `live_videos` list + `status`/`privacy` (referencia v25/v26); H1 a validar en FB-1 |
| D8 | `TokenStore` sin refresh clásico (re-exchange + `SESSION_EXPIRED`) | `TokenStore` `backend/ports.py:58-68`; `ensure_fresh_token` `backend/oauth.py:125-161`; `SESSION_EXPIRED` `backend/errors.py:17` | long-lived guide 2026-06-30 (60 días user, page sin expiración) |
| D9 | E2E por perfil (60 d OK); Page real pendiente de Page 100+ | Runners redactados patrón `tools/t030_live_*.py`; revoke `revokeEndpoint` `src/metadata.h:130` | elegibilidad 2024-06-10 (help center + 1363120/1363144); test users doc |
| D10 | Cero toques Aitum/RTMP/keys; revoke `DELETE /permissions` en Disconnect | Boundary `backend_auth` + borrado+revoke `backend/oauth.py:205-214` | permissions reference v26.0 (`DELETE /{user-id}/permissions`) |
| D14 | El producto no exige empresa: sin registro de autónomo el techo es Independent BYO-app (D4, título+desc+audiencia+publicar por usuario); Managed-terceros tras autónomo + verificación + review | Patrón BYO `src/secure_store.h:34-42` + app-modes `backend/app.py:107-177` | blog Meta 2023-02-01 (fin individual verification) + flujo Sole Proprietor + Help Centre `159334372093366` |

### 9) Estado de la cuenta del operador (2026-09-20) y acciones

- El operador tiene acceso a `developers.facebook.com/apps/` y puede crear
  aplicaciones: punto de partida suficiente para FB-1/FB-2/FB-3 desarrollo.
- Observado en captura: **cero apps creadas** y **"Ningún portafolio
  empresarial"**. Sin portafolio no hay verificación de empresa, y sin ella
  no hay Advanced ni App Review (D12). Crear el portafolio es acción del
  operador, no código.
- Al crear la app: tipo **Consumer** (o sin tipo business), nunca Business
  (las Business no disponen de permisos de usuario como `publish_video`);
  añadir caso de uso Facebook Login; registrar redirect URIs (dev localhost,
  prod `https://<backend>/connect/facebook/callback`); callback de
  desautorización y de borrado de datos.
- Checklist operador: 1) crear app + anotar App ID (el secret solo vive en
  Secret Manager / DPAPI local, jamás en chat ni repo); 2) crear portafolio
  empresarial; 3) E2E perfil con la app en Development (sin revisión);
  4) verificación de empresa + App Review solo antes del Managed comercial.
- App creada 2026-09-20: `manage-streams` (Consumer, portafolio del operador).
  Conjunto definitivo App Review: feature **Live Video API** + **publish_video**
  (perfil) + **pages_manage_posts** + **pages_read_engagement** +
  **pages_show_list** (Page; los dos últimos para publicar y listar) +
  **public_profile** (automático). **No** pedir `publish_to_groups` (Groups API
  deprecada en v19) ni `email` (no lo necesitamos: identidad vía API Live).
  NO enviar la revisión hasta FB-3 (screencast con flujos funcionando +
  verificación de empresa completa).
- Colombia (operador residente, 2026-09-20): Meta no lista documentos
  específicos del país → aplica lista "Other countries" (licencias, registro
  fiscal, bank statement, factura de servicios). Documentos locales que
  encajan: **RUT DIAN** (registro fiscal, gratuito, en línea; persona natural
  con actividad económica) como `Business Tax registration`; **certificado de
  Cámara de Comercio / matrícula mercantil** (persona natural comerciante)
  como `Business registration`; factura de servicios públicos solo para
  dirección/teléfono (no valida el nombre legal). Aceptación del RUT como
  único documento pendiente de confirmación empírica al enviar (Meta valida
  caso por caso; el nombre debe coincidir exacto).

**No se avanza a FB-1 en esta sesión.** Gate pendiente: visto bueno del
operador a este brief (D1–D13, en especial D6R/D7/D11 y vía E2E D9). Sondas
FB-1 propuestas (sin riesgo): listar `live_videos` con ingesta externa en
vista previa y comparar contra el ID de la URL de Live Producer; update
título+desc+privacy en vista previa con read-back; transición a LIVE_NOW
solo con aprobación explícita.
